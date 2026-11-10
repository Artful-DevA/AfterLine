// SPDX-License-Identifier: MIT
#include "app/Application.h"

#include "app/MeetingStore.h"
#include "app/ModelManager.h"
#include "app/Processing.h"
#include "app/RecordingController.h"
#include "app/Settings.h"
#include "core/Paths.h"
#include "core/Transcript.h"
#include "ui/Icons.h"
#include "ui/MainWindow.h"
#include "ui/SetupDialog.h"
#include "ui/Theme.h"

#include <QApplication>
#include <QLocalServer>
#include <QLocalSocket>
#include <QMenu>
#include <QMessageBox>
#include <QSystemTrayIcon>
#include <QTimer>

#include <unistd.h>

namespace afterline {

Application::Application() = default;

Application::~Application()
{
    delete m_window;
    delete m_ctx.recording;  // stops and saves any recording
    delete m_ctx.processing;
    delete m_ctx.models;
    delete m_ctx.store;
    delete m_ctx.settings;
}

QString Application::socketName() { return QString("afterline-%1").arg(::getuid()); }

int Application::run(bool startHidden)
{
    // One AfterLine per user: a second launch brings the first to the front
    // (two recorders fighting over the same library would be a mess).
    {
        QLocalSocket probe;
        probe.connectToServer(socketName());
        if (probe.waitForConnected(500)) {
            probe.write("show\n");
            probe.waitForBytesWritten(500);
            return 0;
        }
    }
    QLocalServer::removeServer(socketName());
    m_server = new QLocalServer(this);
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    m_server->listen(socketName());
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *s = m_server->nextPendingConnection()) {
            connect(s, &QLocalSocket::readyRead, s, [this, s] {
                s->readAll();
                if (m_window)
                    m_window->bringToFront();
                s->deleteLater();
            });
        }
    });

    m_ctx.settings = new Settings;
    ui::Theme::apply(*qApp, m_ctx.settings->theme());

    m_ctx.store = new MeetingStore;
    QString error;
    if (!m_ctx.store->open(meetingsDir(), databasePath(), &error)) {
        QMessageBox::critical(nullptr, tr("AfterLine cannot start"),
                              tr("The meeting library in %1 could not be opened: %2").arg(meetingsDir(), error));
        return 1;
    }
    // Recordings cut short by a crash or power loss: their audio was written
    // continuously, so it is kept and the meeting marked as interrupted.
    const QStringList recovered = m_ctx.store->recoverInterrupted();

    m_ctx.models = new ModelManager;
    m_ctx.processing = new Processing(*m_ctx.store, *m_ctx.settings);
    m_ctx.recording = new RecordingController(*m_ctx.store);

    connect(m_ctx.recording, &RecordingController::recordingStarted, this, [this] {
        m_ctx.processing->setRecordingActive(true);
        updateTray();
    });
    connect(m_ctx.recording, &RecordingController::recordingStopped, this, [this](const QString &id) {
        m_ctx.processing->setRecordingActive(false);
        updateTray();
        if (m_ctx.settings->autoTranscribe())
            m_ctx.processing->transcribe(id);
        if (!m_window || !m_window->isVisible())
            notify(tr("Recording saved"), m_ctx.settings->autoTranscribe() ? tr("Transcription has started.") : QString());
    });
    connect(m_ctx.recording, &RecordingController::pausedChanged, this, [this] { updateTray(); });
    connect(m_ctx.recording, &RecordingController::warning, this, [this](const QString &title, const QString &msg) {
        if (!m_window || !m_window->isActiveWindow())
            notify(title, msg);
    });
    connect(m_ctx.processing, &Processing::finished, this,
            [this](const QString &id, Processing::Kind kind, bool ok, const QString &err) {
                if (m_window && m_window->isActiveWindow())
                    return;
                const QString title = m_ctx.store->get(id).title;
                if (!ok && err != tr("Cancelled."))
                    notify(kind == Processing::Kind::Transcript ? tr("Transcription failed") : tr("Notes failed"),
                           title + ": " + err);
                else if (ok)
                    notify(kind == Processing::Kind::Transcript ? tr("Transcript ready") : tr("Notes ready"), title);
            });

    m_window = new ui::MainWindow(m_ctx);
    connect(m_window, &ui::MainWindow::themeChanged, this, [this] {
        ui::Theme::apply(*qApp, m_ctx.settings->theme());
        // Rebuild the window so custom-painted parts pick up the palette.
        const QByteArray geometry = m_window->saveGeometry();
        m_window->deleteLater();
        m_window = new ui::MainWindow(m_ctx);
        m_window->restoreGeometry(geometry);
        connect(m_window, &ui::MainWindow::hiddenToTray, this, [this] {
            if (m_ctx.recording->recording())
                notify(tr("Still recording"), tr("AfterLine keeps recording in the background."));
        });
        m_window->show();
        m_window->showSettings();
        updateTray();
    });
    connect(m_window, &ui::MainWindow::hiddenToTray, this, [this] {
        if (m_ctx.recording->recording())
            notify(tr("Still recording"), tr("AfterLine keeps recording in the background. Use the tray icon to "
                                             "stop or return."));
    });
    setupTray();
    if (!startHidden || !m_tray)
        m_window->show();

    if (!recovered.isEmpty()) {
        QTimer::singleShot(300, m_window, [this, recovered] {
            m_window->showMeeting(recovered.first());
            QMessageBox::information(
                m_window, tr("A recording was recovered"),
                tr("AfterLine was closed unexpectedly while recording. The audio recorded until then was saved and "
                   "is available in your library (%n meeting(s)).",
                   nullptr, int(recovered.size())));
            if (m_ctx.settings->autoTranscribe())
                for (const auto &id : recovered)
                    m_ctx.processing->transcribe(id);
        });
    }
    m_ctx.processing->resumeInterrupted();

    if (!m_ctx.settings->setupDone())
        QTimer::singleShot(200, m_window, [this] {
            ui::SetupDialog dlg(m_ctx, m_window);
            dlg.exec();
            m_ctx.settings->setSetupDone(true);
        });
    if (!m_ctx.recording->audioAvailable())
        QTimer::singleShot(400, m_window, [this] {
            QMessageBox::warning(m_window, tr("Audio service not available"),
                                 tr("AfterLine could not connect to PipeWire, the system's audio service, so it cannot "
                                    "record. Your existing meetings are available.\n\n%1")
                                     .arg(m_ctx.recording->audioError()));
        });

    connect(qApp, &QApplication::aboutToQuit, this, [this] {
        if (m_ctx.recording->recording())
            m_ctx.recording->stop();
    });
    return qApp->exec();
}

