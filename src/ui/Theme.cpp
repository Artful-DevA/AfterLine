// SPDX-License-Identifier: MIT
#include "ui/Theme.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
#include <QStyleHints>

#include "ui/Icons.h"

#include <algorithm>

namespace afterline::ui {

namespace {

bool g_dark = false;
bool g_systemDarkKnown = false;
bool g_systemDark = false;

struct Palette {
    const char *background, *surface, *surfaceAlt, *border, *text, *muted, *accent, *accentHover, *accentSoft,
        *onAccent, *recording, *success, *warning, *danger, *trackComputer, *trackMicrophone;
};

// background, surface (cards), surfaceAlt (sidebar), border, text, muted, accent, accentHover, accentSoft,
// onAccent, recording, success, warning, danger, trackComputer, trackMicrophone
constexpr Palette kLight = {"#EFE7DB", "#FCF8F2", "#E5DBCC", "#D6C9B7", "#27211D", "#665B50", "#B4553A",
                            "#9C4730", "#F0D9CC", "#FFFFFF", "#C8372D", "#3B7550", "#8F5D10", "#B3261E",
                            "#B4553A", "#4A6A82"};
constexpr Palette kDark = {"#2B2420", "#362E28", "#241E1A", "#4D423A", "#F3EBE1", "#C1B2A3", "#E08C6B",
                           "#EBA085", "#5C3D31", "#241E1A", "#F0564B", "#86C499", "#E5AC62", "#F2827A",
                           "#E08C6B", "#93B6CF"};

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
QToolTip { padding: 6px 9px; border: 0; border-radius: 6px; }
#Sidebar { background: @surfaceAlt; border-right: 1px solid @border; }
#Sidebar QListView { background: transparent; border: 0; outline: 0; }
#BrandName { font-size: 15pt; font-weight: 700; }
#PageTitle { font-size: 21pt; font-weight: 650; }
#SectionTitle { font-size: 12pt; font-weight: 600; }
#Eyebrow { font-size: 8.5pt; font-weight: 700; color: @muted; letter-spacing: 1px; }
#Muted, .Muted { color: @muted; }
#Card { background: @surface; border: 1px solid @border; border-radius: 14px; }
#ModelRow { background: @background; border: 1px solid @border; border-radius: 12px; }
#SoftCard { background: @surfaceAlt; border: 0; border-radius: 12px; }
#Banner { background: @accentSoft; border-radius: 10px; }
#WarningBanner { background: @warningSoft; border: 1px solid @warning; border-radius: 10px; }
#ErrorText { color: @danger; }
#Pill { background: @accentSoft; color: @accent; border-radius: 10px; padding: 2px 9px; font-size: 9pt; font-weight: 700; }
#LivePill { background: @recordingSoft; color: @recording; border-radius: 10px; padding: 3px 10px; font-size: 9pt; font-weight: 700; }
QLineEdit, QPlainTextEdit, QTextEdit, QComboBox, QSpinBox {
  background: @surface; border: 1px solid @border; border-radius: 9px; padding: 0 10px; min-height: 34px;
  selection-background-color: @accentSoft; selection-color: @text; }
QPlainTextEdit, QTextEdit { padding: 8px 10px; }
#Search { padding: 7px 8px; min-height: 0; border-radius: 10px; }
QLineEdit:hover, QComboBox:hover, QSpinBox:hover { border-color: @borderStrong; }
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QComboBox:focus, QSpinBox:focus { border: 1px solid @accent; }
QComboBox { combobox-popup: 0; padding-right: 30px; }
QComboBox:on { border: 1px solid @accent; }
QComboBox::drop-down { subcontrol-origin: padding; subcontrol-position: center right; border: 0; width: 30px; }
QComboBox::down-arrow { image: url(@chevron); width: 14px; height: 14px; }
QComboBox::down-arrow:on { top: 1px; }
QComboBox:disabled { color: @muted; background: @surfaceAlt; }
QComboBox QAbstractItemView { background: @surface; border: 1px solid @border; border-radius: 10px; outline: 0; padding: 5px;
  selection-background-color: @accentSoft; selection-color: @text; }
QComboBox QAbstractItemView::item { min-height: 32px; padding: 0 8px; border-radius: 7px; }
QComboBox QAbstractItemView::item:hover, QComboBox QAbstractItemView::item:selected { background: @accentSoft; color: @text; }
#TitleEdit { font-size: 19pt; font-weight: 650; border: 1px solid transparent; background: transparent; padding: 2px 6px; min-height: 40px; }
#TitleEdit:hover { border: 1px solid @border; }
#TitleEdit:focus { border: 1px solid @accent; background: @surface; }
QPushButton, QToolButton { background: @surface; border: 1px solid @border; border-radius: 9px; padding: 0 14px; min-height: 34px; }
QPushButton:hover, QToolButton:hover { border-color: @borderStrong; background: @surfaceHover; }
QPushButton:pressed, QToolButton:pressed { background: @surfaceAlt; }
QPushButton:focus, QToolButton:focus { border: 1px solid @accent; }
QPushButton:disabled, QToolButton:disabled { color: @muted; background: @surfaceAlt; border-color: @surfaceAlt; }
QToolButton[flat="true"], QPushButton[flat="true"] { border: 0; background: transparent; padding: 0; min-height: 32px; min-width: 32px; border-radius: 8px; }
QToolButton[flat="true"]:hover, QPushButton[flat="true"]:hover { background: @hoverBg; }
QLineEdit QToolButton { min-height: 0; min-width: 0; padding: 0; border: 0; background: transparent; }
QToolButton::menu-indicator, QPushButton::menu-indicator { image: none; width: 0; }
#Primary { background: @accent; color: @onAccent; border: 0; font-weight: 650; padding: 0 18px; min-height: 36px; }
#Primary:hover { background: @accentHover; }
#Primary:disabled { background: @border; color: @muted; }
#Danger { color: @danger; }
#UserBubble { background: @accent; border-radius: 14px; border-bottom-right-radius: 4px; }
#UserBubbleText { color: @onAccent; }
#AnswerBubble { background: @surface; border: 1px solid @border; border-radius: 14px; border-bottom-left-radius: 4px; }
#SourceChip { background: @accentSoftTrans; color: @text; border: 0; border-radius: 8px; padding: 0 10px; min-height: 26px;
  font-size: 9.5pt; text-align: left; }
#SourceChip:hover { background: @accentSoft; }
#SuggestionChip { background: @surface; border: 1px solid @border; border-radius: 10px; padding: 0 12px; min-height: 36px;
  text-align: left; }
