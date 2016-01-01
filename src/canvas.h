#ifndef CANVAS_H
#define CANVAS_H

#include <QWidget>
#include <QPaintEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QResizeEvent>
#include <QList>
#include <QTimer>
#include <sys/socket.h>


class Viewer;
class Layout;
class SingleLayout;
class GridLayout;
class PresenterLayout;
class GotoLine;
class QLabel;


class Canvas : public QWidget {
	Q_OBJECT

public:
	Canvas(Viewer *v, QWidget *parent = 0);
	~Canvas();

	bool is_valid() const;
	void reload(bool clamp);

	void set_search_visible(bool visible);

	Layout *get_layout() const;

	void update_page_overlay();

protected:
	// QT event handling
	void paintEvent(QPaintEvent *event);
	void mousePressEvent(QMouseEvent *event);
	void mouseReleaseEvent(QMouseEvent *event);
	void mouseMoveEvent(QMouseEvent *event);
	void wheelEvent(QWheelEvent *event);
	void mouseDoubleClickEvent(QMouseEvent * event);
	void resizeEvent(QResizeEvent *event);

signals:
	/**
	 * Emitted when the user requests to see the source code of a
	 * particular point on a particular page.
	 */
	void synchronize_editor(int page, int x, int y);

private slots:
	void page_rendered(int page);
	void goto_page();

	// primitive actions
	void set_single_layout();
	void set_grid_layout();
	void set_presenter_layout();

	void toggle_overlay();
	void focus_goto();

	void disable_triple_click();
	void hide_mouse_pointer();

	void swap_selection_and_panning_buttons();

private:
	void setup_keys(QWidget *base);

	Viewer *viewer;
	Layout *cur_layout;
	SingleLayout *single_layout;
	GridLayout *grid_layout;
	PresenterLayout *presenter_layout;

	GotoLine *goto_line;
	QLabel *page_overlay;

	int mx, my;
	int mx_down, my_down;
	bool triple_click_possible;

	int hide_mouse_timeout;
	QTimer hide_mouse_timer;
	Qt::CursorShape last_cursor;

	bool valid;

	// config options
	QColor background;
	QColor background_fullscreen;
	int mouse_wheel_factor;

	Qt::MouseButton click_link_button;
	Qt::MouseButton drag_view_button;
	Qt::MouseButton select_text_button;
};

#endif

