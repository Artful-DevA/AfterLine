// SPDX-License-Identifier: MIT
#include "core/Json.h"

#include <QFile>
#include <QJsonDocument>
#include <QSaveFile>

namespace afterline {

QJsonObject readJsonFile(const QString &path, QString *error)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error)
            *error = f.errorString();
        return {};
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (!doc.isObject()) {
        if (error)
            *error = pe.errorString();
        return {};
    }
    return doc.object();
}

bool writeTextFile(const QString &path, const QString &text, QString *error)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly)) {
        if (error)
            *error = f.errorString();
        return false;
    }
    f.write(text.toUtf8());
    if (!f.commit()) {
        if (error)
            *error = f.errorString();
        return false;
    }
    return true;
}

bool writeJsonFile(const QString &path, const QJsonObject &object, QString *error)
{
    return writeTextFile(path, QString::fromUtf8(QJsonDocument(object).toJson(QJsonDocument::Indented)), error);
}

}  // namespace afterline
