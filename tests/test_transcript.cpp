// SPDX-License-Identifier: MIT
// Transcript model, exports, notes rendering, and the checks that keep
// invented content out of generated notes.
#include "Notes.h"  // engines/summarizer
#include "core/Export.h"
#include "core/Notes.h"
#include "core/Transcript.h"

#include <QJsonArray>
#include <QTemporaryDir>
#include <QtTest>

using namespace afterline;

namespace {

Transcript sample()
{
    Transcript t;
    t.language = "en";
    t.segments = {
        {0, 1.0, 3.5, "computer", "Good morning, let's review the website redesign.", 0.9, false},
        {1, 4.0, 7.0, "microphone", "I finished the mobile layout yesterday.", 0.9, false},
        {2, 8.0, 12.0, "computer", "Maria will prepare the release documentation by Friday.", 0.9, false},
        {3, 3725.5, 3729.0, "microphone", "I will send the screenshots to Maria today.", 0.9, false},
    };
    return t;
}

std::vector<notes::Line> lines()
{
    std::vector<notes::Line> out;
    for (const auto &s : sample().segments)
        out.push_back({s.id, s.start, s.source, s.text});
    return out;
}

}  // namespace

class TranscriptTest : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void formatsTimes()
    {
        QCOMPARE(formatTime(65), QString("1:05"));
        QCOMPARE(formatTime(3725.5), QString("01:02:05"));
        QCOMPARE(formatTime(5, true), QString("00:00:05"));
        QCOMPARE(formatTime(-3), QString("0:00"));
    }

    void savesCorrectionsAndKeepsTheOriginal()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("transcript.json");
        Transcript t = sample();
        QVERIFY(t.save(path));
        QVERIFY(t.setText(1, "I finished the mobile layout on Monday."));
        QVERIFY(!t.setText(1, "I finished the mobile layout on Monday."));  // unchanged
        QVERIFY(t.save(path));
        Transcript back;
        QVERIFY(back.load(path));
        QCOMPARE(back.segments[1].text, QString("I finished the mobile layout on Monday."));
        QVERIFY(back.segments[1].edited);
        Transcript original;
        QVERIFY(original.load(dir.filePath("transcript.original.json")));
        QCOMPARE(original.segments[1].text, QString("I finished the mobile layout yesterday."));
    }

    void findsTheLineAtATime()
    {
        const Transcript t = sample();
        QCOMPARE(t.indexAt(0.5), -1);
        QCOMPARE(t.indexAt(4.5), 1);
        QCOMPARE(t.indexAt(9999), 3);
    }

    void exportsSubtitles()
    {
        ExportInput in;
        in.title = "Weekly";
        in.transcript = sample();
        const QString srt = exportMeeting(in, ExportFormat::Srt);
        QVERIFY(srt.startsWith("1\n00:00:01,000 --> 00:00:03,500\nMeeting: Good morning"));
        QVERIFY(srt.contains("01:02:05,500 --> 01:02:09,000\nYou: I will send"));
        const QString vtt = exportMeeting(in, ExportFormat::WebVtt);
        QVERIFY(vtt.startsWith("WEBVTT\n\n00:00:01.000 --> 00:00:03.500\n<v Meeting>Good morning"));
        const QString md = exportMeeting(in, ExportFormat::Markdown);
        QVERIFY(md.startsWith("# Weekly"));
        QVERIFY(md.contains("**01:02:05 · You**  \nI will send"));
    }

    void rendersNotesWithTimeLinksAndSaysWhenNothingWasFound()
    {
        MeetingNotes n;
        n.data = QJsonObject{
            {"summary", "We reviewed the *redesign*."},
            {"action_items", QJsonArray{QJsonObject{{"task", "Prepare documentation"}, {"owner", "Maria"},
                                                    {"due", "Friday"}, {"start", 8.0}}}},
        };
        const QString md = notesToMarkdown(n);
        QVERIFY(md.contains("We reviewed the \\*redesign\\*."));  // model text cannot inject markup
        QVERIFY(md.contains("- [ ] Prepare documentation — **Maria** · due Friday [0:08](afterline:t=8.00)"));
        QVERIFY(md.contains("No decisions were recorded."));
        QCOMPARE(parseTimeLink("afterline:t=8.00"), 8.0);
        QCOMPARE(parseTimeLink("https://example.com"), -1.0);
        QVERIFY(!notesToMarkdown(n, false).contains("afterline:"));
    }

    void dropsNotesWithoutValidCitations()
    {
        const auto all = lines();
        std::vector<notes::Line> section(all.begin(), all.begin() + 3);
        const QJsonObject raw{
            {"summary", "Review."},
            {"key_points", QJsonArray{
                QJsonObject{{"text", "Mobile layout is done"}, {"sources", QJsonArray{1}}},
                QJsonObject{{"text", "Budget was approved"}, {"sources", QJsonArray{42}}},  // no such line
                QJsonObject{{"text", "Unsupported"}, {"sources", QJsonArray{}}},
            }},
            {"decisions", QJsonArray{}},
            {"action_items", QJsonArray{
                QJsonObject{{"task", "Prepare release documentation"}, {"owner", "Maria"}, {"due", "Friday"},
                            {"sources", QJsonArray{2}}},
                // Invented owner and deadline on a real task.
                QJsonObject{{"task", "Finish mobile layout"}, {"owner", "John"}, {"due", "next Tuesday"},
                            {"sources", QJsonArray{1, 1, 99}}},
            }},
            {"open_questions", QJsonArray{}},
            {"dates", QJsonArray{
                QJsonObject{{"date", "Friday"}, {"text", "Documentation due"}, {"sources", QJsonArray{2}}},
                QJsonObject{{"date", "June 3rd"}, {"text", "Launch"}, {"sources", QJsonArray{0}}},  // never said
            }},
        };
        notes::Stats stats;
        const QJsonObject out = notes::validateSection(raw, section, all, &stats);
        QCOMPARE(out["key_points"].toArray().size(), 1);
        QCOMPARE(out["action_items"].toArray().size(), 2);
        const QJsonObject maria = out["action_items"].toArray()[0].toObject();
        QCOMPARE(maria["owner"].toString(), QString("Maria"));
        QCOMPARE(maria["due"].toString(), QString("Friday"));
        const QJsonObject layout = out["action_items"].toArray()[1].toObject();
        QCOMPARE(layout["owner"].toString(), QString());
        QCOMPARE(layout["due"].toString(), QString());
        QCOMPARE(layout["sources"].toArray(), QJsonArray{1});  // duplicates and unknown ids removed
        QCOMPARE(out["dates"].toArray().size(), 1);
        QCOMPARE(stats.dropped, 3);
        QCOMPARE(stats.ownersCleared, 1);
        QCOMPARE(stats.datesCleared, 1);
    }

    void acceptsYouAsOwnerOnlyForTheMicrophone()
    {
        const auto all = lines();
        const QJsonObject raw{{"action_items", QJsonArray{
            QJsonObject{{"task", "Send screenshots"}, {"owner", "You"}, {"due", ""}, {"sources", QJsonArray{3}}},
            QJsonObject{{"task", "Prepare docs"}, {"owner", "You"}, {"due", ""}, {"sources", QJsonArray{2}}},
        }}};
        const QJsonObject out = notes::validateSection(raw, all, all, nullptr);
        QCOMPARE(out["action_items"].toArray()[0].toObject()["owner"].toString(), QString("You"));
        QCOMPARE(out["action_items"].toArray()[1].toObject()["owner"].toString(), QString());
    }

    void mergesDuplicatesAcrossSections()
    {
        const QJsonObject a{{"summary", "A"},
                            {"decisions", QJsonArray{QJsonObject{{"text", "Keep the existing navigation"},
                                                                 {"sources", QJsonArray{3}}}}}};
        const QJsonObject b{{"summary", "B"},
                            {"decisions", QJsonArray{QJsonObject{{"text", "keep existing navigation"},
                                                                 {"sources", QJsonArray{40}}},
                                                     QJsonObject{{"text", "Launch on March 20"},
                                                                 {"sources", QJsonArray{41}}}}}};
        const QJsonObject m = notes::mergeSections({a, b});
        const QJsonArray d = m["decisions"].toArray();
        QCOMPARE(d.size(), 2);
        QCOMPARE(d[0].toObject()["sources"].toArray(), (QJsonArray{3, 40}));
        QCOMPARE(m["section_summaries"].toArray().size(), 2);
    }

    void chunksLongMeetingsWithinBudget()
    {
        std::vector<notes::Line> many;
        for (int i = 0; i < 1000; ++i)
            many.push_back({i, i * 5.0, "computer", QString("Sentence number %1 of a very long meeting.").arg(i)});
        const auto parts = notes::chunk(many, [](const QString &s) { return int(s.size() / 4); }, 2000);
        QVERIFY(parts.size() > 3);
        size_t total = 0;
        for (const auto &p : parts) {
            int used = 0;
            for (const auto &l : p)
                used += int(notes::formatLine(l).size() / 4) + 1;
            QVERIFY(used <= 2000);
            total += p.size();
        }
        QCOMPARE(total, many.size());  // nothing lost, nothing repeated
    }

    void validatesAnswers()
    {
        const QJsonObject ok = notes::validateAnswer(
            {{"found", true}, {"answer", "Keep navigation."}, {"sources", QJsonArray{2, 7}}}, 3);
        QVERIFY(ok["found"].toBool());
        QCOMPARE(ok["sources"].toArray(), QJsonArray{2});
        const QJsonObject unsupported = notes::validateAnswer(
            {{"found", true}, {"answer", "Probably yes."}, {"sources", QJsonArray{9}}}, 3);
        QVERIFY(!unsupported["found"].toBool());
    }
};

QTEST_GUILESS_MAIN(TranscriptTest)
#include "test_transcript.moc"
