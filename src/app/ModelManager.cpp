// SPDX-License-Identifier: MIT
#include "app/ModelManager.h"

#include "core/Paths.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStorageInfo>
#include <QTimer>

#include <atomic>
#include <thread>

namespace afterline {

QString sha256File(const QString &path, const std::atomic<bool> *cancel)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QByteArray buf(4 << 20, Qt::Uninitialized);
    for (;;) {
        if (cancel && cancel->load())
            return {};
        const qint64 n = f.read(buf.data(), buf.size());
        if (n <= 0)
            break;
#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
        hash.addData(QByteArrayView(buf.constData(), n));
#else
        hash.addData(buf.constData(), int(n));
#endif
    }
    return QString::fromLatin1(hash.result().toHex());
}

namespace {
// Runs fn on a worker thread and delivers the result on the receiver's thread.
template <typename Fn, typename Done>
void runAsync(QObject *receiver, Fn fn, Done done)
{
    QPointer<QObject> guard(receiver);
    std::thread([guard, fn, done]() {
        auto result = fn();
        if (guard)
            QMetaObject::invokeMethod(guard, [result, done]() { done(result); }, Qt::QueuedConnection);
    }).detach();
}
}  // namespace

ModelManager::ModelManager(QObject *parent) : QObject(parent), m_net(new QNetworkAccessManager(this))
{
    m_net->setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
}

ModelManager::~ModelManager()
{
    for (auto &job : m_jobs) {
        if (job.reply)
            job.reply->abort();
        if (job.file) {
            job.file->close();
            delete job.file;
        }
    }
}

qint64 ModelManager::freeSpace() const { return QStorageInfo(modelsDir()).bytesAvailable(); }

bool ModelManager::download(const QString &id, QString *error)
{
    const ModelSpec *spec = findModel(id);
    if (!spec) {
        if (error)
            *error = tr("Unknown model.");
        return false;
    }
    QStringList ids{id};
    if (spec->kind == ModelKind::Speech) {
        const ModelSpec *vad = findModel(recommendedModel(ModelKind::SpeechDetector));
        if (vad && !vad->installed())
            ids << vad->id;
    }
    qint64 needed = 0;
    for (const auto &i : ids) {
        const ModelSpec *s = findModel(i);
        const qint64 have = QFileInfo(s->path() + ".part").size();
        if (!s->installed())
            needed += s->bytes - have;
    }
    // Keep a safety margin so a model never fills the disk recordings need.
    const qint64 margin = 1024LL * 1024 * 1024;
    if (freeSpace() >= 0 && needed + margin > freeSpace()) {
        if (error)
            *error = tr("Not enough free disk space. This model needs %1, plus 1 GB kept free for your "
                        "recordings; %2 is available.")
                         .arg(formatBytes(needed), formatBytes(freeSpace()));
        return false;
    }
    for (const auto &i : ids)
        if (!findModel(i)->installed() && !m_jobs.contains(i))
            start(i);
    return true;
}

void ModelManager::start(const QString &id)
{
    const ModelSpec *spec = findModel(id);
    Job job;
    job.file = new QFile(spec->path() + ".part");
    if (!job.file->open(QIODevice::ReadWrite)) {
        delete job.file;
        Q_EMIT finished(id, false, tr("Cannot write to %1.").arg(modelsDir()));
        return;
    }
    job.resumeFrom = job.file->size();
    if (job.resumeFrom > spec->bytes) {
        job.file->resize(0);
        job.resumeFrom = 0;
    }
    job.file->seek(job.resumeFrom);
    if (m_jobs.contains(id))
        job.retries = m_jobs[id].retries;
    m_jobs[id] = job;
    if (job.resumeFrom == spec->bytes) {
        verifyAndInstall(id);
        return;
    }

    QNetworkRequest req{QUrl(spec->url)};
    // A generic client name: no user or machine details leave the computer.
    req.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("AfterLine/" AFTERLINE_VERSION));
    if (job.resumeFrom > 0)
        req.setRawHeader("Range", "bytes=" + QByteArray::number(job.resumeFrom) + "-");
    req.setTransferTimeout(60000);
    QNetworkReply *reply = m_net->get(req);
    m_jobs[id].reply = reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, id] { onData(id); });
    connect(reply, &QNetworkReply::finished, this, [this, id] { onFinished(id); });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, id](qint64 received, qint64) {
        const ModelSpec *s = findModel(id);
        if (m_jobs.contains(id))
            Q_EMIT progress(id, m_jobs[id].resumeFrom + received, s->bytes);
    });
    Q_EMIT progress(id, job.resumeFrom, spec->bytes);
    Q_EMIT changed();
}

void ModelManager::onData(const QString &id)
{
    auto it = m_jobs.find(id);
    if (it == m_jobs.end() || !it->reply)
        return;
    const int status = it->reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 200 && it->resumeFrom > 0) {
        // The server ignored the range request: start over.
        it->file->resize(0);
        it->file->seek(0);
        it->resumeFrom = 0;
    }
    if (status != 200 && status != 206)
        return;
    const QByteArray data = it->reply->readAll();
    if (it->file->write(data) != data.size()) {
        const QString err = it->file->errorString();
        it->reply->abort();
        fail(id, tr("Could not save the download: %1").arg(err));
    }
}

