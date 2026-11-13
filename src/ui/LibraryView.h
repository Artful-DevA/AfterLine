// SPDX-License-Identifier: MIT
//
// The sidebar: brand, the record button, search, and the meeting library
// grouped by date. While searching, the list shows matching transcript lines
// and notes across all meetings.
#pragma once

#include "app/Application.h"
#include "app/MeetingStore.h"

#include <QAbstractButton>
#include <QAbstractListModel>
#include <QWidget>

class QButtonGroup;
class QComboBox;
class QScrollArea;
class QVBoxLayout;
class QLabel;
class QLineEdit;
class QListView;
class QProgressBar;
class QPushButton;

namespace afterline::ui {

class LibraryModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { KindRole = Qt::UserRole + 1, MeetingIdRole, SubtitleRole, StatusRole, StartRole, SnippetRole };
    enum Kind { Header, Meeting, Hit };
    explicit LibraryModel(AppContext ctx, QObject *parent = nullptr);
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    QStringList mimeTypes() const override;
    QMimeData *mimeData(const QModelIndexList &indexes) const override;
    void reload(const QString &filter);
    void showSearch(const QString &query);
    int rowOf(const QString &meetingId) const;
    void setLiveStatus(const QString &meetingId, const QString &status);

private:
    struct Row {
        Kind kind;
        QString id, title, subtitle, status, snippet;
        double start = -1;
    };
    QString statusOf(const MeetingInfo &m) const;
    AppContext m_ctx;
    QVector<Row> m_rows;
    QHash<QString, QString> m_live;
};

// A row of the sidebar's navigation: All meetings, My tasks, a folder or a
// tag. Folder rows accept meetings dragged onto them.
class NavButton : public QAbstractButton {
    Q_OBJECT
public:
    NavButton(const QString &key, const QString &text, const QString &iconName, const QColor &tint,
              QWidget *parent = nullptr);
    QString key() const { return m_key; }
    void setCount(const QString &count) { m_count = count; update(); }
    void setAcceptsMeetings(bool on) { setAcceptDrops(on); }
    QSize sizeHint() const override { return {200, 34}; }

Q_SIGNALS:
    void meetingDropped(const QString &meetingId);
    void contextMenuWanted(const QPoint &globalPos);

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override { m_hover = true; update(); }
    void leaveEvent(QEvent *) override { m_hover = false; update(); }
    void dragEnterEvent(QDragEnterEvent *e) override;
    void dragLeaveEvent(QDragLeaveEvent *) override { m_dropping = false; update(); }
    void dropEvent(QDropEvent *e) override;
    void contextMenuEvent(QContextMenuEvent *e) override;

private:
    QString m_key, m_icon, m_count;
    QColor m_tint;
    bool m_hover = false, m_dropping = false;
};

class LibraryView : public QWidget {
    Q_OBJECT
public:
    explicit LibraryView(AppContext ctx, QWidget *parent = nullptr);
    void refresh();
    void select(const QString &meetingId);
    void focusSearch();
    void setRecording(bool recording, const QString &time);
    void setLiveStatus(const QString &meetingId, const QString &status);
    void setDownloadStatus(const QString &text, int percent);  // percent < 0 hides

    // "" (all meetings), "tasks", "folder:<name>" or "tag:<name>".
    QString section() const { return m_section; }
    void showSection(const QString &key);

Q_SIGNALS:
    void openMeeting(const QString &meetingId, double seekTo);
    void recordClicked();
    void settingsClicked();
    void tasksClicked();
    void organizeRequested();  // file unfiled meetings with AI
    void foldersChanged();
    void meetingRenamed(const QString &meetingId);

private:
    void reloadFilters();
    void meetingMenu(const QPoint &pos);
    void folderMenu(const QString &name, const QPoint &globalPos);
    void newFolder(const QString &moveMeetingId = {});
    void navHeightChanged();
    void resizeEvent(QResizeEvent *e) override;
    QScrollArea *m_navScroll;
    QVBoxLayout *m_navLayout;
    QButtonGroup *m_navGroup;
    QString m_section;
    AppContext m_ctx;
    LibraryModel *m_model;
    QListView *m_list;
    QLineEdit *m_search;
    QPushButton *m_record;
    QWidget *m_download;
    QLabel *m_downloadLabel;
    QProgressBar *m_downloadBar;
    QString m_selected;
};

}  // namespace afterline::ui
