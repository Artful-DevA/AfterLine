// SPDX-License-Identifier: MIT
#include "core/Notes.h"

#include "core/Json.h"
#include "core/Transcript.h"

#include <QCoreApplication>
#include <QJsonArray>

namespace afterline {

namespace {

QString tr(const char *s) { return QCoreApplication::translate("Notes", s); }

QString stamp(const QJsonObject &item, bool links)
{
    const double t = item["start"].toDouble(-1);
    if (t < 0)
        return {};
    return links ? QString(" [%1](%2)").arg(formatTime(t), timeLink(t)) : QString(" (%1)").arg(formatTime(t));
}

QString escape(QString s)
{
    // Keep model text from turning into Markdown structure.
    s.replace('\\', "\\\\");
    for (const char *c : {"*", "_", "`", "[", "]", "#"})
        s.replace(c, QString("\\") + c);
    return s.simplified();
}

}  // namespace

int MeetingNotes::actionItemCount() const { return data["action_items"].toArray().size(); }

MeetingNotes loadNotes(const QString &path)
{
    return {readJsonFile(path)};
}

QString timeLink(double seconds) { return QString("afterline:t=%1").arg(seconds, 0, 'f', 2); }

double parseTimeLink(const QString &url)
{
    if (!url.startsWith("afterline:t="))
        return -1;
    bool ok = false;
    const double t = url.mid(12).toDouble(&ok);
    return ok ? t : -1;
}

QSet<QString> loadDoneTasks(const QString &path)
{
    QSet<QString> done;
    for (const auto v : readJsonFile(path)["done"].toArray())
        done.insert(v.toString());
    return done;
}

bool saveDoneTasks(const QString &path, const QSet<QString> &done)
{
    QStringList list(done.begin(), done.end());
    list.sort();
    return writeJsonFile(path, QJsonObject{{"done", QJsonArray::fromStringList(list)}});
}

QString notesToMarkdown(const MeetingNotes &notes, bool links, const QSet<QString> &done)
{
    const QJsonObject &d = notes.data;
    QString md;
    md += "## " + tr("Summary") + "\n\n";
    md += d["summary"].toString().isEmpty() ? "_" + tr("No summary.") + "_\n\n" : escape(d["summary"].toString()) + "\n\n";

    auto section = [&](const char *key, const QString &title, const QString &none, auto line) {
        const QJsonArray items = d[key].toArray();
        md += "## " + title + "\n\n";
        if (items.isEmpty()) {
            md += "_" + none + "_\n\n";
            return;
        }
        for (const auto v : items)
            md += line(v.toObject()) + "\n";
        md += "\n";
    };
    section("key_points", tr("Key points"), tr("None."),
            [&](const QJsonObject &o) { return "- " + escape(o["text"].toString()) + stamp(o, links); });
    section("decisions", tr("Decisions"), tr("No decisions were recorded."),
            [&](const QJsonObject &o) { return "- " + escape(o["text"].toString()) + stamp(o, links); });
    section("action_items", tr("Action items"), tr("No action items were mentioned."), [&](const QJsonObject &o) {
        QString s = (done.contains(o["task"].toString()) ? "- [x] " : "- [ ] ") + escape(o["task"].toString());
        const QString owner = o["owner"].toString(), due = o["due"].toString();
        if (!owner.isEmpty())
            s += " — **" + escape(owner) + "**";
        if (!due.isEmpty())
            s += " · " + tr("due") + " " + escape(due);
        return s + stamp(o, links);
    });
    section("open_questions", tr("Open questions"), tr("None."),
            [&](const QJsonObject &o) { return "- " + escape(o["text"].toString()) + stamp(o, links); });
    section("dates", tr("Important dates"), tr("No dates were mentioned."), [&](const QJsonObject &o) {
        QString s = "- **" + escape(o["date"].toString()) + "**";
        if (!o["text"].toString().isEmpty())
            s += " — " + escape(o["text"].toString());
        return s + stamp(o, links);
    });
    return md;
}

}  // namespace afterline
