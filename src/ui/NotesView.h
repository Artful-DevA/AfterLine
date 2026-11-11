// SPDX-License-Identifier: MIT
#pragma once

#include "app/MeetingStore.h"

#include <QWidget>

class QLabel;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTextBrowser;

namespace afterline::ui {

class NotesView : public QWidget {
    Q_OBJECT
public:
    explicit NotesView(QWidget *parent = nullptr);
    // emptyMessage/actionText describe what to do when there are no notes.
    void load(const MeetingInfo &meeting, const QString &emptyMessage, const QString &actionText);
    QString markdown() const { return m_markdown; }  // as shown (edited or generated)
    bool hasNotes() const { return !m_markdown.isEmpty(); }

Q_SIGNALS:
    void seekRequested(double seconds);
    void actionRequested();  // the empty state's button
    void changed();
    void tasksChanged();

private:
    void edit();
    void save();
    void revert();
    bool eventFilter(QObject *o, QEvent *e) override;
    void toggleTask(int index);
    MeetingInfo m_meeting;
    QString m_markdown;
    QStackedWidget *m_stack;
    QTextBrowser *m_view;
    QPlainTextEdit *m_editor;
    QLabel *m_info;
    QLabel *m_emptyText;
    QPushButton *m_emptyAction;
    QPushButton *m_edit, *m_save, *m_cancel, *m_revert;
};

}  // namespace afterline::ui
