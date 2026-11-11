// SPDX-License-Identifier: MIT
#pragma once

#include "core/Transcript.h"

#include <QAbstractListModel>
#include <QWidget>

class QLabel;
class QLineEdit;
class QListView;
class QPushButton;

namespace afterline::ui {

class TranscriptModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Role { StartRole = Qt::UserRole + 1, SourceRole, ConfidenceRole, EditedRole, CurrentRole, MatchRole };
    using QAbstractListModel::QAbstractListModel;
    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    bool setData(const QModelIndex &index, const QVariant &value, int role) override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;
    void setTranscript(const Transcript &t);
    const Transcript &transcript() const { return m_t; }
    void setCurrent(int row);
    void setQuery(const QString &q);
    int current() const { return m_current; }

Q_SIGNALS:
    void edited();

private:
    Transcript m_t;
    int m_current = -1;
    QString m_query;
};

class TranscriptView : public QWidget {
    Q_OBJECT
public:
    explicit TranscriptView(QWidget *parent = nullptr);
    // preliminary: the live transcript made while recording, shown read-only
    // until the full transcript is ready.
    void load(const QString &path, bool preliminary = false);
    void setPlaybackPosition(double seconds);
    void revealTime(double seconds);
    void focusFind();
    bool isEmpty() const { return m_model->transcript().isEmpty(); }
    const Transcript &transcript() const { return m_model->transcript(); }

Q_SIGNALS:
    void seekRequested(double seconds);
    void changed();

private:
    void findNext(int direction);
    QString m_path;
    TranscriptModel *m_model;
    QListView *m_list;
    QLineEdit *m_find;
    QLabel *m_findCount;
    QLabel *m_info;
    bool m_follow = true;
};

}  // namespace afterline::ui
