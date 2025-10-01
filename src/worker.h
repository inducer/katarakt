#pragma once

#include <QThread>


class ResourceManager;
class Canvas;


class Worker : public QThread {
	Q_OBJECT

public:
	Worker(ResourceManager *res);
	void run();

	volatile bool die;

signals:
	void page_rendered(int page);

private:
	ResourceManager *res;

	// config options
	bool smooth_downscaling;
	int thumbnail_size;
};
