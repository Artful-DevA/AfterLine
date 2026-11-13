// SPDX-License-Identifier: MIT
#pragma once

#include <QObject>
#include <QPointer>

class QLocalServer;
class QSystemTrayIcon;

namespace afterline {

class Settings;
class MeetingStore;
class ModelManager;
class Processing;
class RecordingController;
class LiveTranscriber;
class UpdateChecker;
class DesktopNotifier;

namespace ui {
class MainWindow;
}

// Shared services, owned by Application and handed to every view.
struct AppContext {
    Settings *settings = nullptr;
    MeetingStore *store = nullptr;
    ModelManager *models = nullptr;
    Processing *processing = nullptr;
    RecordingController *recording = nullptr;
    LiveTranscriber *live = nullptr;
    UpdateChecker *updates = nullptr;
};

class Application : public QObject {
    Q_OBJECT
public:
    Application();
    ~Application() override;
    // Returns the process exit code.
    int run(bool startHidden);
    static QString socketName();

private:
    void createWindow();
    void applyTheme(bool fromSettings);
    void setupTray();
    void updateTray();
    void notify(const QString &title, const QString &message, const QString &meetingId = {});

    AppContext m_ctx;
    QLocalServer *m_server = nullptr;
    QSystemTrayIcon *m_tray = nullptr;
    DesktopNotifier *m_notifier = nullptr;
    QPointer<ui::MainWindow> m_window;
};

}  // namespace afterline
