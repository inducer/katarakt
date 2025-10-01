#pragma once

#include <QTreeWidget>


class Viewer;
namespace Poppler { class OutlineItem; }


class Toc : public QTreeWidget {
	Q_OBJECT

public:
	Toc(Viewer *v, QWidget *parent = 0);
	~Toc();

	void init();

public slots:
	void goto_link(QTreeWidgetItem *item, int column);

protected:
	bool event(QEvent *e);

private:
	void shutdown();
        void build(const QVector<Poppler::OutlineItem> &items, QTreeWidgetItem *parent);

	Viewer *viewer;
};
