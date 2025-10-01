#include "config.h"
#include <QVectorIterator>
#include <iostream>


using namespace std;


CFG::CFG() :
		settings(QSettings::IniFormat, QSettings::UserScope, QString::fromUtf8("katarakt")) {
	// TODO warn about invalid user input
	init_defaults();
}

CFG::CFG(const char* file) :
		// file comes from the command line, use locale
		settings(QString::fromLocal8Bit(file), QSettings::IniFormat) {
	init_defaults();
}

// explicitly interpret all strings as utf8
void CFG::default_setting(const char *name, const char *value) {
	vd.push_back(QString::fromUtf8(name));
	defaults[vd.back()] = QString::fromUtf8(value);
}

// explicitly interpret all strings as utf8
template <typename T>
void CFG::default_setting(const char *name, const T value) {
	vd.push_back(QString::fromUtf8(name));
	defaults[vd.back()] = value;
}

// explicitly interpret all strings as utf8
void CFG::default_key(const char *action, const char *key1, const char *key2 = nullptr) {
	vk.push_back(QString::fromUtf8(action));
	keys[vk.back()] = QStringList() << QString::fromUtf8(key1);
	if (key2) {
		keys[vk.back()] << QString::fromUtf8(key2);
	}
}

void CFG::init_defaults() {
	// settings
	// canvas
	default_setting("Settings/default_layout", "single");
	default_setting("Settings/background_color", "0xDF202020");
	default_setting("Settings/background_color_fullscreen", "0xFF000000");
	default_setting("Settings/unrendered_page_color", "0x40FFFFFF");

	default_setting("Settings/click_link_button", 1);
	default_setting("Settings/drag_view_button", 2);
	default_setting("Settings/select_text_button", 1);
	default_setting("Settings/hide_mouse_timeout", 2000);
	default_setting("Settings/smooth_scroll_delta", 30); // pixel scroll offset
	default_setting("Settings/screen_scroll_factor", 0.9); // creates overlap for scrolling 1 screen down, should be <= 1
	default_setting("Settings/jump_padding", 0.2); // must be <= 0.5
	default_setting("Settings/rect_margin", 2);
	default_setting("Settings/useless_gap", 2);
	default_setting("Settings/min_zoom", -14);
	default_setting("Settings/max_zoom", 30);
	default_setting("Settings/zoom_factor", 0.05);
	default_setting("Settings/min_page_width", 50);
	default_setting("Settings/presenter_slide_ratio", 0.67);
	// viewer
	default_setting("Settings/quit_on_init_fail", false);
	default_setting("Settings/single_instance_per_file", false);
	default_setting("Settings/stylesheet", "");
	default_setting("Settings/page_overlay_text", "Page %1/%2");
	default_setting("Settings/icon_theme", "");
	// internal
	default_setting("Settings/prefetch_count", 4);
	default_setting("Settings/inverted_color_contrast", 0.5);
	default_setting("Settings/inverted_color_brightening", 0.15);
	default_setting("Settings/mouse_wheel_factor", 120); // (qt-)delta for turning the mouse wheel 1 click
	default_setting("Settings/thumbnail_filter", true); // filter when creating thumbnail image
	default_setting("Settings/thumbnail_size", 32);

	// keys
	// movement
	default_key("Keys/page_up", "PgUp");
	default_key("Keys/page_down", "PgDown");
	default_key("Keys/top", "Home", "G");
	default_key("Keys/bottom", "End", "Shift+G");
	default_key("Keys/goto_page", "Ctrl+G");
	default_key("Keys/half_screen_up", "Ctrl+U");
	default_key("Keys/half_screen_down", "Ctrl+D");
	default_key("Keys/screen_up", "Backspace", "Ctrl+B");
	default_key("Keys/screen_down", "Space", "Ctrl+F");
	default_key("Keys/smooth_up", "Up", "K");
	default_key("Keys/smooth_down", "Down", "J");
	default_key("Keys/smooth_left", "Left", "H");
	default_key("Keys/smooth_right", "Right", "L");
	default_key("Keys/search", "/");
	default_key("Keys/search_backward", "?");
	default_key("Keys/regex_search", "Ctrl+/");
	default_key("Keys/regex_search_backward", "Ctrl+?");
	default_key("Keys/next_hit", "N");
	default_key("Keys/previous_hit", "Shift+N");
	default_key("Keys/next_invisible_hit", "Ctrl+N");
	default_key("Keys/previous_invisible_hit", "Ctrl+Shift+N");
	default_key("Keys/jump_back", "Ctrl+O", "Alt+Left");
	default_key("Keys/jump_forward", "Ctrl+I", "Alt+Right");
	default_key("Keys/mark_jump", "M");
	// layout
	default_key("Keys/set_single_layout", "1");
	default_key("Keys/set_grid_layout", "2");
	default_key("Keys/set_presenter_layout", "3");
	default_key("Keys/zoom_in", "=", "+");
	default_key("Keys/zoom_out", "-");
	default_key("Keys/reset_zoom", "Z");
	default_key("Keys/increase_columns", "]");
	default_key("Keys/decrease_columns", "[");
	default_key("Keys/increase_offset", "}");
	default_key("Keys/decrease_offset", "{");
	default_key("Keys/rotate_left", ",");
	default_key("Keys/rotate_right", ".");
	// viewer
	default_key("Keys/toggle_overlay", "T");
	default_key("Keys/quit", "Q", "W,E,E,E");
	default_key("Keys/close_search", "Esc");
	default_key("Keys/invert_colors", "I");
	default_key("Keys/copy_to_clipboard", "Ctrl+C");
	default_key("Keys/swap_selection_and_panning_buttons", "V");
	default_key("Keys/toggle_fullscreen", "F");
	default_key("Keys/reload", "R");
	default_key("Keys/open", "O");
	default_key("Keys/save", "S");
	default_key("Keys/toggle_toc", "F9");
	default_key("Keys/freeze_presentation", "X");

	// tmp values
	tmp_values[QString::fromUtf8("start_page")] = 0;
	tmp_values[QString::fromUtf8("fullscreen")] = false;
}

