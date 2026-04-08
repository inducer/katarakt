#include <QAction>
#include <QKeySequence>
#include <QString>
#include <QPainter>
#include <QApplication>
#include <QTimer>
#include <QLabel>
#include <iostream>
#include "canvas.h"
#include "viewer.h"
#include "layout/layout.h"
#include "layout/singlelayout.h"
#include "layout/gridlayout.h"
#include "layout/presenterlayout.h"
#include "resourcemanager.h"
#include "search.h"
#include "gotoline.h"
#include "config.h"
#include "beamerwindow.h"
#include "util.h"

using namespace std;


Canvas::Canvas(Viewer *v, QWidget *parent) :
		QWidget(parent),
		viewer(v),
		triple_click_possible(false),
		last_cursor(Qt::BlankCursor),
		valid(true) {
	setFocusPolicy(Qt::StrongFocus);

	// load config options
	CFG *config = CFG::get_instance();

	{
		bool ok;
		unsigned int color = config->get_value("Settings/background_color").toString().toUInt(&ok, 16);
		if (ok) {
			background.setRgba(color);
		} else {
			cerr << "failed to parse background_color" << endl;
		}
	}
	{
		bool ok;
		unsigned int color = config->get_value("Settings/background_color_fullscreen").toString().toUInt(&ok, 16);
		if (ok) {
			background_fullscreen.setRgba(color);
		} else {
			cerr << "failed to parse background_color_fullscreen" << endl;
		}
	}
	mouse_wheel_factor = config->get_value("Settings/mouse_wheel_factor").toInt();

	switch (config->get_value("Settings/click_link_button").toInt()) {
		case 1: click_link_button = Qt::LeftButton; break;
		case 2: click_link_button = Qt::RightButton; break;
		case 3: click_link_button = Qt::MiddleButton; break;
		case 4: click_link_button = Qt::XButton1; break;
		case 5: click_link_button = Qt::XButton2; break;
		default: click_link_button = Qt::NoButton;
	}

	switch (config->get_value("Settings/drag_view_button").toInt()) {
		case 1: drag_view_button = Qt::LeftButton; break;
		case 2: drag_view_button = Qt::RightButton; break;
		case 3: drag_view_button = Qt::MiddleButton; break;
		case 4: drag_view_button = Qt::XButton1; break;
		case 5: drag_view_button = Qt::XButton2; break;
		default: drag_view_button = Qt::NoButton;
	}

	switch (config->get_value("Settings/select_text_button").toInt()) {
		case 1: select_text_button = Qt::LeftButton; break;
		case 2: select_text_button = Qt::RightButton; break;
		case 3: select_text_button = Qt::MiddleButton; break;
		case 4: select_text_button = Qt::XButton1; break;
		case 5: select_text_button = Qt::XButton2; break;
		default: select_text_button = Qt::NoButton;
	}

	single_layout = new SingleLayout(viewer, 0);
	grid_layout = new GridLayout(viewer, 0);
	presenter_layout = new PresenterLayout(viewer, 1);

	QString default_layout = config->get_value("Settings/default_layout").toString();
	if (default_layout == QString::fromUtf8("grid")) {
		cur_layout = grid_layout;
	} else if (default_layout == QString::fromUtf8("presenter")) {
		cur_layout = presenter_layout;
	} else { // "single" and everything else
		cur_layout = single_layout;
	}

	setup_keys(this);

	if (drag_view_button == Qt::LeftButton) {
		setCursor(Qt::OpenHandCursor);
	} else {
		setCursor(Qt::IBeamCursor);
	}
	last_cursor = Qt::BlankCursor;

	// prints the string representation of a key
//	cerr << QKeySequence(Qt::Key_Equal).toString().toUtf8().constData() << endl;

	goto_line = new GotoLine(viewer->get_res()->get_page_count(), this);
	goto_line->move(0, height() - goto_line->height()); // TODO goto_line->height() reports the wrong size
	goto_line->hide();
	connect(goto_line, SIGNAL(returnPressed()), this, SLOT(goto_page()), Qt::UniqueConnection);

	hide_mouse_timeout = config->get_value("Settings/hide_mouse_timeout").toInt();
	setMouseTracking(true);
	hide_mouse_timer.setSingleShot(true);
	connect(&hide_mouse_timer, SIGNAL(timeout()), this, SLOT(hide_mouse_pointer()), Qt::UniqueConnection);

	page_overlay = new QLabel(this);
	page_overlay->setMargin(1);
	page_overlay->setAutoFillBackground(true);
	page_overlay->show();

	// setup beamer
	BeamerWindow *beamer = viewer->get_beamer();
	setup_keys(beamer);
	if (cur_layout == presenter_layout) {
		beamer->show();
		viewer->show_progress(true);
	} else {
		beamer->hide();
		viewer->show_progress(false);
	}
}

