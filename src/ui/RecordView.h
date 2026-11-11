// SPDX-License-Identifier: MIT
#pragma once

#include "app/Application.h"
#include "app/RecordingController.h"

#include <QTimer>
#include <QWidget>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QTextBrowser;

namespace afterline::ui {

class LevelMeter;
class RecordingDot;

class RecordView : public QWidget {
    Q_OBJECT
public:
    explicit RecordView(AppContext ctx, QWidget *parent = nullptr);
    void startOrStop();  // Ctrl+R
    void togglePause();
    void activate();     // page shown
    void deactivate();   // page hidden

Q_SIGNALS:
    void stopped(const QString &meetingId);

private:
    void reloadSources();
    void updateDetail();
    void updatePreview();
    void start();
    void showRecording(bool recording);
    void tick();
    void showWarning(const QString &title, const QString &message);
    QWidget *buildIdle();
    QWidget *buildRecording();
    void setTesting(bool on);
    void renderLive();
    void updateLiveState();

    AppContext m_ctx;
    QStackedWidget *m_stack;
    // Idle
    QComboBox *m_source;
    QLabel *m_sourceDetail;
    QCheckBox *m_includeMic;
    QComboBox *m_mic;
    LevelMeter *m_previewMeter;
    QPushButton *m_test;
    QCheckBox *m_liveToggle;
    QTimer m_testTimeout;
    bool m_testing = false;
    QLabel *m_audioProblem;
    QPushButton *m_start;
    // Recording
    RecordingDot *m_dot;
    QLabel *m_state;
    QLineEdit *m_title;
    QLabel *m_timer;
    QLabel *m_computerLabel, *m_computerState, *m_micLabel, *m_micState;
    QWidget *m_micRow;
    LevelMeter *m_computerMeter, *m_micMeter;
    QWidget *m_warning;
    QLabel *m_warningTitle, *m_warningText;
    QPushButton *m_pause, *m_stop;
    // Live transcript
    QTextBrowser *m_live;
    QLabel *m_liveStatus;
    QString m_partial, m_partialSource;
    QTimer m_timerTick;
    bool m_active = false;
};

}  // namespace afterline::ui
