// SPDX-License-Identifier: MIT
#include "app/DesktopNotifier.h"

#ifdef AFTERLINE_HAVE_DBUS
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusReply>
#endif
#include <QIcon>
#include <QSystemTrayIcon>
#include <QVariantMap>

namespace afterline {

#ifdef AFTERLINE_HAVE_DBUS
namespace {
const char *kService = "org.freedesktop.Notifications";
const char *kPath = "/org/freedesktop/Notifications";
}  // namespace
#endif

DesktopNotifier::DesktopNotifier(QObject *parent) : QObject(parent)
{
#ifdef AFTERLINE_HAVE_DBUS
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.isConnected())
        return;
    m_connected = bus.connect(kService, kPath, kService, "ActionInvoked", this,
                              SLOT(onActionInvoked(uint, QString))) &&
                  bus.connect(kService, kPath, kService, "NotificationClosed", this, SLOT(onClosed(uint, uint)));
#endif
}

bool DesktopNotifier::show(const QString &title, const QString &body, const QList<Action> &actions,
                           const std::function<void()> &onClick, bool urgent)
{
#ifdef AFTERLINE_HAVE_DBUS
    if (m_connected) {
        QDBusInterface iface(kService, kPath, kService, QDBusConnection::sessionBus());
        QStringList actionList;
        // "default" is what clicking the notification itself does.
        if (onClick)
            actionList << "default" << tr("Open");
        for (const auto &a : actions)
            actionList << a.key << a.label;
        QVariantMap hints{{"desktop-entry", "io.github.AfterLine"}, {"urgency", QVariant::fromValue<uchar>(urgent ? 2 : 1)}};
        if (!actions.isEmpty())
            hints.insert("resident", false);
        const QDBusReply<uint> reply =
            iface.call("Notify", QString("AfterLine"), uint(0), QString("io.github.AfterLine"), title, body, actionList,
                       hints, int(actions.isEmpty() ? 8000 : 60000));
        if (reply.isValid()) {
            if (onClick || !actions.isEmpty())
                m_pending.insert(reply.value(), Pending{actions, onClick});
            return true;
        }
    }
#endif
    // No notification service: the tray balloon, without buttons.
    if (m_tray && QSystemTrayIcon::supportsMessages()) {
        m_tray->showMessage(title, body, QIcon(":/icons/afterline.svg"), 8000);
        if (onClick || !actions.isEmpty()) {
            const auto run = onClick ? onClick : actions.first().run;
            QObject *ctx = new QObject(this);
            connect(m_tray, &QSystemTrayIcon::messageClicked, ctx, [run, ctx] {
                run();
                ctx->deleteLater();
            });
        }
        return true;
    }
    return false;
}

void DesktopNotifier::onActionInvoked(uint id, const QString &actionKey)
{
    const auto it = m_pending.find(id);
    if (it == m_pending.end())
        return;
    const Pending p = it.value();
    m_pending.erase(it);
    if (actionKey == "default") {
        if (p.onClick)
            p.onClick();
        return;
    }
    for (const auto &a : p.actions)
        if (a.key == actionKey && a.run) {
            a.run();
            return;
        }
}

void DesktopNotifier::onClosed(uint id, uint) { m_pending.remove(id); }

}  // namespace afterline