void Application::setupTray()
{
    if (!QSystemTrayIcon::isSystemTrayAvailable())
        return;
    m_tray = new QSystemTrayIcon(QIcon(":/icons/afterline.svg"), this);
    m_tray->setToolTip("AfterLine");
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger && m_window)
            m_window->bringToFront();
    });
    updateTray();
    m_tray->show();
}

void Application::updateTray()
{
    if (!m_tray)
        return;
    auto *old = m_tray->contextMenu();
    auto *menu = new QMenu;
    menu->addAction(tr("Show AfterLine"), this, [this] { m_window->bringToFront(); });
    menu->addSeparator();
    if (m_ctx.recording->recording()) {
        const bool paused = m_ctx.recording->paused();
        menu->addAction(ui::icon(paused ? "record" : "pause"), paused ? tr("Resume recording") : tr("Pause recording"),
                        this, [this, paused] { paused ? m_ctx.recording->resume() : m_ctx.recording->pause(); });
        menu->addAction(ui::icon("stop"), tr("Stop and save"), this, [this] { m_ctx.recording->stop(); });
        m_tray->setToolTip(paused ? tr("AfterLine: recording paused") : tr("AfterLine: recording"));
        m_tray->setIcon(ui::icon("record", paused ? ui::Theme::color(ui::Token::Muted) : ui::Theme::color(ui::Token::Recording)));
    } else {
        menu->addAction(ui::icon("record"), tr("New recording"), this, [this] {
            m_window->bringToFront();
            m_window->showRecord();
        });
        m_tray->setToolTip("AfterLine");
        m_tray->setIcon(QIcon(":/icons/afterline.svg"));
    }
    menu->addSeparator();
    menu->addAction(tr("Quit"), this, [this] {
        if (m_window->confirmQuit())
            qApp->quit();
    });
    m_tray->setContextMenu(menu);
    delete old;
}

void Application::notify(const QString &title, const QString &message)
{
    if (m_tray && QSystemTrayIcon::supportsMessages())
        m_tray->showMessage(title, message, QIcon(":/icons/afterline.svg"), 6000);
}

}  // namespace afterline
