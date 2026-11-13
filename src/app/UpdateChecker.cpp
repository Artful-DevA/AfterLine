// SPDX-License-Identifier: MIT
#include "app/UpdateChecker.h"

#include "app/Settings.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QVersionNumber>

namespace afterline {

namespace {
const char *kLatestRelease = "https://api.github.com/repos/Artful-DevA/AfterLine/releases/latest";
QVersionNumber parse(QString v)
{
    v = v.trimmed();
    if (v.startsWith('v') || v.startsWith('V'))
        v.remove(0, 1);
    return QVersionNumber::fromString(v);
}
}  // namespace

UpdateChecker::UpdateChecker(Settings &settings, QObject *parent) : QObject(parent), m_settings(settings) {}

bool UpdateChecker::managedElsewhere() { return !qEnvironmentVariableIsEmpty("FLATPAK_ID"); }

bool UpdateChecker::isNewer(const QString &candidate, const QString &current)
{
    const QVersionNumber a = parse(candidate), b = parse(current);
    return !a.isNull() && !b.isNull() && QVersionNumber::compare(a, b) > 0;
}

void UpdateChecker::start()
{
    if (m_timer || managedElsewhere())
        return;
    m_timer = new QTimer(this);
    m_timer->setInterval(24 * 60 * 60 * 1000);
    connect(m_timer, &QTimer::timeout, this, &UpdateChecker::checkNow);
    m_timer->start();
    // Not during start-up: the first look a little later.
    QTimer::singleShot(20 * 1000, this, &UpdateChecker::checkNow);
}

void UpdateChecker::checkNow()
{
    if (!m_settings.checkForUpdates() || managedElsewhere())
        return;
    if (!m_net)
        m_net = new QNetworkAccessManager(this);
    // AFTERLINE_UPDATE_URL: a test override (a file:// URL works too).
    QNetworkRequest req{QUrl(qEnvironmentVariable("AFTERLINE_UPDATE_URL", kLatestRelease))};
    req.setHeader(QNetworkRequest::UserAgentHeader, QString("AfterLine/%1").arg(AFTERLINE_VERSION));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setTransferTimeout(20000);
    QNetworkReply *reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError)
            return;  // offline: try again tomorrow, silently
        const QJsonObject o = QJsonDocument::fromJson(reply->readAll()).object();
        if (o.value("draft").toBool() || o.value("prerelease").toBool())
            return;
        QString tag = o.value("tag_name").toString();
        if (tag.startsWith('v'))
            tag.remove(0, 1);
        if (tag.isEmpty() || tag == m_latest)
            return;
        m_latest = tag;
        Q_EMIT availableChanged();
    });
}

QString UpdateChecker::available() const
{
    if (m_latest.isEmpty() || !m_settings.checkForUpdates() || !isNewer(m_latest, AFTERLINE_VERSION))
        return {};
    if (!m_settings.skippedVersion().isEmpty() && !isNewer(m_latest, m_settings.skippedVersion()))
        return {};
    return m_latest;
}

void UpdateChecker::skip()
{
    if (m_latest.isEmpty())
        return;
    m_settings.setSkippedVersion(m_latest);
    Q_EMIT availableChanged();
}

}  // namespace afterline
