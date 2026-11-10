// SPDX-License-Identifier: MIT
#pragma once

#include "app/Application.h"

#include <QDialog>

class QStackedWidget;
class QPushButton;
class QLabel;

namespace afterline::ui {

// First run: what AfterLine does, the meeting language, and the offline
// models (with the recommended choice one click away). Downloads continue in
// the background; recording works immediately.
class SetupDialog : public QDialog {
    Q_OBJECT
public:
    explicit SetupDialog(AppContext ctx, QWidget *parent = nullptr);

private:
    void go(int page);
    AppContext m_ctx;
    QStackedWidget *m_pages;
    QPushButton *m_back, *m_next;
    QLabel *m_step;
};

}  // namespace afterline::ui
