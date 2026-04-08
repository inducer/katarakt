#pragma once

#include <QString>
#include <QJsonObject>


class StateFile {
public:
	static QJsonObject load_state(const QString &file_path);
	static void save_state(const QString &file_path, const QJsonObject &state);

private:
	static QString get_state_file_path();
	static QJsonObject load_all();
	static void save_all(const QJsonObject &all_states);
};
