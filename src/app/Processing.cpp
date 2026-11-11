// SPDX-License-Identifier: MIT
#include "app/Processing.h"

#include "app/ModelCatalog.h"
#include "audio/Recorder.h"
#include "core/Json.h"
#include "core/Notes.h"
#include "core/Paths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTimer>

#include <csignal>

namespace afterline {

Processing::Processing(MeetingStore &store, Settings &settings, QObject *parent)
    : QObject(parent), m_store(store), m_settings(settings)
{
    qRegisterMetaType<afterline::Processing::Kind>();
}

Processing::~Processing()
{
    if (m_proc) {
        // Leave the work resumable: the job state stays "running" and the
        // transcriber keeps its partial results. Disconnect first, so the
        // engine's exit is not recorded as a failure.
        m_proc->disconnect(this);
        ::kill(pid_t(m_proc->processId()), SIGCONT);
        m_proc->terminate();
        if (!m_proc->waitForFinished(3000))
            m_proc->kill();
    }
}

bool Processing::engineAvailable(Kind kind)
{
    return QFileInfo(enginePath(kind == Kind::Transcript ? "transcriber" : "summarizer")).isExecutable();
}

int Processing::threads() const { return m_settings.threads(); }

bool Processing::isQueuedOrRunning(const QString &meetingId) const
{
    if (m_proc && m_current.meetingId == meetingId)
        return true;
    for (const auto &j : m_queue)
        if (j.meetingId == meetingId)
            return true;
    return false;
}

void Processing::setState(const QString &meetingId, Kind kind, const QString &state, const QString &error)
{
    if (kind == Kind::Ask)
        return;
    MeetingInfo m = m_store.get(meetingId);
    if (!m.isValid())
        return;
    if (kind == Kind::Transcript)
        m.transcriptState = state;
    else
        m.notesState = state;
    if (!error.isEmpty() || state == "done")
        m.lastError = error;
    m_store.save(m);
}

bool Processing::transcribe(const QString &meetingId, QString *error)
{
    const MeetingInfo m = m_store.get(meetingId);
    const ModelSpec *speech = findModel(m_settings.speechModel());
    if (!m.isValid() || !m.hasAudio()) {
        if (error)
            *error = tr("This meeting has no audio to transcribe.");
        return false;
    }
    if (!speech || !speech->installed()) {
        if (error)
            *error = tr("Download a speech recognition model first (Settings → AI models).");
        return false;
    }
    if (!engineAvailable(Kind::Transcript)) {
        if (error)
            *error = tr("The transcription engine is missing from this installation. Reinstall AfterLine.");
        return false;
    }
    if (isQueuedOrRunning(meetingId)) {
        if (error)
            *error = tr("This meeting is already being processed.");
        return false;
    }
    Job job;
    job.kind = Kind::Transcript;
    job.meetingId = meetingId;
    job.output = m.path("transcript.json");
    job.args << "--model" << speech->path() << "--output" << job.output << "--quality" << m_settings.quality();
    const ModelSpec *vad = findModel(recommendedModel(ModelKind::SpeechDetector));
    if (vad && vad->installed())
        job.args << "--vad" << vad->path();
    const QString language = m.language.isEmpty() ? m_settings.language() : m.language;
    job.args << "--language" << language;
    if (threads() > 0)
        job.args << "--threads" << QString::number(threads());
    if (m.hasComputerAudio())
        job.args << "--track" << "computer=" + m.path(audio::Recorder::kComputerFile);
    if (m.hasMicrophoneAudio())
        job.args << "--track" << "microphone=" + m.path(audio::Recorder::kMicrophoneFile);
    if (QFileInfo::exists(job.output + ".partial"))
        job.args << "--resume";
    enqueue(job);
    return true;
}

bool Processing::writeNotes(const QString &meetingId, QString *error)
{
    const MeetingInfo m = m_store.get(meetingId);
    const ModelSpec *model = findModel(m_settings.notesModel());
    if (!m.isValid() || !m.hasTranscript()) {
        if (error)
            *error = tr("Notes are written from the transcript. Transcribe the meeting first.");
        return false;
    }
    if (m.transcriptIsEmpty()) {
        if (error)
            *error = tr("No speech was found in this recording, so there is nothing to write notes about.");
        return false;
    }
    if (!model || !model->installed()) {
        if (error)
            *error = tr("Download a notes model first (Settings → AI models).");
        return false;
    }
    if (!engineAvailable(Kind::Notes)) {
        if (error)
            *error = tr("The notes engine is missing from this installation. Reinstall AfterLine.");
        return false;
    }
    if (isQueuedOrRunning(meetingId)) {
        if (error)
            *error = tr("This meeting is already being processed.");
        return false;
    }
    Job job;
    job.kind = Kind::Notes;
    job.meetingId = meetingId;
    job.output = m.path("notes.json");
    job.args << "--model" << model->path() << "--transcript" << m.path("transcript.json") << "--output" << job.output;
    if (!m.titleIsAuto)
        job.args << "--title" << m.title;
    if (threads() > 0)
        job.args << "--threads" << QString::number(threads());
    enqueue(job);
    return true;
}

bool Processing::ask(const QString &requestId, const QString &question, const QStringList &meetingIds, QString *error)
{
    const ModelSpec *model = findModel(m_settings.notesModel());
    if (!model || !model->installed()) {
        if (error)
            *error = tr("Ask AfterLine uses the notes model. Download it in Settings → AI models.");
        return false;
    }
    const auto hits = m_store.passagesFor(question, 12, meetingIds);
    QJsonArray passages;
    // What the notes concluded (decisions, action items with their owner,
    // open questions, dates) comes first: it is the meeting's own summary
    // of who does what, and keeps answers from taking a quote literally.
    QStringList noteMeetings = meetingIds;
    for (const auto &h : hits)
        if (!noteMeetings.contains(h.meetingId) && noteMeetings.size() < 4)
            noteMeetings << h.meetingId;
    for (const QString &id : noteMeetings) {
        const MeetingInfo m = m_store.get(id);
        if (!m.isValid() || !QFile::exists(m.path("notes.json")))
            continue;
        const QJsonObject n = readJsonFile(m.path("notes.json"));
        auto add = [&](const QString &text, double start) {
            passages.append(QJsonObject{{"meetingId", m.id}, {"meetingTitle", m.title},
                                        {"date", m.created.date().toString(Qt::ISODate)}, {"start", start},
                                        {"text", text}});
        };
        for (const auto v : n["decisions"].toArray())
            add(tr("[Meeting notes] Decision: %1").arg(v.toObject()["text"].toString()), v.toObject()["start"].toDouble());
        for (const auto v : n["action_items"].toArray()) {
            const QJsonObject o = v.toObject();
            QString t = tr("[Meeting notes] Action item for %1: %2")
                            .arg(o["owner"].toString().isEmpty() ? tr("someone") : o["owner"].toString(),
                                 o["task"].toString());
            if (!o["due"].toString().isEmpty())
                t += tr(" (due %1)").arg(o["due"].toString());
            add(t, o["start"].toDouble());
        }
        for (const auto v : n["open_questions"].toArray())
            add(tr("[Meeting notes] Open question: %1").arg(v.toObject()["text"].toString()), v.toObject()["start"].toDouble());
        for (const auto v : n["dates"].toArray())
            add(tr("[Meeting notes] Date: %1 — %2").arg(v.toObject()["date"].toString(), v.toObject()["text"].toString()),
                v.toObject()["start"].toDouble());
    }
    for (const auto &h : hits)
        passages.append(QJsonObject{{"meetingId", h.meetingId}, {"meetingTitle", h.meetingTitle},
                                    {"date", h.created.date().toString(Qt::ISODate)}, {"start", h.start},
                                    {"text", h.text}});
    if (passages.isEmpty()) {
        // Nothing to ground an answer on: say so without running the model.
        QTimer::singleShot(0, this, [this, requestId, question] {
            Q_EMIT answerReady(requestId,
                               {{"found", false}, {"question", question},
                                {"answer", tr("Your meetings don't mention this. Try different words, or check "
                                              "that the meetings have been transcribed.")},
                                {"sources", QJsonArray()}},
                               {});
        });
        return true;
    }
    Job job;
    job.kind = Kind::Ask;
    job.meetingId = requestId;
    auto *tmp = new QTemporaryDir(QDir::tempPath() + "/afterline-ask-XXXXXX");
    job.tempDir = tmp->path();
    tmp->setAutoRemove(false);
    delete tmp;
    QFile f(job.tempDir + "/passages.json");
    if (f.open(QIODevice::WriteOnly))
        f.write(QJsonDocument(passages).toJson());
    f.close();
    job.output = job.tempDir + "/answer.json";
    job.args << "--model" << model->path() << "--ask" << question << "--passages" << f.fileName() << "--output"
             << job.output << "--ctx" << "6144";
    if (threads() > 0)
        job.args << "--threads" << QString::number(threads());
    // Questions are interactive: ahead of background work.
    m_queue.prepend(job);
    Q_EMIT queueChanged();
    // Store the passages so the UI can resolve citations.
    m_lastPassages[requestId] = passages;
    startNext();
    return true;
}

QJsonArray Processing::passagesOf(const QString &requestId) const { return m_lastPassages.value(requestId); }

void Processing::enqueue(const Job &job)
{
    m_queue.enqueue(job);
    setState(job.meetingId, job.kind, "queued");
    Q_EMIT queueChanged();
    startNext();
}

void Processing::startNext()
{
    // Questions may run during a recording (the person asked); background
    // work waits until the recording ends.
    if (m_proc || m_queue.isEmpty())
        return;
    if (m_recording && m_queue.head().kind != Kind::Ask)
        return;
    m_current = m_queue.dequeue();
    m_buffer.clear();
    m_error.clear();
    m_stage.clear();
    m_progress = 0;
    m_cancelled = false;
    m_paused = false;

    auto *proc = new QProcess(this);
    m_proc = proc;
    proc->setProcessChannelMode(QProcess::SeparateChannels);
    // Engine diagnostics go to a log the user can attach to a bug report.
    proc->setStandardErrorFile(logsDir() + "/engines.log", QIODevice::Append);
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc] {
        m_buffer += proc->readAllStandardOutput();
        int nl;
        while ((nl = m_buffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_buffer.left(nl);
            m_buffer.remove(0, nl + 1);
            onLine(line);
        }
    });
    connect(proc, &QProcess::finished, this, &Processing::onExit);
    connect(proc, &QProcess::errorOccurred, this, [this, proc](QProcess::ProcessError e) {
        if (e == QProcess::FailedToStart) {
            m_error = tr("The engine could not be started: %1").arg(proc->errorString());
            onExit(-1, QProcess::CrashExit);
        }
    });
    const QString engine = enginePath(m_current.kind == Kind::Transcript ? "transcriber" : "summarizer");
    {
        QFile log(logsDir() + "/engines.log");
        if (log.size() > 4 * 1024 * 1024)
            log.resize(0);
        if (log.open(QIODevice::Append))
            log.write(QString("\n== %1 %2\n").arg(QDateTime::currentDateTime().toString(Qt::ISODate), engine).toUtf8());
    }
    setState(m_current.meetingId, m_current.kind, "running");
    Q_EMIT started(m_current.meetingId, m_current.kind);
    proc->start(engine, m_current.args);
}

