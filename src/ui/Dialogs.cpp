// SPDX-License-Identifier: MIT
#include "ui/Dialogs.h"

#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QDialog>
#include <QEvent>
#include <QGraphicsDropShadowEffect>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QGuiApplication>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

namespace afterline::ui {

namespace {

// Dims the window behind a dialog while it is open.
class Scrim : public QWidget {
public:
    explicit Scrim(QWidget *window) : QWidget(window)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, false);
        setGeometry(window->rect());
        window->installEventFilter(this);
        show();
        raise();
    }
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (o == parent() && e->type() == QEvent::Resize)
            setGeometry(static_cast<QWidget *>(o)->rect());
        return false;
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.fillRect(rect(), QColor(0, 0, 0, Theme::isDark() ? 110 : 70));
    }
};

QColor toneColor(Tone tone)
{
    switch (tone) {
    case Tone::Danger: return Theme::color(Token::Danger);
    case Tone::Warning: return Theme::color(Token::Warning);
    case Tone::Info:
    case Tone::Neutral: break;
    }
    return Theme::color(Token::Accent);
}

QString defaultIcon(Tone tone)
{
    switch (tone) {
    case Tone::Danger: return "trash";
    case Tone::Warning: return "alert";
    case Tone::Info: return "check";
    case Tone::Neutral: break;
    }
    return "ask";
}

class Card : public QDialog {
public:
    Card(QWidget *parent, const QString &title, const QString &text, Tone tone, const QString &iconName)
        : QDialog(parent ? parent->window() : nullptr, Qt::Dialog | Qt::FramelessWindowHint)
    {
        // Wayland always composites: a translucent window with a soft shadow.
        // On X11 a compositor may be missing, so the window is shaped instead.
        m_translucent = QGuiApplication::platformName().startsWith("wayland");
        if (m_translucent)
            setAttribute(Qt::WA_TranslucentBackground);
        setWindowTitle(title);
        setModal(true);
        auto *outer = new QVBoxLayout(this);
        outer->setContentsMargins(m_translucent ? QMargins(24, 24, 24, 30) : QMargins());
        auto *card = new QWidget;
        card->setObjectName("DialogCard");
        card->setAttribute(Qt::WA_StyledBackground);
        if (m_translucent) {
            auto *shadow = new QGraphicsDropShadowEffect(card);
            shadow->setBlurRadius(40);
            shadow->setOffset(0, 10);
            shadow->setColor(QColor(0, 0, 0, Theme::isDark() ? 140 : 60));
            card->setGraphicsEffect(shadow);
        }
        outer->addWidget(card);
        auto *v = new QVBoxLayout(card);
        v->setContentsMargins(24, 22, 24, 20);
        v->setSpacing(10);

        const QColor c = toneColor(tone);
        auto *tile = new QLabel;
        tile->setFixedSize(42, 42);
        tile->setAlignment(Qt::AlignCenter);
        tile->setPixmap(icon(iconName.isEmpty() ? defaultIcon(tone) : iconName, c).pixmap(22, 22));
        QColor soft = c;
        soft.setAlpha(Theme::isDark() ? 55 : 32);
        tile->setStyleSheet(QString("background: rgba(%1,%2,%3,%4); border-radius: 12px;")
                                .arg(soft.red()).arg(soft.green()).arg(soft.blue()).arg(soft.alpha()));
        v->addWidget(tile);
        v->addSpacing(2);
        auto *t = label(title, "DialogTitle");
        t->setTextInteractionFlags(Qt::NoTextInteraction);
        v->addWidget(t);
        if (!text.isEmpty()) {
            m_text = label(text, "Muted");
            m_text->setMinimumWidth(360);
            v->addWidget(m_text);
        }
        m_body = new QVBoxLayout;
        m_body->setContentsMargins(0, 4, 0, 0);
        v->addLayout(m_body);
        v->addSpacing(10);
        m_buttons = new QHBoxLayout;
        m_buttons->setSpacing(8);
        m_buttons->addStretch();
        v->addLayout(m_buttons);
        setMaximumWidth(520);
    }

    QPushButton *addButton(const QString &text, const char *objectName, bool accept)
    {
        auto *b = new QPushButton(text);
        if (objectName)
            b->setObjectName(objectName);
        b->setCursor(Qt::PointingHandCursor);
        b->setMinimumWidth(96);
        connect(b, &QPushButton::clicked, this, accept ? &QDialog::accept : &QDialog::reject);
        m_buttons->addWidget(b);
        return b;
    }
    QVBoxLayout *body() { return m_body; }

    int run()
    {
        QPointer<Scrim> scrim;
        if (parentWidget())
            scrim = new Scrim(parentWidget()->window());
        adjustSize();
        if (!m_translucent) {
            QPainterPath path;
            path.addRoundedRect(QRectF(rect()), 16, 16);
            setMask(QRegion(path.toFillPolygon().toPolygon()));
        }
        if (QWidget *w = parentWidget()) {
            const QRect r = w->window()->frameGeometry();
            move(r.center() - rect().center());
        }
        const int result = exec();
        delete scrim;
        return result;
    }

private:
    bool m_translucent = false;
    QLabel *m_text = nullptr;
    QVBoxLayout *m_body;
    QHBoxLayout *m_buttons;
};

}  // namespace

bool confirm(QWidget *parent, const QString &title, const QString &text, const QString &confirmText, Tone tone,
             const QString &iconName, const QString &cancelText)
{
    Card d(parent, title, text, tone, iconName);
    auto *cancel = d.addButton(cancelText.isEmpty() ? QObject::tr("Cancel") : cancelText, nullptr, false);
    auto *ok = d.addButton(confirmText, tone == Tone::Danger ? "DangerButton" : "Primary", true);
    // A destructive choice is never the default: Enter cancels.
    (tone == Tone::Danger ? cancel : ok)->setDefault(true);
    (tone == Tone::Danger ? cancel : ok)->setFocus();
    return d.run() == QDialog::Accepted;
}

void inform(QWidget *parent, const QString &title, const QString &text, Tone tone, const QString &iconName)
{
    Card d(parent, title, text, tone, iconName);
    auto *ok = d.addButton(QObject::tr("OK"), "Primary", true);
    ok->setDefault(true);
    ok->setFocus();
    d.run();
}

bool askText(QWidget *parent, const QString &title, const QString &text, QString *value, const QString &placeholder,
             const QString &confirmText)
{
    Card d(parent, title, text, Tone::Neutral, "edit");
    auto *edit = new QLineEdit(*value);
    edit->setPlaceholderText(placeholder);
    d.body()->addWidget(edit);
    d.addButton(QObject::tr("Cancel"), nullptr, false);
    auto *ok = d.addButton(confirmText.isEmpty() ? QObject::tr("Save") : confirmText, "Primary", true);
    ok->setDefault(true);
    QObject::connect(edit, &QLineEdit::textChanged, ok, [ok](const QString &t) { ok->setEnabled(!t.trimmed().isEmpty()); });
    ok->setEnabled(!value->trimmed().isEmpty());
    edit->setFocus();
    edit->selectAll();
    if (d.run() != QDialog::Accepted)
        return false;
    *value = edit->text().trimmed();
    return true;
}

}  // namespace afterline::ui
