// SPDX-License-Identifier: MIT
#include "core/TextCleanup.h"

#include <QRegularExpression>
#include <QStringList>

#include <algorithm>

namespace afterline {

QString collapseRepetition(const QString &text, int *removedWords)
{
    QStringList words = text.split(QRegularExpression(R"(\s+)"), Qt::SkipEmptyParts);
    auto key = [](const QString &w) {
        QString k = w.toLower();
        k.remove(QRegularExpression(R"([^\p{L}\p{N}']+)"));
        return k;
    };
    int removed = 0;
    bool changed = true;
    while (changed) {
        changed = false;
        for (int n = 1; n <= 8 && !changed; ++n) {
            for (int i = 0; i + 3 * n <= int(words.size()) && !changed; ++i) {
                int reps = 1;
                while (i + (reps + 1) * n <= int(words.size())) {
                    bool same = true;
                    for (int k = 0; k < n && same; ++k)
                        same = key(words[i + k]) == key(words[i + reps * n + k]) && !key(words[i + k]).isEmpty();
                    if (!same)
                        break;
                    ++reps;
                }
                // A word said three times can be real speech ("no, no, no").
                if (reps >= (n == 1 ? 4 : 3)) {
                    words.erase(words.begin() + i + n, words.begin() + i + reps * n);
                    removed += (reps - 1) * n;
                    changed = true;
                }
            }
        }
    }
    if (removedWords)
        *removedWords = removed;
    return words.join(' ');
}

bool implausiblyFast(const QString &text, double seconds)
{
    const int words = int(text.split(' ', Qt::SkipEmptyParts).size());
    return words > 8 && words > std::max(1.0, seconds) * 6.5;
}

}  // namespace afterline
