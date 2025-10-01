#include <iostream>
#include "search.h"
#include "canvas.h"
#include "viewer.h"
#include "config.h"
#include "util.h"
#include "resourcemanager.h"
#include "layout/layout.h"

#include <QRegularExpression>
#include <QString>


using namespace std;


static QRectF calculate_inbetween_rect(QRectF a, QRectF b) {
	// combine previous and next rect
	QRectF inbetween_rect = a | b;

	// trim left and right edge
	if (a.center().x() > b.center().x())
		std::swap(a, b);

	if (a.right() < b.left()) {
		inbetween_rect.setLeft(a.right());
		inbetween_rect.setRight(b.left());
	}

	// trim top and bottom edge
	if (a.center().y() > b.center().x())
		std::swap(a, b);

	if (a.bottom() < b.top()) {
		inbetween_rect.setTop(a.bottom());
		inbetween_rect.setBottom(b.top());
	}

	return inbetween_rect;
}


//==[ SearchWorker ]===========================================================
SearchWorker::SearchWorker(SearchBar *_bar) :
		stop(false),
		die(false),
		bar(_bar),
		forward(true),
		use_regex(false) {
}

void SearchWorker::run() {
	while (1) {
		bar->search_mutex.lock();
		stop = false;
		if (die) {
			bar->search_mutex.unlock();
			break;
		}
		// always clear results -> empty search == stop search
		emit clear_hits();

		// get search string
		bar->term_mutex.lock();
		if (bar->term.isEmpty()) {
			bar->term_mutex.unlock();
			emit update_label_text(QString::fromUtf8("done."));
			continue;
		}
		int start = bar->start_page;
		QString search_term = bar->term;
		forward = bar->forward;
		use_regex = bar->use_regex;
		bar->term_mutex.unlock();

		// check if term contains upper case letters; if so, do case sensitive search (smartcase)
		bool has_upper_case = false;
		for (QString::const_iterator it = search_term.cbegin(); it != search_term.cend(); ++it) {
			if (it->isUpper()) {
				has_upper_case = true;
				break;
			}
		}

#ifdef DEBUG
		cerr << "'" << search_term.toUtf8().constData() << "'" << endl;
#endif

		QRegularExpression re;
		if (use_regex) {
			// try to use regex
			re.setPattern(search_term);
			if (!re.isValid())
				use_regex = false;
		}

		QString configuration_text;
		if (!use_regex) {
			configuration_text = has_upper_case ? QString::fromUtf8("[Case] ") : QString::fromUtf8("[no case] ");
		}

		emit update_label_text(QString::fromUtf8("%10\% searched, 0 hits").arg(configuration_text));

		// search all pages
		int hit_count = 0;
		int page = start;
		do {
			auto p = bar->doc->page(page);
			if (!p) {
				cerr << "failed to load page " << page << endl;
				continue;
			}

			// collect all occurrences
			QList<QRectF> *hits = new QList<QRectF>;
			if (use_regex) {
				// regex is valid -> perform regex matching
				auto text_list = p->textList();

				// precompute text length
				size_t text_length = 0u;
				for (const auto &box : text_list) {
					text_length += box->text().size();

					if (box->hasSpaceAfter()) {
						text_length++;
					} else if (box->nextWord() == nullptr) {
						text_length++;
					}
				}

				// combine full text into one string
				QString full_text;
				full_text.reserve(text_length);
				for (const auto &box : text_list) {
					full_text.append(box->text());

					if (box->hasSpaceAfter()) {
						full_text.append(QChar(QChar::Space));
					} else if (box->nextWord() == nullptr) {
						full_text.append(QChar(QChar::LineFeed));
					}
				}

				// match regex pattern
				QRegularExpressionMatchIterator match_it = re.globalMatch(full_text);
				while (match_it.hasNext()) {
					QRegularExpressionMatch match = match_it.next();

					// gather hit rects
					int offset = match.capturedStart();
					QRectF hit_rect;
					for (const auto &box : text_list) {
						if (offset < box->text().size()) {
							// the match starts in the current box -> gather bounding boxes
							int end_offset = offset + match.capturedLength();
							for (int i = std::max(offset, 0); i < std::min(end_offset, static_cast<int>(box->text().size())); ++i)
								hit_rect |= box->charBoundingBox(i);

							if (end_offset < box->text().size())
								break;
						}

						offset -= box->text().size();

						if (box->hasSpaceAfter()) {
							offset--;

							// spaces are only implicit in the text_list
							// do we need to add a bounding box for the space after?
							if (offset < 0 && offset + match.capturedLength() >= 0) {
								if (box->nextWord()) {
									hit_rect |= calculate_inbetween_rect(box->charBoundingBox(box->text().size() - 1), box->nextWord()->charBoundingBox(0));
								}
							}
						} else if (box->nextWord() == nullptr) {
							offset--;
						}
					}

					if (!hit_rect.isNull())
						hits->push_back(hit_rect);
				}

			} else {
				// use traditional search
				QList<QRectF> tmp = p->search(search_term, has_upper_case ? (Poppler::Page::SearchFlags) 0 : Poppler::Page::IgnoreCase);
				// TODO support Poppler::Page::WholeWords
				hits->swap(tmp);
			}

#ifdef DEBUG
			if (hits->size() > 0) {
				cerr << hits->size() << " hits on page " << page << endl;
			}
#endif

			// clean up when interrupted
			if (stop || die) {
				delete hits;
				break;
			}

			if (hits->size() > 0) {
				hit_count += hits->size();
				emit search_done(page, hits);
			} else {
				delete hits;
			}

			// update progress label next to the search bar
			int percent;
			if (forward) {
				percent = page + bar->doc->numPages() - start;
			} else {
				percent = start + bar->doc->numPages() - page;
			}
			percent = (percent % bar->doc->numPages()) * 100 / bar->doc->numPages();
			QString progress = QString::fromUtf8("%1%2\% searched, %3 hits")
				.arg(configuration_text)
				.arg(percent)
				.arg(hit_count);
			emit update_label_text(progress);

			if (forward) {
				if (++page == bar->doc->numPages()) {
					page = 0;
				}
			} else {
				if (--page == -1) {
					page = bar->doc->numPages() - 1;
				}
			}
		} while (page != start);
#ifdef DEBUG
		cerr << "done!" << endl;
#endif
		emit update_label_text(QString::fromUtf8("%1done, %2 hits")
				.arg(configuration_text)
				.arg(hit_count));
	}
}