void ModelManager::onFinished(const QString &id)
{
    auto it = m_jobs.find(id);
    if (it == m_jobs.end())
        return;
    QNetworkReply *reply = it->reply;
    if (!reply)
        return;
    reply->deleteLater();
    it->reply = nullptr;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (reply->error() == QNetworkReply::OperationCanceledError && !it->file)
        return;
    if (reply->error() != QNetworkReply::NoError || (status != 200 && status != 206)) {
        if (reply->error() == QNetworkReply::OperationCanceledError)
            return;  // cancel() or a write failure already reported
        // Transient failures (Wi-Fi drop, server hiccup) retry from where
        // the download stopped.
        if (it->retries < 5 && reply->error() != QNetworkReply::ContentNotFoundError) {
            const int retries = ++it->retries;
            it->file->flush();
            it->file->close();
            delete it->file;
            it->file = nullptr;
            QTimer::singleShot(2000 * retries, this, [this, id] {
                if (m_jobs.contains(id))
                    start(id);
            });
            return;
        }
        fail(id, reply->error() == QNetworkReply::HostNotFoundError
                     ? tr("No internet connection. The download will continue from where it stopped when you "
                          "try again.")
                     : tr("The download failed (%1). Try again later; it will continue from where it stopped.")
                           .arg(reply->errorString()));
        return;
    }
    it->file->flush();
    verifyAndInstall(id);
}

void ModelManager::verifyAndInstall(const QString &id)
{
    const ModelSpec *spec = findModel(id);
    auto it = m_jobs.find(id);
    if (it->file) {
        it->file->close();
        delete it->file;
        it->file = nullptr;
    }
    const QString part = spec->path() + ".part";
    if (QFileInfo(part).size() != spec->bytes) {
        fail(id, tr("The download is incomplete. Try again; it will continue from where it stopped."));
        return;
    }
    Q_EMIT verifying(id);
    const QString expected = spec->sha256;
    runAsync(this, [part]() { return sha256File(part); }, [this, id, part, expected](const QString &got) {
        const ModelSpec *s = findModel(id);
        if (got.compare(expected, Qt::CaseInsensitive) != 0) {
            QFile::remove(part);
            fail(id, tr("The downloaded file did not pass verification and was deleted. Please try again."));
            return;
        }
        QFile::remove(s->path());
        if (!QFile::rename(part, s->path())) {
            fail(id, tr("Could not install the model in %1.").arg(modelsDir()));
            return;
        }
        m_jobs.remove(id);
        Q_EMIT finished(id, true, {});
        Q_EMIT changed();
    });
}

void ModelManager::fail(const QString &id, const QString &error)
{
    auto it = m_jobs.find(id);
    if (it != m_jobs.end()) {
        if (it->file) {
            it->file->close();
            delete it->file;
        }
        m_jobs.erase(it);
    }
    Q_EMIT finished(id, false, error);
    Q_EMIT changed();
}

void ModelManager::cancel(const QString &id)
{
    auto it = m_jobs.find(id);
    if (it == m_jobs.end())
        return;
    QNetworkReply *reply = it->reply;
    if (it->file) {
        it->file->close();
        delete it->file;
        it->file = nullptr;
    }
    m_jobs.erase(it);
    if (reply)
        reply->abort();
    Q_EMIT finished(id, false, tr("Download paused. It will continue from where it stopped."));
    Q_EMIT changed();
}

bool ModelManager::remove(const QString &id, QString *error)
{
    const ModelSpec *spec = findModel(id);
    if (!spec)
        return false;
    cancel(id);
    QFile::remove(spec->path() + ".part");
    if (QFileInfo::exists(spec->path()) && !QFile::remove(spec->path())) {
        if (error)
            *error = tr("Could not remove %1.").arg(spec->path());
        return false;
    }
    Q_EMIT changed();
    return true;
}

void ModelManager::importFile(const QString &path)
{
    runAsync(this, [path]() { return sha256File(path); }, [this, path](const QString &hash) {
        const ModelSpec *spec = findModelBySha256(hash);
        if (!spec) {
            Q_EMIT imported({}, false,
                            tr("This file is not one of the models AfterLine supports (its checksum does not "
                               "match). Download the model file listed in Settings → AI models on another "
                               "computer and import that file."));
            return;
        }
        if (spec->installed()) {
            Q_EMIT imported(spec->id, true, tr("%1 is already installed.").arg(spec->name));
            return;
        }
        if (QStorageInfo(modelsDir()).bytesAvailable() < spec->bytes + 512LL * 1024 * 1024) {
            Q_EMIT imported(spec->id, false, tr("Not enough free disk space to import this model."));
            return;
        }
        QFile::remove(spec->path() + ".part");
        if (!QFile::copy(path, spec->path() + ".part") || !QFile::rename(spec->path() + ".part", spec->path())) {
            QFile::remove(spec->path() + ".part");
            Q_EMIT imported(spec->id, false, tr("Could not copy the model into %1.").arg(modelsDir()));
            return;
        }
        Q_EMIT imported(spec->id, true, tr("%1 was imported and verified.").arg(spec->name));
        Q_EMIT changed();
    });
}

}  // namespace afterline