Canvas::~Canvas() {
	delete page_overlay;
	delete goto_line;
	delete single_layout;
	delete grid_layout;
	delete presenter_layout;
}

bool Canvas::is_valid() const {
	return valid;
}

void Canvas::reload(bool clamp) {
	cur_layout->rebuild(clamp);
	goto_line->set_page_count(viewer->get_res()->get_page_count());
	update();
}

void Canvas::setup_keys(QWidget *base) {
	add_action(base, "Keys/goto_page", SLOT(focus_goto()), this);

	add_action(base, "Keys/set_single_layout", SLOT(set_single_layout()), this);
	add_action(base, "Keys/set_grid_layout", SLOT(set_grid_layout()), this);
	add_action(base, "Keys/set_presenter_layout", SLOT(set_presenter_layout()), this);

	add_action(base, "Keys/toggle_overlay", SLOT(toggle_overlay()), this);
	add_action(base, "Keys/swap_selection_and_panning_buttons", SLOT(swap_selection_and_panning_buttons()), this);
}

Layout *Canvas::get_layout() const {
	return cur_layout;
}

QString Canvas::get_layout_name() const {
	if (cur_layout == grid_layout) {
		return QString::fromUtf8("grid");
	} else if (cur_layout == presenter_layout) {
		return QString::fromUtf8("presenter");
	} else {
		return QString::fromUtf8("single");
	}
}

void Canvas::set_layout_by_name(const QString &name) {
	if (name == QString::fromUtf8("grid")) {
		set_grid_layout();
	} else if (name == QString::fromUtf8("presenter")) {
		set_presenter_layout();
	} else {
		if (name != QString::fromUtf8("single")) {
			cerr << "unknown layout name '" << name.toUtf8().constData()
			     << "', using single layout" << endl;
		}
		set_single_layout();
	}
}

void Canvas::update_page_overlay() {
	QString frozen_text;
	if (viewer->get_beamer()->is_frozen()) {
		frozen_text = QString::fromUtf8("Frozen: %1, ").arg(viewer->get_beamer()->get_layout()->get_page() + 1);
	}
	QString overlay_text = CFG::get_instance()->get_value("Settings/page_overlay_text").toString()
		.arg(cur_layout->get_page() + 1)
		.arg(viewer->get_res()->get_page_count());
	page_overlay->setText(frozen_text + overlay_text);
	page_overlay->adjustSize();
	page_overlay->move(width() - page_overlay->width(), height() - page_overlay->height());
}

void Canvas::paintEvent(QPaintEvent * /*event*/) {
#ifdef DEBUG
	cerr << "redraw" << endl;
#endif
	QPainter painter(this);
	if (viewer->isFullScreen()) {
		painter.fillRect(rect(), background_fullscreen);
	} else {
		painter.fillRect(rect(), background);
	}
	cur_layout->render(&painter, devicePixelRatio());
}

void Canvas::mousePressEvent(QMouseEvent *event) {
	if ((click_link_button != Qt::NoButton && event->button() == click_link_button)
			|| (drag_view_button != Qt::NoButton && event->button() == drag_view_button)) {
		mx = event->position().x();
		my = event->position().y();
		mx_down = mx;
		my_down = my;
	}
	if (drag_view_button != Qt::NoButton && event->button() == drag_view_button) {
		if (cursor().shape() != Qt::PointingHandCursor) { // TODO
			setCursor(Qt::ClosedHandCursor);
			last_cursor = Qt::BlankCursor;
		}
	}
	if (select_text_button != Qt::NoButton && event->button() == select_text_button) {
		if (triple_click_possible) {
			cur_layout->select(event->position().x(), event->position().y(), Selection::StartLine);
			triple_click_possible = false;
		} else {
			cur_layout->select(event->position().x(), event->position().y(), Selection::Start);
		}

		if (cursor().shape() != Qt::PointingHandCursor) { // TODO
			setCursor(Qt::IBeamCursor);
			last_cursor = Qt::BlankCursor;
		}
	}
}

