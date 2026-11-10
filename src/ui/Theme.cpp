// SPDX-License-Identifier: MIT
#include "ui/Theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>

namespace afterline::ui {

namespace {

bool g_dark = false;
bool g_systemDarkKnown = false;
bool g_systemDark = false;

struct Palette {
    const char *background, *surface, *surfaceAlt, *border, *text, *muted, *accent, *accentHover, *accentSoft,
        *onAccent, *recording, *success, *warning, *danger, *trackComputer, *trackMicrophone;
};

constexpr Palette kLight = {"#F7F3EC", "#FFFCF7", "#EFE8DD", "#E0D6C8", "#292522", "#6F655B", "#B85B3F",
                            "#A14E36", "#F3DFD4", "#FFFFFF", "#C8372D", "#3F7A52", "#9A6512", "#B3261E",
                            "#B85B3F", "#4F6F86"};
constexpr Palette kDark = {"#1C1714", "#25201C", "#2C2520", "#3E342D", "#EFE7DD", "#B3A496", "#D9805F",
                           "#E4927A", "#4A2F25", "#1C1714", "#F0564B", "#7DBB8D", "#E0A458", "#F2827A",
                           "#D9805F", "#8DB0C9"};

const Palette &current() { return g_dark ? kDark : kLight; }

bool detectSystemDark()
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    const auto scheme = QGuiApplication::styleHints()->colorScheme();
    if (scheme != Qt::ColorScheme::Unknown)
        return scheme == Qt::ColorScheme::Dark;
#endif
    if (!g_systemDarkKnown) {
        // Before AfterLine sets its own palette, the platform palette tells
        // whether the desktop is dark.
        g_systemDark = QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
        g_systemDarkKnown = true;
    }
    return g_systemDark;
}

}  // namespace

bool Theme::isDark() { return g_dark; }

QColor Theme::color(Token t)
{
    const Palette &p = current();
    switch (t) {
    case Token::Background: return QColor(p.background);
    case Token::Surface: return QColor(p.surface);
    case Token::SurfaceAlt: return QColor(p.surfaceAlt);
    case Token::Border: return QColor(p.border);
    case Token::Text: return QColor(p.text);
    case Token::Muted: return QColor(p.muted);
    case Token::Accent: return QColor(p.accent);
    case Token::AccentHover: return QColor(p.accentHover);
    case Token::AccentSoft: return QColor(p.accentSoft);
    case Token::OnAccent: return QColor(p.onAccent);
    case Token::Recording: return QColor(p.recording);
    case Token::Success: return QColor(p.success);
    case Token::Warning: return QColor(p.warning);
    case Token::Danger: return QColor(p.danger);
    case Token::TrackComputer: return QColor(p.trackComputer);
    case Token::TrackMicrophone: return QColor(p.trackMicrophone);
    }
    return {};
}

