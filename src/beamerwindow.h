#ifndef BEAMERWINDOW_H
#define BEAMERWINDOW_H

#include <QWidget>


class Viewer;
class Layout;

// TODO make subclass of Canvas?
class BeamerWindow : public QWidget {
	Q_OBJECT

public:
	BeamerWindow(Viewer *v, QWidget *parent = 0);
	~BeamerWindow();

	void freeze(bool f);
	bool is_frozen() const;

	bool is_valid() const;

	Layout *get_layout() const;

public slots:
	void toggle_fullscreen();

protected:
	// QT event handling
	void paintEvent(QPaintEvent *event);
	void mousePressEvent(QMouseEvent *event);
	void mouseReleaseEvent(QMouseEvent *event);
	void wheelEvent(QWheelEvent *event);
	void resizeEvent(QResizeEvent *event);

private slots:
	void page_rendered(int page);

private:
	Viewer *viewer;
	Layout *layout;

	int mx_down, my_down;

	int mouse_wheel_factor;

	Qt::MouseButton click_link_button;

	bool frozen;
	bool valid;
};

#endif

