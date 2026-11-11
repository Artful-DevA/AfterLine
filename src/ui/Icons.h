// SPDX-License-Identifier: MIT
//
// AfterLine's interface icons: simple 24×24 stroke drawings, drawn for this
// project, tinted to the current theme.
#pragma once

#include <QColor>
#include <QIcon>
#include <QString>

namespace afterline::ui {

// name: record, stop, pause, play, mic, mic-off, computer, app, speaker,
// search, settings, folder, tag, trash, download, export, more, ask, check,
// alert, back, plus, refresh, file, clock, close, copy, edit, rewind, forward,
// notes, transcript, waveform, lock, chevron-down, chevron-right, sparkle
QIcon icon(const QString &name, const QColor &color = QColor());
// The icon as an SVG file, for style sheets.
QString iconFile(const QString &name, const QColor &color, double strokeWidth = 1.8);
// An application's own icon from the icon theme, or a generic one.
QIcon appIcon(const QString &themeName);

}  // namespace afterline::ui
