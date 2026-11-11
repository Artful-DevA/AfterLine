// SPDX-License-Identifier: MIT
//
// The model-independent half of note generation: splitting a transcript into
// sections that fit the model, the prompts and output grammars, and checking
// what the model returns against the transcript.
//
// Every note must cite transcript lines. Citations that do not exist in the
// section are removed, and an item left without support is dropped. Owners
// and deadlines are kept only when the cited lines actually mention them. A
// small model will sometimes make things up; this is where that is caught.
#pragma once

#include <QJsonArray>
#include <QJsonObject>
#include <QString>

#include <functional>
#include <vector>

namespace notes {

struct Line {
    int id = 0;
    double start = 0;
    QString source;  // "computer" / "microphone"
    QString text;
};

std::vector<Line> loadTranscript(const QJsonObject &transcript);
QString speakerLabel(const QString &source);
QString timestamp(double seconds);
QString formatLine(const Line &line);

// Splits into consecutive sections of at most `budget` tokens.
std::vector<std::vector<Line>> chunk(const std::vector<Line> &lines,
                                     const std::function<int(const QString &)> &countTokens, int budget);

QString languageName(const QString &code);
QString extractionGrammar();
QString overviewGrammar();
QString askGrammar();
QString extractionSystemPrompt(const QString &languageCode);
QString extractionUserPrompt(const QString &title, int part, int parts, const std::vector<Line> &section);
QString overviewSystemPrompt(const QString &languageCode);
QString overviewUserPrompt(const QString &title, const QStringList &sectionSummaries, const QStringList &keyPoints);
QString askSystemPrompt();
QString askUserPrompt(const QString &question, const QJsonArray &passages);

struct Stats {
    int dropped = 0;        // items removed for lack of valid citations
    int ownersCleared = 0;  // owners not found in the cited lines
    int datesCleared = 0;   // deadlines not found in the cited lines
};

// Cleans one section's model output. `all` is the whole transcript (for
// looking around cited lines).
QJsonObject validateSection(const QJsonObject &raw, const std::vector<Line> &section,
                            const std::vector<Line> &all, Stats *stats);
// Combines sections, merging near-duplicate items and their citations.
QJsonObject mergeSections(const std::vector<QJsonObject> &sections);
// Adds the start time of the first citation to every item, for the UI.
void attachTimes(QJsonObject &notes, const std::vector<Line> &all);

QJsonObject validateAnswer(const QJsonObject &raw, int passageCount);

// Filing a meeting into one of the person's folders, from its notes.
QString classifyGrammar(const QStringList &folders);
QString classifySystemPrompt();
QString classifyUserPrompt(const QJsonObject &notes, const QString &title, const QStringList &folders);
// {"folder": one of folders or "", "confident": bool, "newFolder": short name or ""}
QJsonObject validateClassification(const QJsonObject &raw, const QStringList &folders);

}  // namespace notes
