// SPDX-License-Identifier: MIT
#pragma once

#include "app/Application.h"

#include <QJsonObject>
#include <QTimer>
#include <QWidget>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QVBoxLayout;

namespace afterline::ui {

// Ask AfterLine: a conversation about one meeting or the whole library.
// Your questions are on the right, answers on the left with the moments
// they come from.
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
    struct Source {
        QString meetingId, title;
        double start;
    };
    void ask(const QString &question);
    void answerTasks(const QString &question, const QStringList &scope);
    void onAnswer(const QString &requestId, const QJsonObject &answer, const QString &error);
    QWidget *addBubble(bool mine, const QString &html, const QVector<Source> &sources = {});
    void showSuggestions(bool show);
    void scrollToEnd();

    AppContext m_ctx;
    QString m_meetingId;
    QString m_pending;
    QComboBox *m_scope;
    QScrollArea *m_scroll;
    QVBoxLayout *m_thread;
    QWidget *m_suggestions;
    QWidget *m_thinking = nullptr;
    QLabel *m_thinkingLabel = nullptr;
    QTimer m_dots;
    int m_dotCount = 0;
    QLineEdit *m_input;
    QPushButton *m_send;
};

}  // namespace afterline::ui
