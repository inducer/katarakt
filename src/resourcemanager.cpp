#include <iostream>
#include <limits>
#include <cerrno>
#include <unistd.h>
#include <QSocketNotifier>
#include <QFileInfo>
#ifdef __linux__
#include <sys/inotify.h>
#endif
#include "resourcemanager.h"
#include "canvas.h"
#include "util.h"
#include "kpage.h"
#include "worker.h"
#include "viewer.h"
#include "beamerwindow.h"
#include "selection.h"
#include "layout/layout.h"

using namespace std;


Request::Request(int width, int index) {
	for (int i = 0; i < 3; i++) {
		this->width[i] = -1;
	}
	this->width[index] = width;
}

int Request::get_lowest_index() {
	for (int i = 0; i < 3; i++) {
		if (width[i] != -1) {
			return i;
		}
	}
	// will not happen
	return 0;
}

bool Request::has_index(int index) {
	return width[index] != -1;
}

bool Request::remove_index_ok(int index) {
	width[index] = -1;
	for (int i = 0; i < 3; i++) {
		if (width[i] != -1) {
			return true;
		}
	}
	return false;
}

void Request::update(int width, int index) {
	this->width[index] = width;
}


ResourceManager::ResourceManager(const QString &file, Viewer *v) :
		viewer(v),
		file(file),
		center_page(0),
		rotation(0),
#ifdef __linux__
		i_notifier(nullptr),
#endif
		inverted_colors(false),
		cur_jump_pos(jumplist.end()) {
	initialize(file, QByteArray());
}

void ResourceManager::initialize(const QString &file, const QByteArray &password) {
	page_count = 0;
	k_page = nullptr;

	doc.reset();
	if (!file.isNull()) {
		doc = Poppler::Document::load(file, QByteArray(), password);
	}

	worker = new Worker(this);
	if (viewer->get_canvas() != nullptr) {
		// on first start the canvas has not yet been constructed
		connect(worker, SIGNAL(page_rendered(int)), viewer->get_canvas(), SLOT(page_rendered(int)), Qt::UniqueConnection);
		connect(worker, SIGNAL(page_rendered(int)), viewer->get_beamer(), SLOT(page_rendered(int)), Qt::UniqueConnection);
	}
	worker->start();

	// setup inotify
#ifdef __linux__
	QFileInfo info(file);
	inotify_fd = inotify_init();
	if (inotify_fd == -1) {
		cerr << "inotify_init: " << strerror(errno) << endl;
	} else {
		i_notifier = new QSocketNotifier(inotify_fd, QSocketNotifier::Read, this);
		connect(i_notifier, SIGNAL(activated(int)), this, SLOT(inotify_slot()),
				Qt::UniqueConnection);

		inotify_wd = inotify_add_watch(inotify_fd, info.path().toUtf8().constData(), IN_CLOSE_WRITE | IN_MOVED_TO);
		if (inotify_wd == -1) {
			cerr << "inotify_add_watch: " << strerror(errno) << endl;
		}
	}
#endif

	if (!doc) {
		// poppler already prints a debug message
//		cerr << "failed to open file" << endl;
		return;
	}
	if (doc->isLocked()) {
		// poppler already prints a debug message
//		cerr << "missing password" << endl;
		return;
	}
	doc->setRenderHint(Poppler::Document::Antialiasing, true);
	doc->setRenderHint(Poppler::Document::TextAntialiasing, true);
	doc->setRenderHint(Poppler::Document::TextHinting, true);
#if POPPLER_VERSION >= POPPLER_VERSION_CHECK(0, 18, 0)
	doc->setRenderHint(Poppler::Document::TextSlightHinting, true);
#endif
#if POPPLER_VERSION >= POPPLER_VERSION_CHECK(0, 22, 0)
//	doc->setRenderHint(Poppler::Document::OverprintPreview, true); // TODO what is this?
#endif
#if POPPLER_VERSION >= POPPLER_VERSION_CHECK(0, 24, 0)
	doc->setRenderHint(Poppler::Document::ThinLineSolid, true); // TODO what's the difference between ThinLineSolid and ThinLineShape?
#endif

	page_count = doc->numPages();

	min_aspect = numeric_limits<float>::max();
	max_aspect = numeric_limits<float>::min();

	k_page = new KPage[get_page_count()];
	for (int i = 0; i < get_page_count(); i++) {
		auto p = doc->page(i);
		if (!p) {
			cerr << "failed to load page " << i << endl;
			continue;
		}
		k_page[i].width = p->pageSizeF().width();
		k_page[i].height = p->pageSizeF().height();

		float aspect = k_page[i].width / k_page[i].height;
		if (aspect < min_aspect) {
			min_aspect = aspect;
		}
		if (aspect > max_aspect) {
			max_aspect = aspect;
		}

//		k_page[i].label = p->label();
//		if (k_page[i].label != QString::number(i + 1)) {
//			cout << i << endl;
//		}
	}
}

