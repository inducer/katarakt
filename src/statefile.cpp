#include <iostream>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QStandardPaths>
#include "statefile.h"

using namespace std;


QString StateFile::get_state_file_path() {
	QString config_dir = QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation);
	return config_dir + QString::fromUtf8("/katarakt/state.json");
}

QJsonObject StateFile::load_all() {
	QFile file(get_state_file_path());
	if (!file.open(QIODevice::ReadOnly)) {
		return QJsonObject();
	}
	QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
	if (doc.isNull() || !doc.isObject()) {
		return QJsonObject();
	}
	return doc.object();
}

void StateFile::save_all(const QJsonObject &all_states) {
	QString path = get_state_file_path();
	QDir dir;
	dir.mkpath(QFileInfo(path).path());

	QFile file(path);
	if (!file.open(QIODevice::WriteOnly)) {
		cerr << "failed to open state file for writing: "
		     << path.toUtf8().constData() << endl;
		return;
	}
	if (file.write(QJsonDocument(all_states).toJson()) < 0) {
		cerr << "failed to write state file: "
		     << path.toUtf8().constData() << endl;
	}
}

QJsonObject StateFile::load_state(const QString &file_path) {
	return load_all()[file_path].toObject();
}

void StateFile::save_state(const QString &file_path, const QJsonObject &state) {
	QJsonObject all_states = load_all();
	all_states[file_path] = state;
	save_all(all_states);
}
