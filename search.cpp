#include "search.h"
#include <iostream>

using namespace std;


//==[ Result ]=================================================================
Result::Result(double _x1, double _y1, double _x2, double _y2) :
		x1(_x1), y1(_y1),
		x2(_x2), y2(_y2) {
}

QRect Result::scale_translate(double factor, double off_x, double off_y) {
	return QRect(x1 * factor + off_x, y1 * factor + off_y,
			(x2 - x1) * factor, (y2 - y1) * factor);
}


//==[ SearchWorker ]===========================================================
SearchWorker::SearchWorker(SearchBar *_bar) :
		stop(false),
		die(false),
		bar(_bar) {
}

void SearchWorker::run() {
	while (1) {
		bar->mutex.lock();
		stop = false;
		if (die) {
			break;
		}
		if (bar->term.isEmpty()) {
			continue;
		}
#ifdef DEBUG
		cerr << "'" << bar->term.toUtf8().constData() << "'" << endl;
#endif
		emit bar->search_clear();

		// search all pages
		for (int page = 0; page < bar->doc->numPages(); page++) {
			Poppler::Page *p = bar->doc->page(page);
			if (p == NULL) {
				cerr << "failed to load page " << page << endl;
				continue;
			}

			// collect all occurrences
			list<Result> *hits = new list<Result>;
			Result rect;
			while (!stop && !die &&
					p->search(bar->term, rect.x1, rect.y1, rect.x2, rect.y2,
						Poppler::Page::NextResult, Poppler::Page::CaseInsensitive)) {
				hits->push_back(rect);
			}
#ifdef DEBUG
			if (hits->size() > 0) {
				cerr << hits->size() << " hits on page " << page << endl;
			}
#endif
			delete p;

			emit bar->search_done(page, hits);

			if (stop || die) {
				break;
			}
		}
#ifdef DEBUG
		cerr << "done!" << endl;
#endif
	}
}


//==[ SearchBar ]==============================================================
SearchBar::SearchBar(QString file, QWidget *parent) :
		QLineEdit(parent),
		worker(NULL) {
	doc = Poppler::Document::load(file);
	if (doc == NULL) {
		// poppler already prints a debug message
		return;
	}
	if (doc->isLocked()) {
		cerr << "missing password" << endl;
		delete doc;
		doc = NULL;
		return;
	}
	mutex.lock();
	worker = new SearchWorker(this);
	worker->start();

	connect(this, SIGNAL(returnPressed()), this, SLOT(set_text()));
}

SearchBar::~SearchBar() {
	if (worker != NULL) {
		join_threads();
	}
	if (doc == NULL) {
		return;
	}
	delete doc;
	delete worker;
}

bool SearchBar::is_valid() const {
	return doc != NULL;
}

void SearchBar::set_text() {
	if (term == text()) {
		return;
	}
	term = text();
	worker->stop = true;
	mutex.unlock();
}

void SearchBar::join_threads() {
	worker->die = true;
	mutex.unlock();
	worker->wait();
}
