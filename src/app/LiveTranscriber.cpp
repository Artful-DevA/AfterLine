// SPDX-License-Identifier: MIT
#include "app/LiveTranscriber.h"

#include "app/MeetingStore.h"
#include "app/ModelCatalog.h"
#include "app/Settings.h"
#include "audio/Recorder.h"
#include "core/Paths.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>

#include <algorithm>
#include <cmath>

namespace afterline {

LiveTranscriber::LiveTranscriber(Settings &settings, MeetingStore &store, QObject *parent)
    : QObject(parent), m_settings(settings), m_store(store)
{
}

LiveTranscriber::~LiveTranscriber() { stop(); }

QString LiveTranscriber::unavailableReason() const
{
    if (!m_settings.liveTranscript())
        return tr("Live transcript is turned off in Settings.");
    const ModelSpec *speech = findModel(m_settings.speechModel());
    if (!speech || !speech->installed())
        return tr("Download a speech recognition model in Settings → AI models to see the transcript live.");
    if (!QFileInfo(enginePath("transcriber")).isExecutable())
        return tr("The transcription engine is missing from this installation.");
    return {};
}

void LiveTranscriber::start(const QString &meetingId)
{
    stop();
    m_lines.clear();
    m_meetingId = meetingId;
    m_ready = false;
    if (!unavailableReason().isEmpty()) {
        Q_EMIT stateChanged();
        return;
    }
    const MeetingInfo m = m_store.get(meetingId);
    const ModelSpec *speech = findModel(m_settings.speechModel());
    QStringList args{"--live", "--model", speech->path(), "--language", m.language.isEmpty() ? m_settings.language()
                                                                                             : m.language};
    const ModelSpec *vad = findModel(recommendedModel(ModelKind::SpeechDetector));
    if (vad && vad->installed())
        args << "--vad" << vad->path();
    // Two threads: a steady, light load instead of bursts on every core,
    // which keeps the fans quiet during the call. Still faster than real
    // time on an ordinary laptop.
    const int threads = std::min(2, std::max(1, QThread::idealThreadCount() - 1));
    args << "--threads" << QString::number(threads);
    args << "--track" << "computer=" + m.path(audio::Recorder::kComputerFile);
    if (!m.microphoneLabel.isEmpty())
        args << "--track" << "microphone=" + m.path(audio::Recorder::kMicrophoneFile);

    auto *proc = new QProcess(this);
    proc->setProcessChannelMode(QProcess::SeparateChannels);
    proc->setStandardErrorFile(logsDir() + "/live-transcript.log");
    connect(proc, &QProcess::readyReadStandardOutput, this, [this, proc] {
        m_buffer += proc->readAllStandardOutput();
        int nl;
        while ((nl = m_buffer.indexOf('\n')) >= 0) {
            const QByteArray line = m_buffer.left(nl);
            m_buffer.remove(0, nl + 1);
            onLine(line);
        }
    });
    connect(proc, &QProcess::finished, this, [this, proc] {
        if (m_proc == proc) {
            m_proc = nullptr;
            m_ready = false;
            Q_EMIT stateChanged();
        }
        proc->deleteLater();
    });
    m_proc = proc;
    proc->start(enginePath("transcriber"), args);
    Q_EMIT stateChanged();
}

void LiveTranscriber::stop()
{
    if (!m_proc)
        return;
    // Keep what was transcribed live: the meeting shows it until the full
    // transcript is ready.
    if (!m_lines.isEmpty()) {
        const MeetingInfo m = m_store.get(m_meetingId);
        QJsonArray segments;
        for (int i = 0; i < m_lines.size(); ++i)
            segments.append(QJsonObject{{"id", i}, {"start", m_lines[i].start}, {"end", m_lines[i].end},
                                        {"source", m_lines[i].source}, {"text", m_lines[i].text}});
        if (m.isValid()) {
            QFile f(m.path("live-transcript.json"));
            if (f.open(QIODevice::WriteOnly))
                f.write(QJsonDocument(QJsonObject{{"version", 1}, {"preliminary", true}, {"segments", segments}})
                            .toJson(QJsonDocument::Compact));
        }
    }
    QProcess *proc = m_proc;
    m_proc = nullptr;
    m_ready = false;
    proc->disconnect(this);
    proc->terminate();
    if (!proc->waitForFinished(1500))
        proc->kill();
    proc->deleteLater();
    m_buffer.clear();
    Q_EMIT stateChanged();
}

void LiveTranscriber::onLine(const QByteArray &line)
{
    const QJsonObject e = QJsonDocument::fromJson(line).object();
    const QString type = e["type"].toString();
    if (type == "ready") {
        m_ready = true;
        Q_EMIT stateChanged();
    } else if (type == "segment") {
        LiveLine l{e["start"].toDouble(), e["end"].toDouble(), e["source"].toString(), e["text"].toString()};
        // Keep the lines in time order across both tracks.
        auto it = std::upper_bound(m_lines.begin(), m_lines.end(), l.start,
                                   [](double t, const LiveLine &x) { return t < x.start; });
        m_lines.insert(it, l);
        Q_EMIT lineAdded(l);
    } else if (type == "retract") {
        // A line that turned out to be the call's audio heard by the mic.
        const QString source = e["source"].toString();
        const double start = e["start"].toDouble();
        const auto before = m_lines.size();
        m_lines.erase(std::remove_if(m_lines.begin(), m_lines.end(),
                                     [&](const LiveLine &l) { return l.source == source && std::abs(l.start - start) < 0.05; }),
                      m_lines.end());
        if (m_lines.size() != before)
            Q_EMIT linesChanged();
    } else if (type == "partial") {
        Q_EMIT partial(e["source"].toString(), e["text"].toString());
    }
}

}  // namespace afterline