void Processing::onLine(const QByteArray &line)
{
    const QJsonObject e = QJsonDocument::fromJson(line).object();
    const QString type = e["type"].toString();
    if (type == "progress") {
        m_progress = e["fraction"].toDouble();
        m_stage = e["stage"].toString();
        Q_EMIT progress(m_current.meetingId, m_current.kind, m_progress, m_stage);
    } else if (type == "error") {
        m_error = e["message"].toString();
    }
}

void Processing::onExit(int code, QProcess::ExitStatus status)
{
    QProcess *proc = m_proc;
    if (!proc)
        return;
    m_proc = nullptr;
    proc->deleteLater();
    const Job job = m_current;
    const bool ok = status == QProcess::NormalExit && code == 0 && QFileInfo::exists(job.output);
    QString error = m_error;
    if (!ok && error.isEmpty()) {
        if (m_cancelled)
            error = tr("Cancelled.");
        else if (status == QProcess::CrashExit)
            error = tr("The %1 engine stopped unexpectedly, possibly because the computer ran out of memory. "
                       "Your recording is safe. Close other applications and try again, or choose a smaller "
                       "model in Settings → AI models.")
                        .arg(job.kind == Kind::Transcript ? tr("transcription") : tr("notes"));
        else
            error = tr("Processing failed (code %1).").arg(code);
    }

    if (job.kind == Kind::Ask) {
        QJsonObject answer;
        if (ok)
            answer = readJsonFile(job.output);
        QDir(job.tempDir).removeRecursively();
        Q_EMIT answerReady(job.meetingId, answer, ok ? QString() : error);
    } else {
        setState(job.meetingId, job.kind, ok ? "done" : (m_cancelled ? "cancelled" : "failed"), ok ? QString() : error);
        if (ok) {
            MeetingInfo m = m_store.get(job.meetingId);
            if (job.kind == Kind::Transcript) {
                const QJsonObject t = readJsonFile(job.output);
                if (m.language.isEmpty())
                    m.language = t["language"].toString();
                m_store.save(m);
                QFile::remove(m.path("live-transcript.json"));
            } else {
                const MeetingNotes n = loadNotes(job.output);
                if (m.titleIsAuto && !n.title().isEmpty()) {
                    m.title = n.title();
                    m_store.save(m);
                }
            }
            m_store.reindex(job.meetingId);
        }
        Q_EMIT finished(job.meetingId, job.kind, ok, ok ? QString() : error);
        if (ok && job.kind == Kind::Transcript && m_settings.autoNotes()) {
            const ModelSpec *notes = findModel(m_settings.notesModel());
            if (notes && notes->installed())
                writeNotes(job.meetingId);
        }
    }
    Q_EMIT queueChanged();
    startNext();
}

