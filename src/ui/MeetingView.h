// SPDX-License-Identifier: MIT
#pragma once

#include "app/Application.h"
#include "app/MeetingStore.h"
#include "app/Processing.h"

#include <QTimer>
#include <QWidget>

#include <memory>

class QLabel;
class QMenu;
class QLineEdit;
class QProgressBar;
class QPushButton;
class QSplitter;
class QTabWidget;

namespace afterline::audio {
class Player;
}

namespace afterline::ui {

class AskPanel;
class AudioView;
class NotesView;
class TranscriptView;
class Waveform;

class MeetingView : public QWidget {
    Q_OBJECT
public:
    explicit MeetingView(AppContext ctx, QWidget *parent = nullptr);
    ~MeetingView() override;
    void open(const QString &meetingId, double seekTo = -1);
    QString meetingId() const { return m_meeting.id; }
    void reload();  // after processing changed files
    void togglePlay();
    void showTab(int index);
    void exportMeeting();
    void deleteMeeting();
    void toggleAsk();
    void findInTranscript();
    void closeMeeting();

Q_SIGNALS:
    void deleted(const QString &meetingId);
    void changed(const QString &meetingId);  // title, tags...
    void openMeeting(const QString &meetingId, double seconds);
    void showSettings();

private:
    void seek(double seconds, bool play = true);
    void updateBanner();
    void updatePlayer();
    void startTranscription();
    void startNotes();
    void editTags();
    void fillFolderMenu(QMenu *menu);
    void updateFolderNote();
    void onProgress(const QString &id, Processing::Kind kind, double fraction, const QString &stage);

    AppContext m_ctx;
    MeetingInfo m_meeting;
    std::unique_ptr<audio::Player> m_player;
    QLineEdit *m_title;
    QLabel *m_meta;
    QPushButton *m_folderChip;
    QWidget *m_folderNote;
    QLabel *m_folderNoteText;
    QPushButton *m_folderNoteYes, *m_folderNoteNo;
    QWidget *m_banner;
    QLabel *m_bannerText;
    QProgressBar *m_bannerProgress;
    QPushButton *m_bannerAction, *m_bannerCancel, *m_bannerReport;
    QTabWidget *m_tabs;
    NotesView *m_notes;
    TranscriptView *m_transcript;
    AudioView *m_audio;
    AskPanel *m_ask;
    QSplitter *m_split;
    QWidget *m_playerBar;
    QPushButton *m_play;
    QLabel *m_time;
    Waveform *m_mini;
    QTimer m_tick;
};

}  // namespace afterline::ui