CFG::CFG(const CFG &/*other*/) {
}

CFG &CFG::operator=(const CFG &/*other*/) {
	return *this;
}

CFG::~CFG() {
//	set_defaults(); // uncomment to create ini file with all the default settings
}

void CFG::set_defaults() {
	QVectorIterator<QString> it(vd);
	while (it.hasNext()) {
		QString tmp = it.next();
		settings.setValue(tmp, defaults[tmp]);
	}

	QVectorIterator<QString> i2(vk);
	while (i2.hasNext()) {
		QString tmp = i2.next();
		settings.setValue(tmp, keys[tmp]);
	}
}

CFG *CFG::get_instance() {
	static CFG instance;
	return &instance;
}

void CFG::write_defaults(const char *file) {
	CFG inst(file);
	inst.settings.clear();
	inst.settings.setFallbacksEnabled(false);
	inst.set_defaults();
	inst.settings.sync();
	get_instance();
}

QVariant CFG::get_value(const char *_key) const {
	QString key = QString::fromUtf8(_key);
#ifdef DEBUG
	if (defaults.find(key) == defaults.end()) {
		cout << "missing key " << _key << endl;
	}
#endif
	return settings.value(key, defaults[key]);
}

void CFG::set_value(const char *key, QVariant value) {
	settings.setValue(QString::fromUtf8(key), value);
}

QVariant CFG::get_tmp_value(const char *key) const {
	return tmp_values[QString::fromUtf8(key)];
}

void CFG::set_tmp_value(const char *key, QVariant value) {
	tmp_values[QString::fromUtf8(key)] = value;
}

bool CFG::has_tmp_value(const char *key) const {
	return tmp_values.contains(QString::fromUtf8(key));
}

QVariant CFG::get_most_current_value(const char *_key) const {
	QString key = QString::fromUtf8(_key);
	if (tmp_values.contains(key)) {
		return tmp_values[key];
	} else {
#ifdef DEBUG
		if (defaults.find(key) == defaults.end()) {
			cout << "missing key " << _key << endl;
		}
#endif
		return settings.value(key, defaults[key]);
	}
}

QStringList CFG::get_keys(const char *_action) const {
	QString action = QString::fromUtf8(_action);
	return settings.value(action, keys[action]).toStringList();
}

