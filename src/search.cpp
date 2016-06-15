#include <iostream>
#include "search.h"
#include "canvas.h"
#include "viewer.h"
#include "config.h"
#include "util.h"
#include "resourcemanager.h"
#include "layout/layout.h"

using namespace std;


//==[ SearchWorker ]===========================================================
SearchWorker::SearchWorker(SearchBar *_bar) :
		stop(false),
		die(false),
		bar(_bar),
		forward(true) {
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
		bar->term_mutex.unlock();

		// check if term contains upper case letters; if so, do case sensitive search (smartcase)
		bool has_upper_case = false;
		for (QString::const_iterator it = search_term.begin(); it != search_term.end(); ++it) {
			if (it->isUpper()) {
				has_upper_case = true;
				break;
			}
		}

#ifdef DEBUG
		cerr << "'" << search_term.toUtf8().constData() << "'" << endl;
#endif
		emit update_label_text(QString::fromUtf8("[%1] 0\% searched, 0 hits")
			.arg(has_upper_case ? QString::fromUtf8("Case") : QString::fromUtf8("no case")));

		// search all pages
		int hit_count = 0;
		int page = start;
		do {
			Poppler::Page *p = bar->doc->page(page);
			if (p == NULL) {
				cerr << "failed to load page " << page << endl;
				continue;
			}

			// collect all occurrences
			QList<QRectF> *hits = new QList<QRectF>;
#if POPPLER_VERSION < POPPLER_VERSION_CHECK(0, 22, 0)
			// old search interface, slow for many hits per page
			double x = 0, y = 0, x2 = 0, y2 = 0;
			while (!stop && !die &&
					p->search(search_term, x, y, x2, y2, Poppler::Page::NextResult,
						has_upper_case ? Poppler::Page::CaseSensitive : Poppler::Page::CaseInsensitive)) {
				hits->push_back(QRectF(x, y, x2 - x, y2 - y));
			}
#elif POPPLER_VERSION < POPPLER_VERSION_CHECK(0, 31, 0)
			// new search interface
			QList<QRectF> tmp = p->search(search_term,
					has_upper_case ? Poppler::Page::CaseSensitive : Poppler::Page::CaseInsensitive);
			hits->swap(tmp);
#else
			// even newer interface
			QList<QRectF> tmp = p->search(search_term,
					has_upper_case ? (Poppler::Page::SearchFlags) 0 : Poppler::Page::IgnoreCase);
			// TODO support Poppler::Page::WholeWords
			hits->swap(tmp);
#endif
#ifdef DEBUG
			if (hits->size() > 0) {
				cerr << hits->size() << " hits on page " << page << endl;
			}
#endif
			delete p;

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
			QString progress = QString::fromUtf8("[%1] %2\% searched, %3 hits")
				.arg(has_upper_case ? QString::fromUtf8("Case") : QString::fromUtf8("no case"))
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
		emit update_label_text(QString::fromUtf8("[%1] done, %2 hits")
				.arg(has_upper_case ? QString::fromUtf8("Case") : QString::fromUtf8("no case"))
				.arg(hit_count));
	}
}


//==[ SearchBar ]==============================================================
SearchBar::SearchBar(const QString &file, Viewer *v, QWidget *parent) :
		QWidget(parent),
		viewer(v) {
	setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
	line = new QLineEdit(parent);

	progress = new QLabel(QString::fromUtf8("done."));
	progress->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);

	layout = new QHBoxLayout();
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setSpacing(0);
	layout->addWidget(line);
	layout->addWidget(progress);
	setLayout(layout);

	initialize(file, QByteArray());
}

void SearchBar::initialize(const QString &file, const QByteArray &password) {
	worker = NULL;

	doc = NULL;
//	if (!file.isNull()) { // don't print the poppler error message for the second time
	if (!file.isEmpty()) {
		doc = Poppler::Document::load(file, QByteArray(), password);
	}

	if (doc == NULL) {
		// poppler already prints a debug message
		return;
	}
	if (doc->isLocked()) {
		// poppler already prints a debug message
//		cerr << "missing password" << endl;
		delete doc;
		doc = NULL;
		return;
	}
	worker = new SearchWorker(this);
	worker->start();

	connect(line, SIGNAL(returnPressed()), this, SLOT(set_text()),
			Qt::UniqueConnection);
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
	if (worker != NULL) {
		join_threads();
	}
	if (doc == NULL) {
		return;
	}
	delete doc;
	delete worker;
}

void SearchBar::load(const QString &file, const QByteArray &password) {
	shutdown();
	initialize(file, password);
}

bool SearchBar::is_valid() const {
	return doc != NULL;
}

void SearchBar::focus(bool forward) {
	forward_tmp = forward; // only apply when the user presses enter
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
	if (term == line->text()) {
		c->setFocus(Qt::OtherFocusReason);
		c->get_layout()->update_search();
		return;
	}

	term_mutex.lock();
	start_page = c->get_layout()->get_page();
	term = line->text();
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

