// SPDX-License-Identifier: MIT
#include "Notes.h"

#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <map>

namespace notes {

namespace {

QSet<QString> words(const QString &text)
{
    QSet<QString> out;
    QString w;
    for (const QChar c : text.toLower() + ' ') {
        if (c.isLetterOrNumber()) {
            w += c;
        } else if (!w.isEmpty()) {
            out.insert(w);
            w.clear();
        }
    }
    return out;
}

// Words that carry meaning (drops short function words in any language).
QSet<QString> significant(const QString &text)
{
    QSet<QString> out;
    for (const QString &w : words(text))
        if (w.size() >= 3 || w.at(0).isDigit())
            out.insert(w);
    return out;
}

double overlap(const QString &a, const QString &b)
{
    const QSet<QString> wa = significant(a), wb = significant(b);
    if (wa.isEmpty() || wb.isEmpty())
        return 0;
    int common = 0;
    for (const auto &w : wa)
        common += wb.contains(w);
    return double(common) / double(std::min(wa.size(), wb.size()));
}

QString textAround(const std::vector<Line> &all, const QJsonArray &sources, int radius)
{
    QSet<int> wanted;
    for (const auto v : sources)
        for (int k = -radius; k <= radius; ++k)
            wanted.insert(v.toInt() + k);
    QString out;
    for (const auto &l : all)
        if (wanted.contains(l.id))
            out += l.text + ' ';
    return out;
}

bool mentionedIn(const QString &needle, const QString &haystack)
{
    const QSet<QString> want = significant(needle);
    if (want.isEmpty())
        return false;
    const QSet<QString> have = words(haystack);
    int found = 0;
    for (const auto &w : want)
        found += have.contains(w);
    return double(found) / double(want.size()) >= 0.6;
}

const char *kListKeys[] = {"key_points", "decisions", "action_items", "open_questions", "dates"};

QString itemText(const QJsonObject &o)
{
    return o.contains("task") ? o["task"].toString() : o["text"].toString();
}

}  // namespace

std::vector<Line> loadTranscript(const QJsonObject &transcript)
{
    std::vector<Line> lines;
    for (const auto v : transcript["segments"].toArray()) {
        const QJsonObject o = v.toObject();
        const QString text = o["text"].toString().trimmed();
        if (!text.isEmpty())
            lines.push_back({o["id"].toInt(), o["start"].toDouble(), o["source"].toString(), text});
    }
    return lines;
}

QString speakerLabel(const QString &source)
{
    return source == "microphone" ? QStringLiteral("You") : QStringLiteral("Meeting");
}

QString timestamp(double seconds)
{
    const int s = int(seconds);
    return QString("%1:%2:%3").arg(s / 3600, 2, 10, QChar('0')).arg(s / 60 % 60, 2, 10, QChar('0'))
        .arg(s % 60, 2, 10, QChar('0'));
}

QString formatLine(const Line &line)
{
    return QString("[#%1 %2 %3] %4").arg(line.id).arg(timestamp(line.start), speakerLabel(line.source), line.text);
}

std::vector<std::vector<Line>> chunk(const std::vector<Line> &lines,
                                     const std::function<int(const QString &)> &countTokens, int budget)
{
    std::vector<std::vector<Line>> out;
    std::vector<Line> current;
    int used = 0;
    for (const auto &l : lines) {
        const int t = countTokens(formatLine(l)) + 1;
        if (!current.empty() && used + t > budget) {
            out.push_back(std::move(current));
            current.clear();
            used = 0;
        }
        current.push_back(l);
        used += t;
    }
    if (!current.empty())
        out.push_back(std::move(current));
    return out;
}

QString languageName(const QString &code)
{
    static const QHash<QString, QString> names = {
        {"en", "English"}, {"ro", "Romanian"}, {"de", "German"}, {"fr", "French"}, {"es", "Spanish"},
        {"it", "Italian"}, {"pt", "Portuguese"}, {"nl", "Dutch"}, {"pl", "Polish"}, {"hu", "Hungarian"},
        {"uk", "Ukrainian"}, {"ru", "Russian"}, {"cs", "Czech"}, {"sv", "Swedish"}, {"tr", "Turkish"},
        {"el", "Greek"}, {"bg", "Bulgarian"}, {"ja", "Japanese"}, {"zh", "Chinese"}, {"ko", "Korean"},
    };
    return names.value(code, QStringLiteral("the same language as the transcript"));
}

// JSON grammars (GBNF). Every list item must carry at least one citation;
// lists are bounded so a confused model cannot ramble forever.
static const char *kCommonRules = R"(
ids ::= "[" ws id ( "," ws id ){0,5} ws "]"
id ::= [0-9] [0-9]{0,5}
string ::= "\"" char* "\""
char ::= [^"\\\x7F\x00-\x1F] | "\\" (["\\/bfnrt] | "u" [0-9a-fA-F]{4})
ws ::= [ \t\n]{0,8}
)";

