// SPDX-License-Identifier: MIT
// Meeting library: create/save/list, search (including Romanian without
// diacritics), deletion, rebuilding a lost index, and crash recovery.
#include "app/MeetingStore.h"
#include "app/Tasks.h"
#include "core/Notes.h"
#include "audio/Recorder.h"
#include "core/Json.h"
#include "core/OggOpus.h"
#include "core/Transcript.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QLockFile>
#include <QTemporaryDir>
#include <QtTest>

using namespace afterline;

class StorageTest : public QObject {
    Q_OBJECT
    QTemporaryDir tmp;
    QString root() { return tmp.filePath("meetings"); }
    QString db() { return tmp.filePath("library.sqlite"); }

    void addTranscript(const MeetingInfo &m, const QVector<QPair<double, QString>> &lines)
    {
        Transcript t;
        int id = 0;
        for (const auto &l : lines)
            t.segments.push_back({id++, l.first, l.first + 2, "computer", l.second, 1, false});
        QVERIFY(t.save(m.path("transcript.json")));
    }

private Q_SLOTS:
    void createsSavesAndLists()
    {
        MeetingStore store;
        QString err;
        QVERIFY2(store.open(root(), db(), &err), qPrintable(err));
        MeetingInfo m = store.create("Firefox", "USB microphone");
        QVERIFY(m.isValid());
        QCOMPARE(m.state, QString("recording"));
        m.state = "saved";
        m.title = "Website redesign";
        m.titleIsAuto = false;
        m.tags = {"design", "web"};
        QVERIFY(store.save(m));
        const MeetingInfo back = store.get(m.id);
        QCOMPARE(back.title, QString("Website redesign"));
        QCOMPARE(back.tags, (QStringList{"design", "web"}));
        QCOMPARE(back.sourceLabel, QString("Firefox"));
        QCOMPARE(store.list().size(), 1);
        QVERIFY(!store.get("../etc").isValid());  // ids cannot escape the library
    }

    void searchesTranscriptsAndIgnoresDiacritics()
    {
        MeetingStore store;
        QVERIFY(store.open(root(), db(), nullptr));
        MeetingInfo ro = store.create("Zoom", "");
        ro.title = "Ședința de lansare";
        ro.state = "saved";
        addTranscript(ro, {{1, "Bună dimineața. Începem ședința despre lansarea aplicației."},
                           {9, "Andrei se ocupă de site-ul web."}});
        QVERIFY(store.save(ro));
        MeetingInfo en = store.create("Firefox", "");
        en.state = "saved";
        addTranscript(en, {{2, "We keep the existing navigation."}, {20, "Maria prepares the documentation."}});
        QVERIFY(store.save(en));

        auto hits = store.search("sedinta");
        bool found = false;
        for (const auto &h : hits)
            found |= h.meetingId == ro.id && h.segmentId == 0 && h.snippet.contains("<b>");
        QVERIFY2(found, "searching without diacritics must find Romanian text");
        hits = store.search("navig");  // prefix
        QVERIFY(!hits.isEmpty());
        QCOMPARE(hits.first().meetingId, en.id);
        QCOMPARE(hits.first().start, 2.0);
        // Restricting to other meetings excludes it.
        QVERIFY(store.search("navigation", 50, {ro.id}).isEmpty());
        // Hostile input is just text.
        store.search("\"unbalanced * NEAR( OR");

        // Ask AfterLine retrieval: stop words are ignored, word forms match by prefix.
        const auto passages = store.passagesFor("What did Maria prepare?", 5);
        QCOMPARE(passages.size(), 1);
        QCOMPARE(passages.first().meetingId, en.id);
        QVERIFY(passages.first().text.contains("Others: Maria prepares the documentation."));
        QVERIFY(store.passagesFor("what was it about", 5).isEmpty());  // nothing specific to look for
    }

    void rebuildsALostIndex()
    {
        QFile::remove(db());
        QFile::remove(db() + "-wal");
        MeetingStore store;
        QVERIFY(store.open(root(), db(), nullptr));
        QCOMPARE(store.list().size(), 3);
        QVERIFY(!store.search("navigation").isEmpty());
    }

    void survivesACorruptIndex()
    {
        {
            QFile f(db());
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write("garbage that is not sqlite");
        }
        QFile::remove(db() + "-wal");
        QFile::remove(db() + "-shm");
        MeetingStore store;
        QString err;
        QVERIFY2(store.open(root(), db(), &err), qPrintable(err));
        QCOMPARE(store.list().size(), 3);
    }

    void recoversRecordingsInterruptedByACrash()
    {
        MeetingStore store;
        QVERIFY(store.open(root(), db(), nullptr));
        MeetingInfo m = store.create("Entire computer", "");
        {
            // 12 s written and flushed, then the process "dies".
            OggOpusWriter w;
            QVERIFY(w.open(m.path(audio::Recorder::kComputerFile).toStdString(), nullptr));
            std::vector<float> s(48000 * 12, 0.1f);
            QVERIFY(w.write(s.data(), s.size()));
            QVERIFY(w.flush(true));
            QFile f(m.path(audio::Recorder::kComputerFile));
            const QString copy = m.path("crashed.opus");
            QVERIFY(QFile::copy(m.path(audio::Recorder::kComputerFile), copy));
            w.close();
            QFile::remove(m.path(audio::Recorder::kComputerFile));
            QFile::rename(copy, m.path(audio::Recorder::kComputerFile));
        }
        // A meeting whose recorder is still alive must be left alone.
        MeetingInfo live = store.create("Entire computer", "");
        QLockFile lock(MeetingStore::lockFile(live));
        QVERIFY(lock.tryLock());

        const QStringList recovered = store.recoverInterrupted();
        QCOMPARE(recovered, QStringList{m.id});
        const MeetingInfo r = store.get(m.id);
        QCOMPARE(r.state, QString("interrupted"));
        QVERIFY(r.duration > 11 && r.duration <= 12.01);
        QCOMPARE(r.events.size(), 1);
        QCOMPARE(store.get(live.id).state, QString("recording"));
    }