#SuggestionChip:hover { border-color: @accent; }
#Composer { background: @surface; border: 1px solid @border; border-radius: 12px; }
#ComposerInput { border: 0; background: transparent; padding: 0 2px; }
#SendButton { background: @accent; border: 0; border-radius: 9px; min-height: 34px; min-width: 34px; max-width: 34px; padding: 0; }
#SendButton:hover { background: @accentHover; }
#SendButton:disabled { background: @border; }
#IconChoice { background: transparent; border: 1px solid transparent; border-radius: 10px; min-height: 40px; max-height: 40px; min-width: 40px; max-width: 40px; padding: 0; }
#IconChoice:hover { background: @hoverBg; }
#IconChoice:checked { background: @background; border: 1px solid @borderStrong; }
#TaskRow { background: @surface; border: 1px solid @border; border-radius: 12px; }
#TaskRow[hover="true"] { border-color: @borderStrong; }
#FolderChip { background: @surface; border: 1px solid @border; border-radius: 12px; padding: 0 10px; min-height: 24px;
  font-size: 9.5pt; font-weight: 600; }
#FolderChip:hover { border-color: @borderStrong; }
#FolderNote { background: @accentSoftTrans; border-radius: 10px; }
#LinkButton { background: transparent; border: 0; color: @accent; padding: 0; min-height: 22px; font-weight: 600; }
#LinkButton:hover { color: @accentHover; text-decoration: underline; }
#LinkButton:disabled { color: @muted; }
#SmallButton { min-height: 28px; padding: 0 12px; border-radius: 8px; }
#DialogCard { background: @surface; border: 1px solid @border; border-radius: 16px; }
#DialogTitle { font-size: 13.5pt; font-weight: 650; }
#DangerButton { background: @danger; color: #FFFFFF; border: 0; font-weight: 650; padding: 0 18px; min-height: 36px; }
#DangerButton:hover { background: @dangerHover; }
#RecordButton { background: @accent; color: @onAccent; border: 0; border-radius: 25px; font-size: 12pt;
  font-weight: 650; padding: 0 30px; min-height: 50px; }