ResourceManager::~ResourceManager() {
	shutdown();
}

void ResourceManager::shutdown() {
	if (worker != nullptr) {
		join_threads();
	}
	garbageMutex.lock();
	for (int i = 0; i < 3; i++) {
		garbage[i].clear();
	}
	garbageMutex.unlock();
	requests.clear();
	requestSemaphore.acquire(requestSemaphore.available());
#ifdef __linux__
	::close(inotify_fd);
	delete i_notifier;
	i_notifier = nullptr;
#endif
	delete[] k_page;
	delete worker;
}

void ResourceManager::load(const QString &file, const QByteArray &password) {
	shutdown();
	initialize(file, password);
}

bool ResourceManager::is_valid() const {
	return (doc != nullptr);
}

bool ResourceManager::is_locked() const {
	if (doc == nullptr) {
		return false;
	}
	return doc->isLocked();
}

const QString &ResourceManager::get_file() const {
	return file;
}

void ResourceManager::set_file(const QString &new_file) {
	file = new_file;
}

const KPage *ResourceManager::get_page(int page, int width, int index) {
	if (page < 0 || page >= get_page_count()) {
		return nullptr;
	}

	// page not available or wrong size/rotation/color
	k_page[page].mutex.lock();
	bool must_invert_colors = k_page[page].inverted_colors != inverted_colors;
	if (must_invert_colors) {
		k_page[page].toggle_invert_colors();
	}

	if (k_page[page].img[index].isNull() ||
			k_page[page].status[index] != width ||
			k_page[page].rotation[index] != rotation ||
			must_invert_colors) {
		enqueue(page, width, index);
	}

	return &k_page[page];
}

int ResourceManager::get_rotation() const {
	return rotation;
}

void ResourceManager::rotate(int value, bool relative) {
	value %= 4;
	if (relative) {
		rotation = (rotation + value + 4) % 4;
	} else {
		rotation = value;
	}
}

void ResourceManager::unlock_page(int page) const {
	k_page[page].mutex.unlock();
}

void ResourceManager::invert_colors() {
	inverted_colors = !inverted_colors;
}

bool ResourceManager::are_colors_inverted() const {
	return inverted_colors;
}

void ResourceManager::collect_garbage(int keep_min, int keep_max, int index) {
	requestMutex.lock();
	if (index == 0) { // make a separate center_page for each index?
		center_page = (keep_min + keep_max) / 2;
	}
	requestMutex.unlock();
	// free distant pages
	garbageMutex.lock();
	for (set<int>::iterator it = garbage[index].begin(); it != garbage[index].end(); /* empty */) {
		int page = *it;
		if (page >= keep_min && page <= keep_max) {
			++it; // move on
			continue;
		}
		garbage[index].erase(it++); // erase and move on (iterator becomes invalid)
#ifdef DEBUG
		cerr << "    removing page " << page << endl;
#endif
		k_page[page].mutex.lock();
		k_page[page].img[index] = QImage();
		k_page[page].img_other[index] = QImage();
		k_page[page].status[index] = 0;
		k_page[page].rotation[index] = 0;
		k_page[page].mutex.unlock();
	}
	garbageMutex.unlock();

	// keep the request list small
	if (keep_max < keep_min) {
		return;
	}
	requestMutex.lock();
	for (map<int,Request>::iterator it = requests.begin(); it != requests.end(); ) {
		if ((it->first < keep_min || it->first > keep_max) && it->second.has_index(index)) {
			if (!it->second.remove_index_ok(index)) { // no index left in request -> delete
				requestSemaphore.acquire(1);
				requests.erase(it++);
			}
		} else {
			++it;
		}
	}
	requestMutex.unlock();
}

void ResourceManager::connect_canvas() const {
	connect(worker, SIGNAL(page_rendered(int)), viewer->get_canvas(), SLOT(page_rendered(int)), Qt::UniqueConnection);
	connect(worker, SIGNAL(page_rendered(int)), viewer->get_beamer(), SLOT(page_rendered(int)), Qt::UniqueConnection);
}

void ResourceManager::store_jump(int page) {
	map<int,list<int>::iterator>::iterator it = jump_map.find(page);
	if (it != jump_map.end()) {
		jumplist.erase(it->second);
	}
	jumplist.push_back(page);
	jump_map[page] = --jumplist.end();
	cur_jump_pos = jumplist.end();

//	cerr << "jumplist: ";
//	for (list<int>::iterator it = jumplist.begin(); it != jumplist.end(); ++it) {
//		if (it == cur_jump_pos) {
//			cerr << "*";
//		}
//		cerr << *it << " ";
//	}
//	cerr << endl;
}