void Canvas::mouseReleaseEvent(QMouseEvent *event) {
	if (event->button() == Qt::LeftButton && event->modifiers() == Qt::ControlModifier) {
		// emit synctex signal
		pair<int, QPointF> location = cur_layout->get_location_at(event->position().x(), event->position().y());
		// scale from [0,1] to points
		location.second.rx() *= viewer->get_res()->get_page_width(location.first, false);
		location.second.ry() *= viewer->get_res()->get_page_height(location.first, false);

		emit synchronize_editor(location.first, (int) ROUND(location.second.x()), (int) ROUND(location.second.y()));
	} else if (click_link_button != Qt::NoButton && event->button() == click_link_button) {
		if (mx_down == event->position().x() && my_down == event->position().y()) {
			pair<int, QPointF> location = cur_layout->get_location_at(mx_down, my_down);
			cur_layout->activate_link(location.first, location.second.x(), location.second.y());
		}
	}

	if (drag_view_button == Qt::LeftButton) {
		setCursor(Qt::OpenHandCursor);
	} else {
		setCursor(Qt::IBeamCursor);
	}
	last_cursor = Qt::BlankCursor;

	if (select_text_button != Qt::NoButton && event->button() == select_text_button) {
		cur_layout->copy_selection_text();
	}

	// auto-hide mouse pointer
	if (event->buttons() == 0) {
		if (hide_mouse_timeout > 0) {
			hide_mouse_timer.start(hide_mouse_timeout);
		}
	} else {
		hide_mouse_timer.stop();
	}
}

void Canvas::mouseMoveEvent(QMouseEvent *event) {
	if (drag_view_button != Qt::NoButton && event->buttons() & drag_view_button) {
		cur_layout->scroll_smooth(event->position().x() - mx, event->position().y() - my);
		mx = event->position().x();
		my = event->position().y();

		// wrap mouse around when dragging at the border
		if (mx <= 0) {
			mx = width() - 2;
			QCursor::setPos(mapToGlobal(QPoint(mx, my)));
		}
		if (mx >= width() - 1) {
			mx = 1;
			QCursor::setPos(mapToGlobal(QPoint(mx, my)));
		}

		if (my <= 0) {
			my = height() - 2;
			QCursor::setPos(mapToGlobal(QPoint(mx, my)));
		}
		if (my >= height() - 1) {
			my = 1;
			QCursor::setPos(mapToGlobal(QPoint(mx, my)));
		}
	}
	if (select_text_button != Qt::NoButton && event->buttons() & select_text_button) {
		cur_layout->select(event->position().x(), event->position().y(), Selection::End);

		// scrolling by dragging the selection
		// TODO only scrolls when the mouse is moved
		int margin = min(10, min(width() / 10, height() / 10));
		if (event->position().x() < margin) {
			cur_layout->scroll_smooth(min(margin - static_cast<int>(event->position().x()), margin) * 2, 0);
		}
		if (event->position().x() > width() - margin) {
			cur_layout->scroll_smooth(max(width() - static_cast<int>(event->position().x()) - margin, -margin) * 2, 0);
		}
		if (event->position().y() < margin) {
			cur_layout->scroll_smooth(0, min(margin - static_cast<int>(event->position().y()), margin) * 2);
		}
		if (event->position().y() > height() - margin) {
			cur_layout->scroll_smooth(0, max(height() - static_cast<int>(event->position().y()) - margin, -margin) * 2);
		}
	}

	// auto-show mouse pointer
	if (last_cursor != Qt::BlankCursor) {
		setCursor(last_cursor);
		last_cursor = Qt::BlankCursor;
	}
	// auto-hide mouse pointer
	if (event->buttons() == 0) {
		if (hide_mouse_timeout > 0) {
			hide_mouse_timer.start(hide_mouse_timeout);
		}
	} else {
		hide_mouse_timer.stop();
	}
}