#RecordButton:hover { background: @accentHover; }
#RecordButton:disabled { background: @border; color: @muted; }
#StopButton { background: @recording; color: #FFFFFF; border: 0; border-radius: 21px; font-weight: 650; padding: 0 22px; min-height: 42px; }
#StopButton:hover { background: @recordingHover; }
#PauseButton { border-radius: 21px; padding: 0 20px; min-height: 42px; font-weight: 600; }
#Timer { font-size: 34pt; font-weight: 300; }
QTabBar::tab { background: transparent; border: 0; border-bottom: 2px solid transparent; padding: 9px 4px; margin-right: 20px;
  color: @muted; font-weight: 600; }
QTabBar::tab:selected { color: @text; border-bottom: 2px solid @accent; }
QTabBar::tab:hover { color: @text; }
QTabWidget::pane { border: 0; border-top: 1px solid @border; }
QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }
QScrollBar::handle:vertical { background: @border; border-radius: 3px; min-height: 30px; }
QScrollBar::handle:vertical:hover { background: @muted; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }
QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }
QScrollBar::handle:horizontal { background: @border; border-radius: 3px; min-width: 30px; }
QCheckBox { spacing: 9px; }
QCheckBox::indicator { width: 18px; height: 18px; border: 1.5px solid @borderStrong; border-radius: 5px; background: @surface; }
QCheckBox::indicator:hover { border-color: @accent; }
QCheckBox::indicator:checked { background: @accent; border-color: @accent; image: url(@check); }
QRadioButton { spacing: 9px; }
QProgressBar { background: @surfaceAlt; border: 0; border-radius: 3px; max-height: 6px; min-height: 6px; text-align: center; color: transparent; }
QProgressBar::chunk { background: @accent; border-radius: 3px; }
QSlider::groove:horizontal { height: 4px; background: @border; border-radius: 2px; }
QSlider::sub-page:horizontal { background: @accent; border-radius: 2px; }
QSlider::handle:horizontal { background: @surface; border: 2px solid @accent; width: 12px; height: 12px; margin: -6px 0; border-radius: 8px; }
QMenu { background: @surface; border: 1px solid @border; padding: 6px; border-radius: 10px; }
QMenu::item { padding: 8px 24px 8px 12px; border-radius: 7px; }
QMenu::item:selected { background: @accentSoft; color: @text; }
QMenu::separator { height: 1px; background: @border; margin: 5px 8px; }
QTextBrowser { border: 0; background: @surface; }
QListView, QTreeView { background: @surface; border: 0; outline: 0; }
QSplitter::handle { background: @border; }
QGroupBox { border: 0; margin-top: 18px; font-weight: 600; }
QGroupBox::title { subcontrol-origin: margin; left: 0; padding: 0; }
QDialog { background: @background; }

)";
    const QColor warn = color(Token::Warning);
    QColor warnSoft = warn;
    warnSoft.setAlpha(28);
    QColor hover = color(Token::Text);
    hover.setAlpha(18);
    QColor accentSoftTrans = color(Token::AccentSoft);
    accentSoftTrans.setAlpha(150);
    QColor recSoft = color(Token::Recording);
    recSoft.setAlpha(isDark() ? 50 : 30);
    const QColor borderStrong = isDark() ? color(Token::Border).lighter(135) : color(Token::Border).darker(118);
    const QColor surfaceHover = isDark() ? color(Token::Surface).lighter(110) : color(Token::Surface).darker(102);
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
        {"@borderStrong", borderStrong.name()},  {"@accentSoftTrans", rgba(accentSoftTrans)},  {"@surfaceHover", surfaceHover.name()},
        {"@recordingSoft", rgba(recSoft)},       {"@recordingHover", color(Token::Recording).darker(112).name()},
        {"@background", hex(Token::Background)}, {"@dangerHover", color(Token::Danger).darker(112).name()},
        {"@chevron", iconFile("chevron-down", color(Token::Muted), 2.2)},
        {"@check", iconFile("check", color(Token::OnAccent), 3.0)},
    };
    // Longest names first, so "@surfaceAlt" is not caught by "@surface".
    auto sorted = vars;
    std::sort(sorted.begin(), sorted.end(),
              [](const auto &a, const auto &b) { return a.first.size() > b.first.size(); });
    for (const auto &v : sorted)
        css.replace(v.first, v.second);
    return css;
}

}  // namespace afterline::ui
