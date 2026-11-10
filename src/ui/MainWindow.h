// SPDX-License-Identifier: MIT
#pragma once

#include "app/Application.h"

#include <QMainWindow>

class QStackedWidget;

namespace afterline::ui {

class LibraryView;
class MeetingView;
class RecordView;
class SettingsView;

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(AppContext ctx, QWidget *parent = nullptr);
    void showRecord();
    void showMeeting(const QString &id, double seekTo = -1);
    void showSettings();
    void showHome();
    void bringToFront();
    // True: really quit (the user confirmed or nothing is recording).
    bool confirmQuit();

Q_SIGNALS:
    void themeChanged();
    void hiddenToTray();

protected:
    void closeEvent(QCloseEvent *e) override;

private:
    void setupShortcuts();
    void onProcessingUpdate();
    AppContext m_ctx;
    QStackedWidget *m_pages;
    QWidget *m_home;
    LibraryView *m_library;
    RecordView *m_record;
    MeetingView *m_meeting;
    SettingsView *m_settings;
    QWidget *m_previous = nullptr;
    bool m_quitting = false;
};

}  // namespace afterline::ui
