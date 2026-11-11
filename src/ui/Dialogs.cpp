// SPDX-License-Identifier: MIT
#include "ui/Dialogs.h"

#include "app/MeetingStore.h"

#include "ui/Icons.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <QButtonGroup>
#include <QDialog>
#include <QGridLayout>
#include <QToolButton>
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
             const QString &confirmText, bool allowEmpty)
{
    Card d(parent, title, text, Tone::Neutral, allowEmpty ? "tag" : "edit");
    auto *edit = new QLineEdit(*value);
    edit->setPlaceholderText(placeholder);
    d.body()->addWidget(edit);
    d.addButton(QObject::tr("Cancel"), nullptr, false);
    auto *ok = d.addButton(confirmText.isEmpty() ? QObject::tr("Save") : confirmText, "Primary", true);
    ok->setDefault(true);
    QObject::connect(edit, &QLineEdit::textChanged, ok,
                     [ok, allowEmpty](const QString &t) { ok->setEnabled(allowEmpty || !t.trimmed().isEmpty()); });
    ok->setEnabled(allowEmpty || !value->trimmed().isEmpty());
    edit->setFocus();
    edit->selectAll();
    if (d.run() != QDialog::Accepted)
        return false;
    *value = edit->text().trimmed();
    return true;
}

const QStringList &folderIcons()
{
    static const QStringList icons = {"folder", "briefcase", "people",  "star",  "heart",  "book",
                                      "rocket", "home",      "code",    "chart", "bulb",   "calendar",
                                      "flag",   "school",    "target",  "coffee", "globe", "ask"};
    return icons;
}

const QStringList &folderColors()
{
    static const QStringList colors = {"#B4553A", "#C2841F", "#6B8E3A", "#2F8F83",
                                       "#3F72AF", "#6A5ACD", "#A0527F", "#6B7280"};
    return colors;
}

QColor folderColor(const QString &stored)
{
    QColor c(stored.isEmpty() ? folderColors().first() : stored);
    if (!c.isValid())
        c = Theme::color(Token::Accent);
    return Theme::isDark() ? c.lighter(135) : c;
}

bool editFolder(QWidget *parent, FolderInfo *folder, bool isNew, const QStringList &taken)
{
    Card d(parent, isNew ? QObject::tr("New folder") : QObject::tr("Edit folder"),
           QObject::tr("Pick a name, an icon and a colour. AfterLine can file new meetings into your folders for you."),
           Tone::Neutral, "folder");
    auto *name = new QLineEdit(folder->name);
    name->setPlaceholderText(QObject::tr("For example: Work, Clients, School"));
    d.body()->addWidget(name);
    d.body()->addSpacing(6);
    d.body()->addWidget(label(QObject::tr("ICON"), "Eyebrow"));

    QString chosenIcon = folder->icon.isEmpty() ? "folder" : folder->icon;
    QString chosenColor = folder->color.isEmpty() ? folderColors().first() : folder->color;
    auto *iconGrid = new QGridLayout;
    iconGrid->setSpacing(6);
    auto *icons = new QButtonGroup(&d);
    QVector<QToolButton *> iconButtons;
    auto paintIcons = [&] {
        for (auto *b : iconButtons) {
            const bool on = b->property("iconName").toString() == chosenIcon;
            const QColor c = folderColor(chosenColor);
            b->setIcon(icon(b->property("iconName").toString(), on ? c : Theme::color(Token::Muted)));
            b->setChecked(on);
        }
    };
    int i = 0;
    for (const QString &ic : folderIcons()) {
        auto *b = new QToolButton;
        b->setObjectName("IconChoice");
        b->setProperty("iconName", ic);
        b->setCheckable(true);
        b->setIconSize({20, 20});
        b->setFixedSize(40, 40);
        b->setCursor(Qt::PointingHandCursor);
        b->setAccessibleName(ic);
        icons->addButton(b);
        iconButtons << b;
        QObject::connect(b, &QToolButton::clicked, &d, [&, ic] {
            chosenIcon = ic;
            paintIcons();
        });
        iconGrid->addWidget(b, i / 9, i % 9);
        ++i;
    }
    d.body()->addLayout(iconGrid);
    d.body()->addSpacing(6);
    d.body()->addWidget(label(QObject::tr("COLOUR"), "Eyebrow"));
    auto *colorRow = new QHBoxLayout;
    colorRow->setSpacing(8);
    QVector<QToolButton *> swatches;
    auto paintSwatches = [&] {
        for (auto *b : swatches) {
            const QString c = b->property("color").toString();
            const bool on = c == chosenColor;
            b->setStyleSheet(QString("QToolButton { background: %1; border-radius: 14px; border: %2; min-height: 28px; max-height: 28px; min-width: 28px; max-width: 28px; padding: 0; }")
                                 .arg(folderColor(c).name(),
                                      on ? QString("3px solid %1").arg(Theme::hex(Token::Text)) : QString("0")));
        }
    };
    for (const QString &c : folderColors()) {
        auto *b = new QToolButton;
        b->setProperty("color", c);
        b->setFixedSize(28, 28);
        b->setCursor(Qt::PointingHandCursor);
        b->setAccessibleName(QObject::tr("Colour %1").arg(c));
        swatches << b;
        QObject::connect(b, &QToolButton::clicked, &d, [&, c] {
            chosenColor = c;
            paintSwatches();
            paintIcons();
        });
        colorRow->addWidget(b);
    }
    colorRow->addStretch();
    d.body()->addLayout(colorRow);
    auto *error = label({}, "ErrorText");
    error->hide();
    d.body()->addWidget(error);
    paintIcons();
    paintSwatches();

    d.addButton(QObject::tr("Cancel"), nullptr, false);
    auto *ok = d.addButton(isNew ? QObject::tr("Create folder") : QObject::tr("Save"), "Primary", true);
    ok->setDefault(true);
    const QString original = folder->name;
    auto validate = [&] {
        const QString n = name->text().trimmed();
        bool clash = false;
        for (const auto &t : taken)
            clash |= t.compare(n, Qt::CaseInsensitive) == 0 && t.compare(original, Qt::CaseInsensitive) != 0;
        error->setText(clash ? QObject::tr("There is already a folder with this name.") : QString());
        error->setVisible(clash);
        ok->setEnabled(!n.isEmpty() && !clash);
    };
    QObject::connect(name, &QLineEdit::textChanged, &d, validate);
    validate();
    name->setFocus();
    name->selectAll();
    if (d.run() != QDialog::Accepted)
        return false;
    folder->name = name->text().trimmed();
    folder->icon = chosenIcon;
    folder->color = chosenColor;
    return true;
}

}  // namespace afterline::ui
