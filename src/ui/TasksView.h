// SPDX-License-Identifier: MIT
//
// My tasks: every action item assigned to you, from all meetings' notes, as
// one list you can tick off. Each task opens the moment it was said.
#pragma once

#include "app/Application.h"

#include <QWidget>

class QCheckBox;
class QLabel;
class QVBoxLayout;

namespace afterline::ui {

class TasksView : public QWidget {
    Q_OBJECT
public:
    explicit TasksView(AppContext ctx, QWidget *parent = nullptr);
    void reload();

Q_SIGNALS:
    void openMeeting(const QString &meetingId, double seconds);
    void changed();  // a task was ticked off

private:
    AppContext m_ctx;
    QVBoxLayout *m_list;
    QLabel *m_summary;
    QCheckBox *m_everyone;
};

}  // namespace afterline::ui
