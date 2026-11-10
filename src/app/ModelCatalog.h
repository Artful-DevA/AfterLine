// SPDX-License-Identifier: MIT
//
// The AI models AfterLine knows how to download, verify and use. Each entry
// pins a file by its SHA-256, so a download (or an imported file) is used only
// if it is bit-for-bit the file that was tested.
#pragma once

#include <QString>
#include <QVector>

namespace afterline {

enum class ModelKind { Speech, SpeechDetector, Notes };

struct ModelSpec {
    QString id;
    ModelKind kind;
    QString name;         // shown to people
    QString description;  // one line: what it is good for
    QString fileName;
    QString url;
    QString sha256;
    qint64 bytes = 0;
    int memoryMB = 0;     // approximate RAM while running
    QString license;
    QString languages;
    bool recommended = false;

    QString path() const;  // where it lives once installed
    bool installed() const;
};

const QVector<ModelSpec> &modelCatalog();
const ModelSpec *findModel(const QString &id);
const ModelSpec *findModelBySha256(const QString &sha256);
QVector<ModelSpec> modelsOfKind(ModelKind kind);
// The default choice for this computer (memory-aware).
QString recommendedModel(ModelKind kind);
qint64 totalMemoryBytes();
qint64 availableMemoryBytes();
QString formatBytes(qint64 bytes);

}  // namespace afterline
