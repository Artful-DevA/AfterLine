// SPDX-License-Identifier: MIT
//
// Cleaning up what the speech recogniser writes.
#pragma once

#include <QString>

namespace afterline {

// The recogniser sometimes loops ("ice ice ice ...", "that's the mom, that's
// the mom, ..."), mostly on music or noise. A phrase of 2 to 8 words said three
// or more times in a row, or a single word said four or more times, is kept
// once. `removedWords` receives how many words were dropped.
QString collapseRepetition(const QString &text, int *removedWords = nullptr);
// More words than anyone can say in that time: invented, not heard.
bool implausiblyFast(const QString &text, double seconds);

}  // namespace afterline
