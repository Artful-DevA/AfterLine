// SPDX-License-Identifier: MIT
//
// Tells the user when a newer AfterLine is out. Once a day it asks GitHub for
// the latest release's version number; nothing about the user or their
// meetings is sent. Can be turned off in Settings.
#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QTimer;

namespace afterline {

class Settings;

class UpdateChecker : public QObject {
    Q_OBJECT
public:
    explicit UpdateChecker(Settings &settings, QObject *parent = nullptr);
    void start();
    void checkNow();
    // The newer version found, or empty (none, skipped, or not checked yet).
    QString available() const;
    // Hides the notice until an even newer version comes out.
    void skip();
    // Installed through a store (Flathub) that updates the app itself.
    static bool managedElsewhere();
    // "0.2.10" > "0.2.9"; accepts a leading "v".
    static bool isNewer(const QString &candidate, const QString &current);

Q_SIGNALS:
    void availableChanged();

private:
    Settings &m_settings;
    QNetworkAccessManager *m_net = nullptr;
    QTimer *m_timer = nullptr;
    QString m_latest;
};

}  // namespace afterline
