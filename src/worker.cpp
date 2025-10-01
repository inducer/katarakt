#include "worker.h"
#include "resourcemanager.h"
#include "kpage.h"
#include "canvas.h"
#include "selection.h"
#include "util.h"
#include "config.h"
#include <list>
#include <iostream>
#include <poppler-qt6.h>

using namespace std;


Worker::Worker(ResourceManager *res) :
		die(false),
		res(res) {
	// load config options
	CFG *config = CFG::get_instance();
	smooth_downscaling = config->get_value("Settings/thumbnail_filter").toBool();
	thumbnail_size = config->get_value("Settings/thumbnail_size").toInt();
}

void Worker::run() {
	while (1) {
		res->requestSemaphore.acquire(1);
		if (die) {
			break;
		}

		// get next page to render
		res->requestMutex.lock();
		int page, width, index;
		map<int,Request>::iterator less = res->requests.lower_bound(res->center_page);
		map<int,Request>::iterator greater = less--;
		map<int,Request>::iterator closest;

		if (greater != res->requests.end()) {
			if (greater != res->requests.begin()) {
				// favour nearby page, go down first
				if (greater->first + less->first <= res->center_page * 2) {
					closest = greater;
				} else {
					closest = less;
				}
			} else {
				closest = greater;
			}
		} else {
			closest = less;
		}

		page = closest->first;
		index = closest->second.get_lowest_index();
		width = closest->second.width[index];
		if (closest->second.remove_index_ok(index)) {
			res->requestSemaphore.release(1);
		} else {
			res->requests.erase(closest);
		}
		res->requestMutex.unlock();

		// check for duplicate requests
		KPage &kp = res->k_page[page];

		kp.mutex.lock();
		bool render_new = true;
		if (kp.status[index] == width && kp.rotation[index] == res->rotation) {
			if (kp.img[index].isNull()) { // only invert colors
				render_new = false;
			} else { // nothing to do
				kp.mutex.unlock();
				continue;
			}
		}
		int rotation = res->rotation;
		kp.mutex.unlock();

		// open page
#ifdef DEBUG
		cerr << "    rendering page " << page << " for index " << index << ", center: " << res->center_page << endl;
#endif
		std::unique_ptr<Poppler::Page> p;
		if (render_new) {
			p = res->doc->page(page);
			if (p == nullptr) {
				cerr << "failed to load page " << page << endl;
				continue;
			}

			// render page
			float dpi = 72.0 * width / res->get_page_width(page);
			QImage img = p->renderToImage(dpi, dpi, -1, -1, -1, -1,
					static_cast<Poppler::Page::Rotation>(rotation));

			if (img.isNull()) {
				cerr << "failed to render page " << page << endl;
				continue;
			}

			// insert new image
			kp.mutex.lock();
			if (kp.inverted_colors) {
				kp.img[index] = QImage();
				kp.img_other[index] = img;
			} else {
				kp.img[index] = img;
				kp.img_other[index] = QImage();
			}
			kp.status[index] = width;
			kp.rotation[index] = rotation;
		} else {
			// image already exists
			kp.mutex.lock();
		}

		if (kp.inverted_colors) {
			// generate inverted image
			kp.img[index] = kp.img_other[index];
			invert_image(&kp.img[index]);
		}

		// create thumbnail
		if (kp.thumbnail.isNull()) {
			Qt::TransformationMode mode = Qt::FastTransformation;
			if (smooth_downscaling) {
				mode = Qt::SmoothTransformation;
			}
			// scale
			if (kp.inverted_colors) {
				kp.thumbnail = kp.img_other[index].scaled(QSize(thumbnail_size, thumbnail_size), Qt::IgnoreAspectRatio, mode);
			} else {
				kp.thumbnail = kp.img[index].scaled(QSize(thumbnail_size, thumbnail_size), Qt::IgnoreAspectRatio, mode);
			}
			// rotate
			if (kp.rotation[index] != 0) {
				QTransform trans;
				trans.rotate(-kp.rotation[index] * 90);
				kp.thumbnail = kp.thumbnail.transformed(trans);
			}
			kp.thumbnail_other = kp.thumbnail;
			invert_image(&kp.thumbnail_other);
			if (kp.inverted_colors) {
				kp.thumbnail.swap(kp.thumbnail_other);
			}
		}
		kp.mutex.unlock();

		res->garbageMutex.lock();
		res->garbage[index].insert(page);
		res->garbageMutex.unlock();

		emit page_rendered(page);

		// collect goto links
		res->link_mutex.lock();
		if (!kp.links_initialized) {
			kp.links_initialized = true;
			kp.links = p->links();
		}
		if (kp.text == nullptr) {
			res->link_mutex.unlock();

			auto text = p->textList();
			// assign boxes to lines
			// make single parts from chained boxes
			set<Poppler::TextBox *> used;
			QList<SelectionPart *> selection_parts;
			for (const auto &box : text) {
				if (used.find(box.get()) != used.end()) {
					continue;
				}
				used.insert(box.get());

				SelectionPart *p = new SelectionPart(box.get());
				selection_parts.push_back(p);
				Poppler::TextBox *next = box->nextWord();
				while (next != nullptr) {
					used.insert(next);
					p->add_word(next);
					next = next->nextWord();
				}
			}

			// sort by y coordinate
			stable_sort(selection_parts.begin(), selection_parts.end(), selection_less_y);

			QRectF line_box;
			QList<SelectionLine *> *lines = new QList<SelectionLine *>();
			for (SelectionPart *part : selection_parts) {
				QRectF box = part->get_bbox();
				// box fits into line_box's line
				if (!lines->empty() && box.y() <= line_box.center().y() && box.bottom() > line_box.center().y()) {
					float ratio_w = box.width() / line_box.width();
					float ratio_h = box.height() / line_box.height();
					if (ratio_w < 1.0f) {
						ratio_w = 1.0f / ratio_w;
					}
					if (ratio_h < 1.0f) {
						ratio_h = 1.0f / ratio_h;
					}
					if (ratio_w > 1.3f && ratio_h > 1.3f) {
						lines->back()->sort();
						lines->push_back(new SelectionLine(part));
						line_box = part->get_bbox();
					} else {
						lines->back()->add_part(part);
					}
				// it doesn't fit, create new line
				} else {
					if (!lines->empty()) {
						lines->back()->sort();
					}
					lines->push_back(new SelectionLine(part));
					line_box = part->get_bbox();
				}
			}
			if (!lines->empty()) {
				lines->back()->sort();
			}

			res->link_mutex.lock();
			kp.text_boxes = std::move(text);
			kp.text = lines;
		}
		res->link_mutex.unlock();
	}
}

