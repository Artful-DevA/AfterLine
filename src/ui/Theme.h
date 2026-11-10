// SPDX-License-Identifier: MIT
//
// AfterLine's look: warm cream and terracotta in light mode, deep espresso in
// dark mode. Both palettes are designed (not inverted) and checked for text
// contrast. The theme follows the desktop unless the user picks one.
#pragma once

#include <QColor>
#include <QString>

class QApplication;

namespace afterline::ui {

enum class Token {
    Background,   // window
    Surface,      // cards, lists, documents
    SurfaceAlt,   // sidebar, toolbars
    Border,
    Text,
    Muted,        // secondary text
    Accent,       // terracotta
    AccentHover,
    AccentSoft,   // selection, highlights
    OnAccent,
    Recording,    // the recording indicator
    Success,
    Warning,
    Danger,
    TrackComputer,
    TrackMicrophone,
};

class Theme {
public:
    // mode: "system", "light" or "dark".
    static void apply(QApplication &app, const QString &mode);
    static bool isDark();
    static QColor color(Token t);
    static QString hex(Token t) { return color(t).name(); }
    static QString styleSheet();
};

}  // namespace afterline::ui