QString extractionGrammar()
{
    return QString(R"(root ::= "{" ws "\"summary\":" ws string "," ws "\"key_points\":" ws points "," ws "\"decisions\":" ws points "," ws "\"action_items\":" ws actions "," ws "\"open_questions\":" ws points "," ws "\"dates\":" ws dates ws "}"
points ::= "[" ws ( point ( "," ws point ){0,7} )? ws "]"
point ::= "{" ws "\"text\":" ws string "," ws "\"sources\":" ws ids ws "}"
actions ::= "[" ws ( action ( "," ws action ){0,9} )? ws "]"
action ::= "{" ws "\"task\":" ws string "," ws "\"owner\":" ws string "," ws "\"due\":" ws string "," ws "\"sources\":" ws ids ws "}"
dates ::= "[" ws ( date ( "," ws date ){0,7} )? ws "]"
date ::= "{" ws "\"date\":" ws string "," ws "\"text\":" ws string "," ws "\"sources\":" ws ids ws "}"
)") + kCommonRules;
}

QString overviewGrammar()
{
    return QString(R"(root ::= "{" ws "\"title\":" ws string "," ws "\"summary\":" ws string ws "}")") + kCommonRules;
}

QString askGrammar()
{
    return QString(R"(root ::= "{" ws "\"found\":" ws ("true" | "false") "," ws "\"answer\":" ws string "," ws "\"sources\":" ws ( "[]" | ids ) ws "}")") +
           kCommonRules;
}

QString extractionSystemPrompt(const QString &languageCode)
{
    return QString(
        "You write meeting notes from a transcript. Each transcript line starts with [#number time speaker].\n"
        "Rules:\n"
        "- Use only what is said in the transcript. Never invent names, tasks, decisions, dates or deadlines.\n"
        "- Every item must cite the line numbers that support it in \"sources\".\n"
        "- Empty lists are correct when the transcript has nothing of that kind. Do not fill lists for the sake of it.\n"
        "- decisions: only things the participants clearly agreed on or decided.\n"
        "- action_items: tasks someone will do. owner is the responsible person only if the transcript names "
        "them (\"You\" if the person who recorded said they will do it), otherwise \"\". due is a deadline only "
        "if one is stated, otherwise \"\".\n"
        "- open_questions: questions raised and not answered in this part.\n"
        "- dates: dates, days or deadlines explicitly mentioned.\n"
        "- \"You\" is the person who recorded the meeting; \"Meeting\" is everyone else on the call.\n"
        "- The transcript comes from speech recognition and may contain errors; do not repeat obvious mistakes.\n"
        "- Write everything in %1. Be concise.")
        .arg(languageName(languageCode));
}

QString extractionUserPrompt(const QString &title, int part, int parts, const std::vector<Line> &section)
{
    QString text;
    for (const auto &l : section)
        text += formatLine(l) + '\n';
    QString where = parts > 1 ? QString("This is part %1 of %2 of the meeting (%3 to %4).\n")
                                    .arg(part).arg(parts)
                                    .arg(timestamp(section.front().start), timestamp(section.back().start))
                              : QString();
    return QString("Meeting: %1\n%2\nTranscript:\n%3\nWrite the notes as JSON.")
        .arg(title.isEmpty() ? QStringLiteral("(untitled)") : title, where, text);
}

QString overviewSystemPrompt(const QString &languageCode)
{
    return QString("You summarise meetings. Using only the section summaries and points given, write a short "
                   "title (at most 8 words) and a summary of 2 to 5 sentences of what happened in the meeting. "
                   "Add nothing that is not in the input. Write in %1.")
        .arg(languageName(languageCode));
}

QString overviewUserPrompt(const QString &title, const QStringList &sectionSummaries, const QStringList &keyPoints)
{
    QString text = "Current title: " + (title.isEmpty() ? QStringLiteral("(none)") : title) + "\n\nSections:\n";
    for (int i = 0; i < sectionSummaries.size(); ++i)
        text += QString("%1. %2\n").arg(i + 1).arg(sectionSummaries[i]);
    if (!keyPoints.isEmpty()) {
        text += "\nKey points:\n";
        for (const auto &k : keyPoints)
            text += "- " + k + "\n";
    }
    return text + "\nWrite the title and summary as JSON.";
}

QString askSystemPrompt()
{
    return "You answer questions about the user's recorded meetings using only the numbered transcript passages "
           "provided. If the passages do not contain the answer, set found to false and say briefly that the "
           "meetings do not cover it. Never guess. Cite the passage numbers you used in sources. Answer in the "
           "language of the question, in a few sentences.";
}

QString askUserPrompt(const QString &question, const QJsonArray &passages)
{
    QString text = "Passages:\n";
    for (int i = 0; i < passages.size(); ++i) {
        const QJsonObject p = passages[i].toObject();
        text += QString("[%1] (%2, %3, %4) %5\n")
                    .arg(i + 1)
                    .arg(p["meetingTitle"].toString(), p["date"].toString(), timestamp(p["start"].toDouble()),
                         p["text"].toString());
    }
    return text + "\nQuestion: " + question + "\nAnswer as JSON.";
}

QJsonObject validateSection(const QJsonObject &raw, const std::vector<Line> &section,
                            const std::vector<Line> &all, Stats *stats)
{
    QSet<int> valid;
    for (const auto &l : section)
        valid.insert(l.id);
    QHash<int, QString> sourceOf;
    for (const auto &l : all)
        sourceOf[l.id] = l.source;

    QJsonObject out;
    out["summary"] = raw["summary"].toString().trimmed();
    for (const char *key : kListKeys) {
        QJsonArray cleaned;
        for (const auto v : raw[key].toArray()) {
            QJsonObject item = v.toObject();
            QJsonArray sources;
            QSet<int> seen;
            for (const auto s : item["sources"].toArray()) {
                const int id = s.toInt(-1);
                if (valid.contains(id) && !seen.contains(id)) {
                    sources.append(id);
                    seen.insert(id);
                }
            }
            const QString text = itemText(item).trimmed();
            if (sources.isEmpty() || text.isEmpty()) {
                if (stats)
                    stats->dropped++;
                continue;
            }
            item["sources"] = sources;
            const QString context = textAround(all, sources, 2);
            if (item.contains("owner")) {
                const QString owner = item["owner"].toString().trimmed();
                bool keep = !owner.isEmpty();
                if (keep && owner.compare("You", Qt::CaseInsensitive) == 0) {
                    keep = false;
                    for (const auto s : sources)
                        keep |= sourceOf.value(s.toInt()) == "microphone";
                } else if (keep) {
                    keep = mentionedIn(owner, context);
                }
                if (!keep && !owner.isEmpty() && stats)
                    stats->ownersCleared++;
                item["owner"] = keep ? owner : QString();
            }
            if (item.contains("due")) {
                const QString due = item["due"].toString().trimmed();
                const bool keep = !due.isEmpty() && mentionedIn(due, context);
                if (!keep && !due.isEmpty() && stats)
                    stats->datesCleared++;
                item["due"] = keep ? due : QString();
            }
            if (std::string(key) == "dates") {
                // A date nobody said is worse than no date.
                if (!mentionedIn(item["date"].toString(), context)) {
                    if (stats)
                        stats->dropped++;
                    continue;
                }
            }
            if (item.contains("task"))
                item["task"] = text;
            else
                item["text"] = text;
            cleaned.append(item);
        }
        out[key] = cleaned;
    }
    return out;
}

QJsonObject mergeSections(const std::vector<QJsonObject> &sections)
{
    QJsonObject out;
    QJsonArray summaries;
    for (const auto &s : sections)
        if (!s["summary"].toString().isEmpty())
            summaries.append(s["summary"]);
    out["section_summaries"] = summaries;
    for (const char *key : kListKeys) {
        QJsonArray merged;
        for (const auto &s : sections) {
            for (const auto v : s[key].toArray()) {
                QJsonObject item = v.toObject();
                bool duplicate = false;
                for (int i = 0; i < merged.size(); ++i) {
                    QJsonObject m = merged[i].toObject();
                    if (overlap(itemText(m), itemText(item)) >= 0.75) {
                        QJsonArray src = m["sources"].toArray();
                        for (const auto x : item["sources"].toArray())
                            if (!src.contains(x))
                                src.append(x);
                        m["sources"] = src;
                        for (const char *f : {"owner", "due"})
                            if (m.contains(f) && m[f].toString().isEmpty())
                                m[f] = item[f];
                        merged[i] = m;
                        duplicate = true;
                        break;
                    }
                }
                if (!duplicate)
                    merged.append(item);
            }
        }
        out[key] = merged;
    }
    return out;
}

void attachTimes(QJsonObject &notes, const std::vector<Line> &all)
{
    QHash<int, double> startOf;
    for (const auto &l : all)
        startOf[l.id] = l.start;
    for (const char *key : kListKeys) {
        QJsonArray arr = notes[key].toArray();
        for (int i = 0; i < arr.size(); ++i) {
            QJsonObject item = arr[i].toObject();
            double first = -1;
            for (const auto s : item["sources"].toArray()) {
                const double t = startOf.value(s.toInt(), -1);
                if (t >= 0 && (first < 0 || t < first))
                    first = t;
            }
            item["start"] = first;
            arr[i] = item;
        }
        notes[key] = arr;
    }
}

QJsonObject validateAnswer(const QJsonObject &raw, int passageCount)
{
    QJsonArray sources;
    for (const auto s : raw["sources"].toArray()) {
        const int n = s.toInt(0);
        if (n >= 1 && n <= passageCount && !sources.contains(n))
            sources.append(n);
    }
    bool found = raw["found"].toBool() && !sources.isEmpty();
    QString answer = raw["answer"].toString().trimmed();
    if (!found && raw["found"].toBool())
        answer = QStringLiteral("I could not find support for an answer in your meetings.");
    return {{"found", found}, {"answer", answer}, {"sources", sources}};
}

}  // namespace notes
