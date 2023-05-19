#ifndef SEARCH_H
#define SEARCH_H

#include <QString>
#include <QThread>
#include <QMutex>
#include <QWidget>
#include <QLineEdit>
#include <QLabel>
#include <QHBoxLayout>
#include <QRect>
#include <QEvent>
#include <QList>
#if QT_VERSION >= 0x050000
#	include <poppler-qt5.h>
#else
#	include <poppler-qt4.h>
#endif


class SearchBar;
class Canvas;
class Viewer;


class SearchWorker : public QThread {
	Q_OBJECT

public:
	SearchWorker(SearchBar *_bar);
	void run();

	volatile bool stop;
	volatile bool die;

signals:
	void update_label_text(const QString &text);
	void search_done(int page, QList<QRectF> *hits);
	void clear_hits();

private:
	SearchBar *bar;
	bool forward;
	bool use_regex;
};


class SearchBar : public QWidget {
	Q_OBJECT

public:
	SearchBar(const QString &file, Viewer *v, QWidget *parent = 0);
	~SearchBar();

	void load(const QString &file, const QByteArray &password);
	bool is_valid() const;
	void focus(bool forward = true, bool use_regex = false);
	const std::map<int,QList<QRectF> *> *get_hits() const;
	bool is_search_forward() const;

protected:
	// QT event handling
	bool event(QEvent *event);

public slots:
	void reset_search();

private slots:
	void insert_hits(int page, QList<QRectF> *hits);
	void clear_hits();
	void set_text();

private:
	void initialize(const QString &file, const QByteArray &password);
	void join_threads();
	void shutdown();

	QLineEdit *line;
	QLabel *progress;
	QHBoxLayout *layout;

	Poppler::Document *doc;
	Viewer *viewer;

	std::map<int,QList<QRectF> *> hits;

	QMutex search_mutex;
	QMutex term_mutex;
	SearchWorker *worker;
	QString term;
	int start_page = 0;
	bool forward_tmp = true;
	bool forward = true;
	bool use_regex_tmp = false;
	bool use_regex = false;

	friend class SearchWorker;
};

#endif