//==[ SearchBar ]==============================================================
SearchBar::SearchBar(const QString &file, Viewer *v, QWidget *parent) :
		QWidget(parent),
		viewer(v) {
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	line = new QLineEdit(parent);

	regex_box = new QCheckBox(QString::fromUtf8("RegEx"));
	regex_box->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

	progress = new QLabel(QString::fromUtf8("done."));
	progress->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

	layout = new QHBoxLayout();
	layout->setContentsMargins(0, 0, 0, 0);
	layout->addWidget(line);
	layout->addWidget(progress);
	layout->addWidget(regex_box);
	setLayout(layout);

	initialize(file, QByteArray());
}

void SearchBar::initialize(const QString &file, const QByteArray &password) {
	worker = nullptr;

	doc = nullptr;
//	if (!file.isNull()) { // don't print the poppler error message for the second time
	if (!file.isEmpty()) {
		doc = Poppler::Document::load(file, QByteArray(), password);
	}

	if (!doc) {
		// poppler already prints a debug message
		return;
	}
	if (doc->isLocked()) {
		// poppler already prints a debug message
//		cerr << "missing password" << endl;
		doc = nullptr;
		return;
	}
	worker = new SearchWorker(this);
	worker->start();

	connect(line, SIGNAL(returnPressed()), this, SLOT(set_text()), Qt::UniqueConnection);
	connect(regex_box, SIGNAL(toggled(bool)), this, SLOT(set_use_regex(bool)), Qt::UniqueConnection);
	connect(worker, SIGNAL(update_label_text(const QString &)),
			progress, SLOT(setText(const QString &)), Qt::UniqueConnection);
	connect(worker, SIGNAL(search_done(int, QList<QRectF> *)),
			this, SLOT(insert_hits(int, QList<QRectF> *)), Qt::UniqueConnection);
	connect(worker, SIGNAL(clear_hits()),
			this, SLOT(clear_hits()), Qt::UniqueConnection);
}

