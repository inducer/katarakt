#pragma once

#include "layout.h"


class SingleLayout : public Layout {
public:
	SingleLayout(Viewer *v, int render_index, int page = 0);
	~SingleLayout() {}

	const QRect calculate_placement(int page) const;
	void render(QPainter *painter, double device_pixel_ratio) override;

	void advance_invisible_hit(bool forward = true) override;

	std::pair<int, QPointF> get_location_at(int px, int py) const override;

	bool page_visible(int p) const override;

private:
	int calculate_fit_width(int page) const;
};
