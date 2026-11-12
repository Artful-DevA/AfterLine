// SPDX-License-Identifier: MIT
//
// Desktop notifications (the freedesktop.org service that GNOME, KDE,
// Cinnamon, XFCE and others provide), with buttons such as "Record". Falls
// back to the tray icon's balloon where no notification service runs.
#pragma once

#include <QHash>
#include <QObject>
#include <QStringList>

#include <functional>

class QSystemTrayIcon;

namespace afterline {

class DesktopNotifier : public QObject {
    Q_OBJECT
public:
    struct Action {
        QString key, label;
        std::function<void()> run;
    };

    explicit DesktopNotifier(QObject *parent = nullptr);
    void setTrayFallback(QSystemTrayIcon *tray) { m_tray = tray; }
    // `onClick` runs when the notification itself is clicked. Returns false
    // when no notification could be shown at all.
    bool show(const QString &title, const QString &body, const QList<Action> &actions = {},
              const std::function<void()> &onClick = {}, bool urgent = false);

private Q_SLOTS:
    void onActionInvoked(uint id, const QString &actionKey);
    void onClosed(uint id, uint reason);

private:
    struct Pending {
        QList<Action> actions;
        std::function<void()> onClick;
    };
    QHash<uint, Pending> m_pending;
    QSystemTrayIcon *m_tray = nullptr;
    bool m_connected = false;
};

}  // namespace afterline