    void foldersKeepIconColourAndFollowRenames()
    {
        MeetingStore store;
        QVERIFY(store.open(root(), db(), nullptr));
        MeetingInfo a = store.create("Zoom", {});
        MeetingInfo b = store.create("Zoom", {});
        QVERIFY(store.saveFolder(FolderInfo{"Work", "briefcase", "#3F72AF"}));
        store.setFolder(a.id, "Work", true);
        QCOMPARE(store.get(a.id).folder, QString("Work"));
        QVERIFY(store.get(a.id).folderAuto);
        // A folder named only by a meeting is listed (and remembered) too.
        store.setFolder(b.id, "Clients");
        QCOMPARE(store.folders().size(), 2);
        QCOMPARE(store.folder("work").icon, QString("briefcase"));

        // Renaming moves the meetings along; deleting keeps them, unfiled.
        QVERIFY(store.saveFolder(FolderInfo{"Job", "star", "#B4553A"}, "Work"));
        QCOMPARE(store.get(a.id).folder, QString("Job"));
        QCOMPARE(store.folder("Job").icon, QString("star"));
        store.removeFolder("Job");
        QVERIFY(store.get(a.id).isValid());
        QVERIFY(store.get(a.id).folder.isEmpty());
        QVERIFY(!store.get(a.id).folderAuto);
        QCOMPARE(store.folders().size(), 1);
    }

    void collectsTasksAndRemembersTickedOnes()
    {
        MeetingStore store;
        QVERIFY(store.open(root(), db(), nullptr));
        MeetingInfo m = store.create("Zoom", "Mic");
        m.title = "Launch";
        store.save(m);
        QJsonArray items{QJsonObject{{"task", "Send the slides"}, {"owner", "You"}, {"due", "Friday"}, {"start", 12.5}},
                         QJsonObject{{"task", "Book the room"}, {"owner", "Maria"}, {"due", ""}, {"start", 30.0}}};
        QVERIFY(writeJsonFile(m.path("notes.json"), QJsonObject{{"action_items", items}}));

        auto mine = collectTasks(store, true);
        QCOMPARE(mine.size(), 1);
        QCOMPARE(mine[0].task, QString("Send the slides"));
        QCOMPARE(mine[0].due, QString("Friday"));
        QCOMPARE(mine[0].meetingTitle, QString("Launch"));
        QVERIFY(!mine[0].done);
        QCOMPARE(collectTasks(store, false).size(), 2);

        QVERIFY(setTaskDone(store, m.id, "Send the slides", true));
        QVERIFY(collectTasks(store, true)[0].done);
        QVERIFY(setTaskDone(store, m.id, "Send the slides", false));
        QVERIFY(!collectTasks(store, true)[0].done);

        // Deleting removes it from the notes, the edited notes and the ticks.
        QVERIFY(setTaskDone(store, m.id, "Book the room", true));
        QFile md(m.path("notes.md"));
        QVERIFY(md.open(QIODevice::WriteOnly));
        md.write("## Action items\n\n- [ ] Send the slides (You)\n- [x] Book the room (Maria)\n");
        md.close();
        QVERIFY(deleteTask(store, m.id, "Book the room"));
        const auto left = collectTasks(store, false);
        QCOMPARE(left.size(), 1);
        QCOMPARE(left[0].task, QString("Send the slides"));
        QVERIFY(md.open(QIODevice::ReadOnly));
        const QString edited = QString::fromUtf8(md.readAll());
        QVERIFY(!edited.contains("Book the room"));
        QVERIFY(edited.contains("Send the slides"));
        QVERIFY(!loadDoneTasks(m.path("tasks.json")).contains("Book the room"));
        QVERIFY(!deleteTask(store, m.id, "Book the room"));
    }

    void recognisesQuestionsAboutTasks()
    {
        for (const char *q : {"What do I need to do?", "Do I have tasks for next meeting?", "my action items",
                              "What do I owe people", "any follow-ups for me?", "Who is responsible for the docs?"})
            QVERIFY2(isTaskQuestion(q), q);
        for (const char *q : {"What did we decide about the website?", "When is the launch?", "Summarize the call"})
            QVERIFY2(!isTaskQuestion(q), q);
    }

    void deletesMeetings()
    {
        MeetingStore store;
        QVERIFY(store.open(root(), db(), nullptr));
        const int before = store.list().size();
        const QString id = store.list().first().id;
        QString err;
        QVERIFY2(store.remove(id, &err), qPrintable(err));
        QCOMPARE(store.list().size(), before - 1);
        QVERIFY(!store.get(id).isValid());
        QVERIFY(!store.remove(id, &err));
    }
};

QTEST_GUILESS_MAIN(StorageTest)
#include "test_storage.moc"