void ResourceManager::clear_jumps() {
	jumplist.clear();
	jump_map.clear();
	cur_jump_pos = jumplist.end();
}

int ResourceManager::jump_back() {
	if (cur_jump_pos == jumplist.begin()) {
		return -1;
	}
	if (cur_jump_pos == jumplist.end()) {
		store_jump(viewer->get_canvas()->get_layout()->get_page());
		--cur_jump_pos;
	}
	--cur_jump_pos;
	return *cur_jump_pos;
}

int ResourceManager::jump_forward() {
	if (cur_jump_pos == jumplist.end() || cur_jump_pos == --jumplist.end()) {
		return -1;
	}
	++cur_jump_pos;
	return *cur_jump_pos;
}

std::unique_ptr<Poppler::LinkDestination> ResourceManager::resolve_link_destination(const QString &name) const {
	return doc->linkDestination(name);
}

void ResourceManager::inotify_slot() {
#ifdef __linux__
	i_notifier->setEnabled(false);

	size_t event_size = sizeof(struct inotify_event) + NAME_MAX + 1;
	// take care of alignment
	struct inotify_event i_buf[event_size / sizeof(struct inotify_event) + 1]; // has at least event_size
	char *buf = reinterpret_cast<char *>(i_buf);

	ssize_t bytes = read(inotify_fd, buf, sizeof(i_buf));
	if (bytes == -1) {
		cerr << "read: " << strerror(errno) << endl;
	} else {
		ssize_t offset = 0;
		while (offset < bytes) {
			struct inotify_event *event = reinterpret_cast<struct inotify_event *>(&buf[offset]);

			QFileInfo info(file);
			if (info.fileName() == QString::fromLocal8Bit(event->name)) {
				viewer->reload(false); // don't clamp
				i_notifier->setEnabled(true);
				return;
			}

			offset += sizeof(struct inotify_event) + event->len;
		}
	}

	i_notifier->setEnabled(true);
#endif
}

void ResourceManager::enqueue(int page, int width, int index) {
	requestMutex.lock();
	map<int,Request>::iterator it = requests.find(page);
	if (it == requests.end()) {
		requests.insert(make_pair(page, Request(width, index)));
		requestSemaphore.release(1);
	} else {
		it->second.update(width, index);
	}
	requestMutex.unlock();
}

//QString ResourceManager::get_page_label(int page) const {
//	if (page < 0 || page >= get_page_count()) {
//		return QString();
//	}
//	return k_page[page].label;
//}

float ResourceManager::get_page_width(int page, bool rotated) const {
	if (page < 0 || page >= get_page_count()) {
		return -1;
	}
	if (!rotated || rotation == 0 || rotation == 2) {
		return k_page[page].width;
	}
	// swap if rotated by 90 or 270 degrees
	return k_page[page].height;
}

float ResourceManager::get_page_height(int page, bool rotated) const {
	if (page < 0 || page >= get_page_count()) {
		return -1;
	}
	if (!rotated || rotation == 0 || rotation == 2) {
		return k_page[page].height;
	}
	return k_page[page].width;
}

float ResourceManager::get_page_aspect(int page, bool rotated) const {
	if (page < 0 || page >= get_page_count()) {
		return -1;
	}
	if (!rotated || rotation == 0 || rotation == 2) {
		return k_page[page].width / k_page[page].height;
	}
	return k_page[page].height / k_page[page].width;
}

float ResourceManager::get_min_aspect(bool rotated) const {
	if (!rotated || rotation == 0 || rotation == 2) {
		return min_aspect;
	} else {
		return 1.0f / max_aspect;
	}
}

float ResourceManager::get_max_aspect(bool rotated) const {
	if (!rotated || rotation == 0 || rotation == 2) {
		return max_aspect;
	} else {
		return 1.0f / min_aspect;
	}
}

int ResourceManager::get_page_count() const {
	return page_count;
}

const std::vector<std::unique_ptr<Poppler::Link>> &ResourceManager::get_links(int page) {
	if (page < 0 || page >= get_page_count()) {
		static std::vector<std::unique_ptr<Poppler::Link>> empty;
		return empty;
	}

	return k_page[page].links;
}

const QList<SelectionLine *> *ResourceManager::get_text(int page) {
	if (page < 0 || page >= get_page_count()) {
		return nullptr;
	}
	link_mutex.lock();
	QList<SelectionLine *> *t = k_page[page].text;
	link_mutex.unlock();
	return t;
}

QVector<Poppler::OutlineItem> ResourceManager::get_outline() const {
	if (doc == nullptr || doc->isLocked()) {
		static QVector<Poppler::OutlineItem> empty;
		return empty;
	}

	return doc->outline();
}

void ResourceManager::join_threads() {
	worker->die = true;
	requestSemaphore.release(1);
	worker->wait();
}