void Processing::cancel(const QString &meetingId)
{
    for (int i = m_queue.size() - 1; i >= 0; --i)
        if (m_queue[i].meetingId == meetingId) {
            setState(meetingId, m_queue[i].kind, "");
            m_queue.removeAt(i);
        }
    if (m_proc && m_current.meetingId == meetingId) {
        m_cancelled = true;
        ::kill(pid_t(m_proc->processId()), SIGCONT);
        m_proc->terminate();
        QPointer<QProcess> p = m_proc;
        QTimer::singleShot(5000, this, [p] {
            if (p)
                p->kill();
        });
    }
    Q_EMIT queueChanged();
}

void Processing::cancelAsk(const QString &requestId) { cancel(requestId); }

void Processing::setRecordingActive(bool active)
{
    m_recording = active;
    if (m_proc && m_current.kind != Kind::Ask) {
        // Freeze background work for the duration of the call.
        ::kill(pid_t(m_proc->processId()), active ? SIGSTOP : SIGCONT);
        m_paused = active;
        Q_EMIT progress(m_current.meetingId, m_current.kind, m_progress,
                        active ? tr("Paused while recording") : m_stage);
    }
    if (!active)
        startNext();
}

void Processing::resumeInterrupted()
{
    for (const auto &m : m_store.list()) {
        if (m.state == "recording")
            continue;
        if (m.transcriptState == "running" || m.transcriptState == "queued")
            transcribe(m.id);
        else if (m.notesState == "running" || m.notesState == "queued")
            writeNotes(m.id);
    }
}

}  // namespace afterline