void Theme::apply(QApplication &app, const QString &mode)
{
    const bool systemDark = detectSystemDark();
    g_dark = mode == "dark" || (mode != "light" && systemDark);
    app.setStyle(QStyleFactory::create("Fusion"));

    QPalette pal;
    pal.setColor(QPalette::Window, color(Token::Background));
    pal.setColor(QPalette::WindowText, color(Token::Text));
    pal.setColor(QPalette::Base, color(Token::Surface));
    pal.setColor(QPalette::AlternateBase, color(Token::SurfaceAlt));
    pal.setColor(QPalette::Text, color(Token::Text));
    pal.setColor(QPalette::PlaceholderText, color(Token::Muted));
    pal.setColor(QPalette::Button, color(Token::Surface));
    pal.setColor(QPalette::ButtonText, color(Token::Text));
    pal.setColor(QPalette::Highlight, color(Token::AccentSoft));
    pal.setColor(QPalette::HighlightedText, color(Token::Text));
    pal.setColor(QPalette::Link, color(Token::Accent));
    pal.setColor(QPalette::LinkVisited, color(Token::Accent));
    pal.setColor(QPalette::ToolTipBase, color(Token::Text));
    pal.setColor(QPalette::ToolTipText, color(Token::Background));
    pal.setColor(QPalette::Mid, color(Token::Border));
    pal.setColor(QPalette::Midlight, color(Token::Border));
    pal.setColor(QPalette::Dark, color(Token::Border).darker(130));
    pal.setColor(QPalette::Light, color(Token::Surface));
    for (auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        pal.setColor(QPalette::Disabled, role, color(Token::Muted));
    app.setPalette(pal);
    app.setStyleSheet(styleSheet());
}

QString Theme::styleSheet()
{
    QString css = R"(
QWidget { font-size: 10.5pt; }
QToolTip { padding: 6px 8px; border: 0; }
#Sidebar { background: @surfaceAlt; border-right: 1px solid @border; }
#Sidebar QListView { background: transparent; border: 0; outline: 0; }
#BrandName { font-size: 15pt; font-weight: 700; }
#PageTitle { font-size: 20pt; font-weight: 600; }
#SectionTitle { font-size: 12.5pt; font-weight: 600; }
#Muted, .Muted { color: @muted; }
#Card { background: @surface; border: 1px solid @border; border-radius: 12px; }
#Banner { background: @accentSoft; border-radius: 10px; }
#WarningBanner { background: @warningSoft; border: 1px solid @warning; border-radius: 10px; }
#ErrorText { color: @danger; }
QLineEdit, QPlainTextEdit, QTextEdit, QComboBox, QSpinBox {
  background: @surface; border: 1px solid @border; border-radius: 8px; padding: 7px 9px;
  selection-background-color: @accentSoft; selection-color: @text; }
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QComboBox:focus, QSpinBox:focus { border: 1px solid @accent; }
QComboBox::drop-down { border: 0; width: 26px; }
QComboBox QAbstractItemView { background: @surface; border: 1px solid @border; selection-background-color: @accentSoft;
  selection-color: @text; outline: 0; padding: 4px; }
#TitleEdit { font-size: 19pt; font-weight: 600; border: 1px solid transparent; background: transparent; padding: 2px 6px; }
#TitleEdit:hover { border: 1px solid @border; }
#TitleEdit:focus { border: 1px solid @accent; background: @surface; }
QPushButton, QToolButton { background: @surface; border: 1px solid @border; border-radius: 8px; padding: 7px 14px; }
QPushButton:hover, QToolButton:hover { border-color: @muted; }
QPushButton:pressed, QToolButton:pressed { background: @surfaceAlt; }
QPushButton:focus, QToolButton:focus { border: 1px solid @accent; }
QPushButton:disabled, QToolButton:disabled { color: @muted; background: @surfaceAlt; }
QToolButton[flat="true"], QPushButton[flat="true"] { border: 0; background: transparent; padding: 6px; }
QToolButton[flat="true"]:hover, QPushButton[flat="true"]:hover { background: @hoverBg; }
QToolButton::menu-indicator, QPushButton::menu-indicator { image: none; width: 0; }
#Primary { background: @accent; color: @onAccent; border: 0; font-weight: 600; padding: 9px 18px; }
#Primary:hover { background: @accentHover; }
#Primary:disabled { background: @border; color: @muted; }
#Danger { color: @danger; }
#RecordButton { background: @accent; color: @onAccent; border: 0; border-radius: 26px; font-size: 12.5pt;
  font-weight: 600; padding: 14px 30px; }
#RecordButton:hover { background: @accentHover; }
#StopButton { background: @recording; color: #FFFFFF; border: 0; border-radius: 22px; font-weight: 600; padding: 11px 24px; }
#Timer { font-size: 40pt; font-weight: 300; }
QTabBar::tab { background: transparent; border: 0; border-bottom: 2px solid transparent; padding: 8px 4px; margin-right: 18px;
  color: @muted; font-weight: 600; }
QTabBar::tab:selected { color: @text; border-bottom: 2px solid @accent; }
QTabBar::tab:hover { color: @text; }
QTabWidget::pane { border: 0; border-top: 1px solid @border; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: @border; border-radius: 4px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: @muted; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: @border; border-radius: 4px; min-width: 30px; }
QCheckBox::indicator { width: 16px; height: 16px; }
QProgressBar { background: @surfaceAlt; border: 0; border-radius: 3px; height: 6px; text-align: center; color: transparent; }
QProgressBar::chunk { background: @accent; border-radius: 3px; }
QSlider::groove:horizontal { height: 4px; background: @border; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @accent; border-radius: 2px; }
QSlider::handle:horizontal { background: @surface; border: 2px solid @accent; width: 12px; height: 12px; margin: -6px 0; border-radius: 8px; }
QMenu { background: @surface; border: 1px solid @border; padding: 6px; border-radius: 8px; }
QMenu::item { padding: 7px 22px 7px 12px; border-radius: 6px; }
QMenu::item:selected { background: @accentSoft; color: @text; }
QMenu::separator { height: 1px; background: @border; margin: 5px 8px; }
QTextBrowser { border: 0; background: @surface; }
QListView, QTreeView { background: @surface; border: 0; outline: 0; }
QSplitter::handle { background: @border; }
QGroupBox { border: 0; margin-top: 18px; font-weight: 600; }
QGroupBox::title { subcontrol-origin: margin; left: 0; padding: 0; }
)";
    const QColor warn = color(Token::Warning);
    QColor warnSoft = warn;
    warnSoft.setAlpha(28);
    QColor hover = color(Token::Text);
    hover.setAlpha(18);
    auto rgba = [](const QColor &c) {
        return QString("rgba(%1,%2,%3,%4)").arg(c.red()).arg(c.green()).arg(c.blue()).arg(c.alpha());
    };
    const QList<QPair<QString, QString>> vars = {
        {"@surfaceAlt", hex(Token::SurfaceAlt)}, {"@surface", hex(Token::Surface)},
        {"@border", hex(Token::Border)},         {"@muted", hex(Token::Muted)},
        {"@accentSoft", hex(Token::AccentSoft)}, {"@accentHover", hex(Token::AccentHover)},
        {"@accent", hex(Token::Accent)},         {"@onAccent", hex(Token::OnAccent)},
        {"@recording", hex(Token::Recording)},   {"@danger", hex(Token::Danger)},
        {"@warningSoft", rgba(warnSoft)},        {"@warning", hex(Token::Warning)},
        {"@hoverBg", rgba(hover)},               {"@text", hex(Token::Text)},
    };
    for (const auto &v : vars)
        css.replace(v.first, v.second);
    return css;
}

}  // namespace afterline::ui