SearchBar::~SearchBar() {
	shutdown();
	delete layout;
	delete progress;
	delete line;
}

void SearchBar::shutdown() {
	if (worker != nullptr) {
		join_threads();
	}

	if (!doc) return;

	delete worker;
}

void SearchBar::load(const QString &file, const QByteArray &password) {
	shutdown();
	initialize(file, password);
}

bool SearchBar::is_valid() const {
	return doc != nullptr;
}

void SearchBar::focus(bool forward, bool use_regex) {
	forward_tmp = forward; // only apply when the user presses enter
	use_regex_tmp = use_regex;
	regex_box->setChecked(use_regex_tmp);

	line->activateWindow();
	line->setText(term);
	line->setFocus(Qt::OtherFocusReason);
	line->selectAll();
	show();
}

const std::map<int,QList<QRectF> *> *SearchBar::get_hits() const {
	return &hits;
}

bool SearchBar::is_search_forward() const {
	return forward;
}

bool SearchBar::event(QEvent *event) {
	if (event->type() == QEvent::Hide) {
		viewer->get_canvas()->set_search_visible(false);
		return true;
	} else if (event->type() == QEvent::Show) {
		viewer->get_canvas()->set_search_visible(true);
		return true;
	}
	return QWidget::event(event);
}

void SearchBar::reset_search() {
	clear_hits();
	term = QString();
	progress->setText(QString::fromUtf8("done."));
	viewer->get_canvas()->set_search_visible(false);
	viewer->get_canvas()->setFocus(Qt::OtherFocusReason);
	hide();
}

void SearchBar::set_use_regex(bool use_regex)
{
	use_regex_tmp = use_regex;
	set_text();
}

void SearchBar::insert_hits(int page, QList<QRectF> *l) {
	bool empty = hits.empty();

	map<int,QList<QRectF> *>::iterator it = hits.find(page);
	if (it != hits.end()) {
		delete it->second;
	}
	hits[page] = l;

	if (viewer->get_canvas()->get_layout()->page_visible(page)) {
		viewer->get_canvas()->update();
	}

	// only update the layout if the hits should be viewed
	if (empty) {
		viewer->get_canvas()->get_layout()->update_search();
	}
}

void SearchBar::clear_hits() {
	for (map<int,QList<QRectF> *>::iterator it = hits.begin(); it != hits.end(); ++it) {
		delete it->second;
	}
	hits.clear();
	viewer->get_canvas()->update();
}

void SearchBar::set_text() {
	// prevent searching a non-existing document
	if (!is_valid()) {
		return;
	}

	forward = forward_tmp;
	Canvas *c = viewer->get_canvas();
	// do not start the same search again but signal slots
	if (term == line->text() && use_regex == use_regex_tmp) {
		c->setFocus(Qt::OtherFocusReason);
		c->get_layout()->update_search();
		return;
	}

	term_mutex.lock();
	start_page = c->get_layout()->get_page();
	term = line->text();
	use_regex = use_regex_tmp;
	term_mutex.unlock();

	worker->stop = true;
	search_mutex.unlock();
	c->setFocus(Qt::OtherFocusReason);
}

void SearchBar::join_threads() {
	worker->die = true;
	search_mutex.unlock();
	worker->wait();
}

