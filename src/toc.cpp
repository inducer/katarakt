#include <QEvent>
#include <QKeyEvent>
#include <QHeaderView>
#include <poppler-qt6.h>
#include "toc.h"
#include "viewer.h"
#include "canvas.h"
#include "layout/layout.h"
#include "resourcemanager.h"
#include "util.h"


Q_DECLARE_METATYPE(Poppler::LinkDestination)


Toc::Toc(Viewer *v, QWidget *parent) :
		QTreeWidget(parent), viewer(v) {
	setColumnCount(2);

	QHeaderView *h = header();
	h->setStretchLastSection(false);
	h->setSectionResizeMode(0, QHeaderView::Stretch);
	h->setSectionResizeMode(1, QHeaderView::ResizeToContents);

	QStringList list = QStringList() << QString::fromUtf8("Contents") << QString();
	setHeaderLabels(list);

	setAlternatingRowColors(true);

	connect(this, SIGNAL(itemActivated(QTreeWidgetItem *, int)), this, SLOT(goto_link(QTreeWidgetItem *, int)), Qt::UniqueConnection);

	init();
}

void Toc::init() {
	shutdown();

	auto outline = viewer->get_res()->get_outline();
	build(outline, invisibleRootItem());

	// indicate empty toc
	if (topLevelItemCount() == 0) {
		QTreeWidgetItem *item = new QTreeWidgetItem(invisibleRootItem(), QStringList(QString::fromUtf8("(empty)")));
		item->setFlags(Qt::NoItemFlags);
	}
}

Toc::~Toc() {
	shutdown();
}

void Toc::shutdown() {
	clear();
}

void Toc::goto_link(QTreeWidgetItem *item, int column) {
	if (column == -1) {
		return;
	}
	// handle empty-indicator
	if (item->text(1).isEmpty()) {
		return;
	}

	auto link = item->data(0, Qt::UserRole).value<QSharedPointer<const Poppler::LinkDestination>>();
	viewer->get_canvas()->get_layout()->goto_link_destination(*link);
	viewer->get_canvas()->setFocus(Qt::OtherFocusReason);
}

bool Toc::event(QEvent *e) {
	if (e->type() == QEvent::ShortcutOverride) {
		QKeyEvent *ke = static_cast<QKeyEvent *>(e);
		if (ke->key() < Qt::Key_Escape) {
			// don't accept -> other keyboard shortcuts take precedence
//			ke->accept();
		} else {
			switch (ke->key()) {
				case Qt::Key_Return:
				case Qt::Key_Enter:
				case Qt::Key_Delete:
				case Qt::Key_Home:
				case Qt::Key_End:
				case Qt::Key_Backspace:
				case Qt::Key_Left:
				case Qt::Key_Right:
				case Qt::Key_Up:
				case Qt::Key_Down:
				case Qt::Key_Tab:
					ke->accept();
				default:
					break;
			}
		}
	}
	return QTreeWidget::event(e);
}

void Toc::build(const QVector<Poppler::OutlineItem> &items, QTreeWidgetItem *parent) {
	if (items.empty()) {
		return;
	}

	for (const auto &outlineItem : items) {
		QStringList strings;
		strings << outlineItem.name();

		const auto &destination = outlineItem.destination();
		if (!destination.isNull()) {
			strings << QString::number(destination->pageNumber());
		}
		// TODO check "ExternalFileName"
		// TODO take "Open" into account?
		QTreeWidgetItem *treeItem = new QTreeWidgetItem(parent, strings);
		treeItem->setTextAlignment(1, Qt::AlignRight);
		treeItem->setData(0, Qt::UserRole, QVariant::fromValue(destination));

		build(outlineItem.children(), treeItem);
	}
}

