// SPDX-License-Identifier: MIT
#pragma once

#include "app/Application.h"

#include <QJsonObject>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QTextBrowser;

namespace afterline::ui {

class AskPanel : public QWidget {
    Q_OBJECT
public:
    explicit AskPanel(AppContext ctx, QWidget *parent = nullptr);
    void setMeeting(const QString &meetingId, const QString &title);
    void focusInput();

Q_SIGNALS:
    void openMeeting(const QString &meetingId, double seconds);
    void closeRequested();

private:
    void ask();
    void onAnswer(const QString &requestId, const QJsonObject &answer, const QString &error);
    void append(const QString &html);
    AppContext m_ctx;
    QString m_meetingId;
    QString m_pending;
    QComboBox *m_scope;
    QTextBrowser *m_log;
    QLineEdit *m_input;
    QPushButton *m_send;
    QLabel *m_busy;
    QString m_html;
};

}  // namespace afterline::ui
