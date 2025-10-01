#pragma once

#include <QImage>
#include <QMutex>
#include <poppler-qt6.h>


class SelectionLine;


class KPage {
private:
	KPage();
	~KPage();

public:
	const QImage *get_image(int index = 0) const;
	int get_width(int index = 0) const;
	char get_rotation(int index = 0) const;
	const QList<SelectionLine *> *get_text() const;
//	QString get_label() const;

private:
	void toggle_invert_colors();

	float width;
	float height;
	QImage img[3];
	QImage thumbnail;
	// for inverted colors with reduced contrast
	// img_other contain the currently not needed color versions
	// img store the current versions to be displayed
	QImage img_other[3];
	QImage thumbnail_other;

//	QString label;
	bool links_initialized = false;
	std::vector<std::unique_ptr<Poppler::Link>> links;

	QMutex mutex;
	int status[3];
	char rotation[3];
	bool inverted_colors = false; // img[]s and thumb must be consistent

	// this variable is for memory management of TextBoxes
	// this way the SelectionLines can work with poppler-qt6 with minimal changes
	std::vector<std::unique_ptr<Poppler::TextBox>> text_boxes;
	QList<SelectionLine *> *text = nullptr;

	friend class Worker;
	friend class ResourceManager;
};
