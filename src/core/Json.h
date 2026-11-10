// SPDX-License-Identifier: MIT
#pragma once

#include <QJsonObject>
#include <QString>

namespace afterline {

// Missing or damaged files yield an empty object (and an error message).
QJsonObject readJsonFile(const QString &path, QString *error = nullptr);
// Atomic: a crash mid-write leaves the previous version intact.
bool writeJsonFile(const QString &path, const QJsonObject &object, QString *error = nullptr);
bool writeTextFile(const QString &path, const QString &text, QString *error = nullptr);

}  // namespace afterline