void Canvas::wheelEvent(QWheelEvent *event) {
	// TODO unsure if pixelDelta should be used, documentation says it is unreliable on X11
	switch (QApplication::keyboardModifiers()) {
		// scroll
		case Qt::NoModifier:
			if (cur_layout->supports_smooth_scrolling()) {
				cur_layout->scroll_smooth(event->angleDelta().x(), event->angleDelta().y());
			} else {
				// on my laptop angleDelta returns small increments when using the touchpad
				// we want to scroll whole pages here -> accumulate deltas
				static int remainder;
				remainder -= event->angleDelta().y();
				cur_layout->scroll_page(remainder / mouse_wheel_factor);
				remainder %= mouse_wheel_factor;
			}
			break;

		// zoom
		case Qt::ControlModifier: {
			static int remainder;
			remainder += event->angleDelta().y();
			cur_layout->set_zoom(remainder / mouse_wheel_factor);
			remainder %= mouse_wheel_factor;
			break;
		}
	}
}

void Canvas::mouseDoubleClickEvent(QMouseEvent *event) {
	if (drag_view_button != Qt::NoButton && event->button() == drag_view_button) {
		cur_layout->goto_page_at(event->position().x(), event->position().y());
	}
	if (select_text_button != Qt::NoButton && event->button() == select_text_button) {
		// enable triple click, disable after timeout
		triple_click_possible = true;
		QTimer::singleShot(QApplication::doubleClickInterval(), this, SLOT(disable_triple_click()));

		cur_layout->select(event->position().x(), event->position().y(), Selection::StartWord);
	}
}

void Canvas::resizeEvent(QResizeEvent *event) {
	cur_layout->resize(event->size().width(), event->size().height());
	goto_line->move(0, height() - goto_line->height());
	page_overlay->move(width() - page_overlay->width(), height() - page_overlay->height());
}

// primitive actions
void Canvas::set_single_layout() {
	single_layout->activate(cur_layout);
	cur_layout = single_layout;
	update();
	viewer->get_beamer()->hide();
	viewer->show_progress(false);
	viewer->activateWindow();
}

void Canvas::set_grid_layout() {
	grid_layout->activate(cur_layout);
	grid_layout->rebuild();
	cur_layout = grid_layout;
	update();
	viewer->get_beamer()->hide();
	viewer->show_progress(false);
	viewer->activateWindow();
}

void Canvas::set_presenter_layout() {
	presenter_layout->activate(cur_layout);
	presenter_layout->rebuild();
	cur_layout = presenter_layout;
	update();

	// TODO move beamer to second screen
//	QDesktopWidget *desktop = QApplication::desktop();
//	if (desktop->screenCount() > 1) {
//		int primary_screen = desktop->primaryScreen();
//		int cur_screen = desktop->screenNumber(viewer);
//		cout << "primary: " << primary_screen << ", current: " << cur_screen << endl;
//	}
	if (!viewer->get_beamer()->is_frozen()) {
		viewer->get_beamer()->get_layout()->scroll_page(cur_layout->get_page(), false);
	}
	viewer->get_beamer()->show();
	viewer->show_progress(true);
}

void Canvas::toggle_overlay() {
	page_overlay->setVisible(!page_overlay->isVisible());
}

void Canvas::focus_goto() {
	goto_line->activateWindow();
	goto_line->show();
	goto_line->setFocus();
	goto_line->setText(QString::number(cur_layout->get_page() + 1));
	goto_line->selectAll();
	goto_line->move(0, height() - goto_line->height()); // TODO this is only necessary because goto_line->height() is wrong in the beginning
}

void Canvas::disable_triple_click() {
	triple_click_possible = false;
}

void Canvas::hide_mouse_pointer() {
	last_cursor = cursor().shape();
	setCursor(Qt::BlankCursor);
}

void Canvas::swap_selection_and_panning_buttons() {
	Qt::MouseButton tmp = drag_view_button;
	drag_view_button = select_text_button;
	select_text_button = tmp;

	if (drag_view_button == Qt::LeftButton) {
		setCursor(Qt::OpenHandCursor);
	} else {
		setCursor(Qt::IBeamCursor);
	}
	last_cursor = Qt::BlankCursor;
}


void Canvas::set_search_visible(bool visible) {
	cur_layout->set_search_visible(visible);
	update();
}

void Canvas::page_rendered(int page) {
	if (cur_layout->page_visible(page)) {
		update();
	}
}

void Canvas::goto_page() {
	int page = goto_line->text().toInt() - 1;
	goto_line->hide();
	setFocus(Qt::OtherFocusReason);
	cur_layout->scroll_page_top_jump(page, false);
}

