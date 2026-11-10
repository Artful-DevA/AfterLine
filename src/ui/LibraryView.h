// SPDX-License-Identifier: MIT
//
// The sidebar: brand, the record button, search, and the meeting library
// grouped by date. While searching, the list shows matching transcript lines
// and notes across all meetings.
#pragma once

#include "app/Application.h"
#include "app/MeetingStore.h"

#include <QAbstractListModel>
#include <QWidget>

class QComboBox;
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

Q_SIGNALS:
    void openMeeting(const QString &meetingId, double seekTo);
    void recordClicked();
    void settingsClicked();

private:
    void reloadFilters();
    AppContext m_ctx;
    LibraryModel *m_model;
    QListView *m_list;
    QLineEdit *m_search;
    QComboBox *m_filter;
    QPushButton *m_record;
    QWidget *m_download;
    QLabel *m_downloadLabel;
    QProgressBar *m_downloadBar;
    QString m_selected;
};

}  // namespace afterline::ui
