// SPDX-License-Identifier: MIT
//
// AfterLine's own dialogs: a card that matches the rest of the app (instead
// of the desktop's stock message boxes), shown over a dimmed window.
#pragma once

#include <QColor>
#include <QString>
#include <QStringList>

class QWidget;

namespace afterline {
struct FolderInfo;
}

namespace afterline::ui {

enum class Tone { Neutral, Info, Warning, Danger };

// Asks for confirmation. Returns true when the confirm button was chosen.
bool confirm(QWidget *parent, const QString &title, const QString &text, const QString &confirmText,
             Tone tone = Tone::Neutral, const QString &iconName = {}, const QString &cancelText = {});
// Tells the person something; one button.
void inform(QWidget *parent, const QString &title, const QString &text, Tone tone = Tone::Info,
            const QString &iconName = {});
// Asks for a line of text. Returns false when cancelled.
bool askText(QWidget *parent, const QString &title, const QString &text, QString *value,
             const QString &placeholder = {}, const QString &confirmText = {}, bool allowEmpty = false);

// Creates or edits a folder: name, icon and colour. Returns false when
// cancelled. `taken`: names already used (case-insensitive).
bool editFolder(QWidget *parent, FolderInfo *folder, bool isNew, const QStringList &taken);
// Icons and colours a folder can have.
const QStringList &folderIcons();
const QStringList &folderColors();
// The folder's colour, adjusted for the current theme.
QColor folderColor(const QString &stored);

}  // namespace afterline::ui
