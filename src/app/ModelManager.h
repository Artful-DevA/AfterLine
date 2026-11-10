// SPDX-License-Identifier: MIT
//
// Downloads, verifies, imports and removes AI models.
//
// Downloads resume where they stopped (a ".part" file and an HTTP range
// request) and are only installed after their SHA-256 matches the catalog.
// Imported files (for computers without internet) are accepted only if their
// hash matches a catalog entry. Nothing is ever downloaded without the user
// asking for it.
#pragma once

#include "app/ModelCatalog.h"

#include <QHash>
#include <QObject>
#include <QPointer>

#include <atomic>

class QFile;
class QNetworkAccessManager;
class QNetworkReply;

namespace afterline {

class ModelManager : public QObject {
    Q_OBJECT
public:
    explicit ModelManager(QObject *parent = nullptr);
    ~ModelManager() override;

    bool isDownloading(const QString &id) const { return m_jobs.contains(id); }
    bool anyDownloading() const { return !m_jobs.isEmpty(); }
    // Starts a download (and the speech detector with any speech model).
    // Returns false with a reason if it cannot start (e.g. not enough space).
    bool download(const QString &id, QString *error = nullptr);
    void cancel(const QString &id);
    bool remove(const QString &id, QString *error = nullptr);
    // Verifies a local file against the catalog and copies it into place.
    void importFile(const QString &path);
    qint64 freeSpace() const;

Q_SIGNALS:
    void progress(const QString &id, qint64 received, qint64 total);
    void verifying(const QString &id);
    void finished(const QString &id, bool ok, const QString &error);
    void imported(const QString &id, bool ok, const QString &message);
    void changed();

private:
    struct Job {
        QPointer<QNetworkReply> reply;
        QFile *file = nullptr;
        qint64 resumeFrom = 0;
        int retries = 0;
    };
    void start(const QString &id);
    void onData(const QString &id);
    void onFinished(const QString &id);
    void verifyAndInstall(const QString &id);
    void fail(const QString &id, const QString &error);

    QNetworkAccessManager *m_net;
    QHash<QString, Job> m_jobs;
};

// SHA-256 of a file, hex. Blocking; call from a worker thread for big files.
QString sha256File(const QString &path, const std::atomic<bool> *cancel = nullptr);

}  // namespace afterline
