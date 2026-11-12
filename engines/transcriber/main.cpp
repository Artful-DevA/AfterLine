// SPDX-License-Identifier: MIT
//
// afterline-transcriber: offline speech recognition for one meeting.
//
//   afterline-transcriber --model ggml-small-q5_1.bin [--vad ggml-silero.bin]
//       --track computer=/path/computer.opus --track microphone=/path/mic.opus
//       --output transcript.json [--language auto|en|ro|...] [--threads N]
//       [--quality fast|balanced|accurate] [--resume]
//
// Long recordings are processed in chunks of about five minutes that end in
// the quietest moment near the boundary (never mid-word), so memory stays
// bounded for a meeting of any length. After every chunk the progress is
// saved, so a cancelled or crashed run resumes where it stopped.
#include "Protocol.h"
#include "core/OggOpus.h"
#include "core/TextCleanup.h"

#include <ggml-backend.h>
#include <whisper.h>

#include <QCoreApplication>
#include <QElapsedTimer>
#include <QThread>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

#include <algorithm>
#include <cmath>
#include <map>

#include <csignal>
#ifdef __linux__
#include <sys/prctl.h>
#endif
#ifndef _WIN32
#include <unistd.h>
#endif

using namespace engine;

namespace {

struct Track {
    QString source;  // "computer" or "microphone"
    QString path;
    double seconds = 0;
};

struct Segment {
    double start, end;
    QString source, text, language;
    double confidence;
};

constexpr int kRate = 16000;
constexpr double kChunkSeconds = 300;

double rmsOf(const std::vector<float> &v, size_t from, size_t to)
{
    double s = 0;
    for (size_t i = from; i < to; ++i)
        s += double(v[i]) * v[i];
    return to > from ? std::sqrt(s / double(to - from)) : 0;
}

// The quietest 400 ms in the last 20 s before `end`: a safe place to cut.
double quietBoundary(const QString &path, double end)
{
    const double from = std::max(0.0, end - 20);
    const auto pcm = afterline::decodeOggOpus(path.toStdString(), kRate, nullptr, from, end - from);
    const size_t win = kRate * 2 / 5, hop = kRate / 10;
    if (pcm.size() < win)
        return end;
    double best = 1e9;
    size_t bestAt = pcm.size() - win;
    for (size_t i = 0; i + win <= pcm.size(); i += hop) {
        const double e = rmsOf(pcm, i, i + win);
        if (e < best * 0.95) {  // prefer later positions on near-ties
            best = e;
            bestAt = i;
        }
    }
    return from + double(bestAt + win / 2) / kRate;
}

// Whisper's well-known inventions on silence or noise.
bool isNoise(const QString &text, float noSpeechProb)
{
    static const QRegularExpression bracketed(R"(^[\[\(\*♪].*[\]\)\*♪]$)");
    const QString t = text.trimmed();
    if (t.isEmpty() || bracketed.match(t).hasMatch())
        return true;
    if (noSpeechProb > 0.75f && t.size() < 40)
        return true;
    return false;
}

QString normalise(const QString &text)
{
    QString out;
    for (const QChar c : text.toLower())
        if (c.isLetterOrNumber() || c.isSpace())
            out += c;
    return out.simplified();
}

// ---------------------------------------------------------------------------
// Echo gate. With speakers instead of headphones the call reaches the
// microphone a few tens of milliseconds later and quieter. The computer track
// is a perfect reference for it, so wherever the microphone is no louder than
// that echo would be, it is muted for recognition (only for recognition: the
// recording itself is never changed). Your own speech is much louder relative
// to the echo, so it passes, including when you talk over someone.
constexpr int kFrame = kRate / 10;  // 100 ms

std::vector<float> frameLevels(const QString &path)
{
    std::vector<float> out;
    afterline::OggOpusReader reader;
    if (!reader.open(path.toStdString(), kRate, nullptr))
        return out;
    std::vector<float> buf(kFrame);
    for (;;) {
        const size_t n = reader.read(buf.data(), buf.size());
        if (n == 0)
            break;
        out.push_back(float(rmsOf(buf, 0, n)));
    }
    return out;
}

std::vector<bool> echoMask(const std::vector<float> &mic, const std::vector<float> &computer)
{
    const size_t n = std::min(mic.size(), computer.size());
    std::vector<bool> mask(mic.size(), false);
    if (n < 50)
        return mask;
    // Delay of the echo (speaker output + room + input buffering), 0-500 ms,
    // from the correlation of the two level envelopes.
    size_t lag = 0;
    double bestCorr = -1;
    for (size_t L = 0; L <= 5; ++L) {
        double xy = 0, xx = 0, yy = 0;
        for (size_t j = L; j < n; ++j) {
            xy += double(mic[j]) * computer[j - L];
            xx += double(mic[j]) * mic[j];
            yy += double(computer[j - L]) * computer[j - L];
        }
        const double c = (xx > 0 && yy > 0) ? xy / std::sqrt(xx * yy) : 0;
        if (c > bestCorr) {
            bestCorr = c;
            lag = L;
        }
    }
    auto callAt = [&](size_t j, int offset) -> float {
        const long k = long(j) - long(lag) + offset;
        return (k >= 0 && size_t(k) < n) ? computer[size_t(k)] : 0.f;
    };
    std::vector<float> ratios;
    std::vector<float> near(n, 0.f);
    for (size_t j = 0; j < n; ++j) {
        near[j] = std::max({callAt(j, -1), callAt(j, 0), callAt(j, 1)});
        const float c = callAt(j, 0);
        if (c > 0.02f)
            ratios.push_back(mic[j] / c);
    }
    if (ratios.size() < 30)
        return mask;  // not enough call audio to learn the echo level
    // Most moments with call audio are echo only (you are listening), so a
    // low percentile of mic/call is the echo's level.
    std::nth_element(ratios.begin(), ratios.begin() + long(ratios.size() * 3 / 10), ratios.end());
    const float echoGain = ratios[ratios.size() * 3 / 10];
    std::fprintf(stderr, "echo gate: delay %zu00 ms, echo gain %.3f (correlation %.2f)\n", lag, echoGain, bestCorr);
    if (echoGain < 0.02f)
        return mask;  // headphones: there is no echo to remove
    std::vector<bool> echo(n, false);
    for (size_t j = 0; j < n; ++j)
        echo[j] = near[j] > 0.005f && mic[j] < 2.5f * echoGain * near[j];
    // Decide per stretch of sound (frames above the noise floor separated by
    // 300 ms of quiet), not per frame: muting individual frames leaves
    // fragments the recogniser turns into invented words.
    size_t j = 0;
    while (j < n) {
        if (mic[j] < 0.004f) {
            ++j;
            continue;
        }
        size_t end = j, quiet = 0;
        while (end < n && quiet < 3) {
            quiet = mic[end] < 0.004f ? quiet + 1 : 0;
            ++end;
        }
        int echoFrames = 0, loud = 0;
        for (size_t k = j; k < end; ++k)
            if (mic[k] >= 0.004f) {
                ++loud;
                echoFrames += echo[k];
            }
        const bool mostlyEcho = loud > 0 && echoFrames >= 0.6 * loud;
        for (size_t k = j; k < end; ++k)
            mask[k] = mostlyEcho || echo[k];
        j = end;
    }
    std::fprintf(stderr, "echo gate: %ld of %zu frames muted\n", long(std::count(mask.begin(), mask.end(), true)), mask.size());
    return mask;
}

void applyMask(std::vector<float> &pcm, const std::vector<bool> &mask, double startSeconds)
{
    const size_t first = size_t(startSeconds * 10 + 0.5);
    for (size_t i = 0; i < pcm.size(); ++i) {
        const size_t j = first + i / kFrame;
        if (j < mask.size() && mask[j])
            pcm[i] = 0.f;
    }
}

QStringList wordsOf(const QString &text) { return normalise(text).split(' ', Qt::SkipEmptyParts); }

// Speakers (not headphones) let the call leak into the microphone, so the
// same words appear on both tracks. A microphone line is treated as echo when
// nearly all of its words were said on the computer track at the same moment;
// it is then dropped and the computer-track copy kept. Lines where you added
// words of your own survive.
int removeEcho(std::vector<Segment> &segments)
{
    std::vector<Segment> computer;
    for (const auto &s : segments)
        if (s.source == "computer")
            computer.push_back(s);
    if (computer.empty())
        return 0;
    const auto before = segments.size();
    segments.erase(std::remove_if(segments.begin(), segments.end(), [&](const Segment &m) {
        if (m.source != "microphone")
            return false;
        const QStringList mic = wordsOf(m.text);
        if (mic.isEmpty())
            return true;
        QSet<QString> heard;
        for (const auto &c : computer)
            if (c.start < m.end + 1.5 && m.start < c.end + 1.5)
                for (const auto &w : wordsOf(c.text))
                    heard.insert(w);
        int matched = 0;
        for (const auto &w : mic)
            matched += heard.contains(w);
        return double(matched) / double(mic.size()) >= 0.8;
    }), segments.end());
    return int(before - segments.size());
}

// Joins fragments the recogniser split mid-sentence ("I" + "will send...").
void mergeFragments(std::vector<Segment> &segments)
{
    std::vector<Segment> out;
    for (const auto &s : segments) {
        if (!out.empty()) {
            Segment &p = out.back();
            const QString pt = p.text.trimmed();
            const bool open = !pt.isEmpty() && !QString(".?!").contains(pt.back());
            if (p.source == s.source && open && s.start - p.end < 1.2 && wordsOf(pt).size() <= 3) {
                QString next = s.text.trimmed();
                if (!next.isEmpty() && pt.size() <= 3)  // "I" + "Will send" -> "I will send"
                    next[0] = next[0].toLower();
                p.text = pt + ' ' + next;
                p.end = std::max(p.end, s.end);
                p.confidence = (p.confidence + s.confidence) / 2;
                continue;
            }
        }
        out.push_back(s);
    }
    segments.swap(out);
}

QJsonObject segmentJson(const Segment &s, int id)
{
    return {{"id", id},
            {"start", std::round(s.start * 100) / 100},
            {"end", std::round(s.end * 100) / 100},
            {"source", s.source},
            {"text", s.text},
            {"language", s.language},
            {"confidence", std::round(s.confidence * 1000) / 1000}};
}

Segment segmentFrom(const QJsonObject &o)
{
    return {o["start"].toDouble(), o["end"].toDouble(), o["source"].toString(), o["text"].toString(),
            o["language"].toString(), o["confidence"].toDouble()};
}

struct ProgressState {
    double done = 0, total = 1, chunk = 0;
    QString stage;
};

bool onAbort(void *) { return g_abort.load(); }

void quietLog(ggml_log_level level, const char *text, void *)
{
    if (level >= GGML_LOG_LEVEL_WARN)
        std::fputs(text, stderr);
}

// Splits whisper's segments (often a whole 30 s window) into sentences with
// their own start and end times, using token timestamps, so a click on a
// transcript line lands on that sentence.
void appendSentences(whisper_context *ctx, double offset, double limit, const QString &source,
                     const QString &language, std::vector<Segment> &out, std::map<QString, int> &votes)
{
    const whisper_token eot = whisper_token_eot(ctx);
    const int n = whisper_full_n_segments(ctx);
    for (int i = 0; i < n; ++i) {
        const float nsp = whisper_full_get_segment_no_speech_prob(ctx, i);
        QByteArray text;
        double t0 = -1, t1 = 0, p = 0;
        int count = 0;
        auto flush = [&]() {
            int looped = 0;
            const QString sentence = afterline::collapseRepetition(QString::fromUtf8(text).trimmed(), &looped);
            const double d0 = offset + t0 / 100.0, d1 = std::min(limit, offset + t1 / 100.0);
            const bool invented = looped > 0 && (afterline::implausiblyFast(sentence, d1 - d0) ||
                                                 looped > 2 * int(sentence.split(' ', Qt::SkipEmptyParts).size()));
            if (!isNoise(sentence, nsp) && t0 >= 0 && !invented) {
                const double s0 = d0;
                const double s1 = std::min(limit, std::max(s0, offset + t1 / 100.0));
                out.push_back({s0, s1, source, sentence, language, count ? p / count : 0});
                votes[language] += int(sentence.size());
            }
            text.clear();
            t0 = -1;
            p = 0;
            count = 0;
        };
        const int tokens = whisper_full_n_tokens(ctx, i);
        for (int k = 0; k < tokens; ++k) {
            if (whisper_full_get_token_id(ctx, i, k) >= eot)
                continue;
            const QByteArray piece = whisper_full_get_token_text(ctx, i, k);
            if (t0 < 0)
                t0 = double(whisper_full_get_token_t0(ctx, i, k));
            t1 = double(whisper_full_get_token_t1(ctx, i, k));
            text += piece;
            p += whisper_full_get_token_p(ctx, i, k);
            ++count;
            const QByteArray trimmed = piece.trimmed();
            const bool sentenceEnd = !trimmed.isEmpty() && QByteArray(".?!").contains(trimmed.back());
            const bool next_starts_word = k + 1 < tokens &&
                                          QByteArray(whisper_full_get_token_text(ctx, i, k + 1)).startsWith(' ');
            if ((sentenceEnd && text.size() >= 12 && (k + 1 >= tokens || next_starts_word)) ||
                (text.size() > 240 && next_starts_word))
                flush();
        }
        flush();
    }
}

// Groups detected speech into windows of up to ~28 s (whisper's natural
// unit), with a little margin so first and last syllables are kept. Returns
// sample ranges. Without a speech detector, the whole chunk is one window.
std::vector<std::pair<size_t, size_t>> speechWindows(whisper_vad_context *vad, const std::vector<float> &pcm)
{
    std::vector<std::pair<size_t, size_t>> out;
    if (!vad) {
        out.push_back({0, pcm.size()});
        return out;
    }
    whisper_vad_params vp = whisper_vad_default_params();
    vp.threshold = 0.4f;
    vp.min_speech_duration_ms = 150;
    vp.min_silence_duration_ms = 600;
    vp.speech_pad_ms = 0;
    whisper_vad_segments *segs = whisper_vad_segments_from_samples(vad, vp, pcm.data(), int(pcm.size()));
    if (!segs) {
        out.push_back({0, pcm.size()});
        return out;
    }
    const double lead = 0.6, trail = 0.4, maxWindow = 28.0, joinGap = 1.0;
    std::vector<std::pair<double, double>> regions;
    for (int i = 0; i < whisper_vad_segments_n_segments(segs); ++i) {
        const double t0 = whisper_vad_segments_get_segment_t0(segs, i) / 100.0 - lead;
        const double t1 = whisper_vad_segments_get_segment_t1(segs, i) / 100.0 + trail;
        if (!regions.empty() && t0 - regions.back().second < joinGap &&
            t1 - regions.back().first <= maxWindow)
            regions.back().second = t1;
        else if (!regions.empty() && t0 < regions.back().second)
            // Split for length: start where the previous window ended, so no
            // audio (and no sentence) is transcribed twice.
            regions.push_back({regions.back().second, t1});
        else
            regions.push_back({t0, t1});
    }
    whisper_vad_free_segments(segs);
    const double total = double(pcm.size()) / kRate;
    for (auto &r : regions) {
        const double a = std::max(0.0, r.first), b = std::min(total, r.second);
        if (b - a > 0.2)
            out.push_back({size_t(a * kRate), size_t(b * kRate)});
    }
    return out;
}

// ---------------------------------------------------------------------------
// Live mode: a preliminary transcript while the meeting is being recorded.
// The tracks are files that keep growing; every moment of speech is
// transcribed once it has ended (or once it has run on for ~25 s). The full,
// careful transcript is still made after the recording stops.

struct LiveTrack {
    Track track;
    double done = 0;  // seconds already transcribed (or known to be silent)
    std::vector<Segment> recent;
};

// Removes from a microphone line the sentences that were the call heard
// through the speakers. The two tracks are recognised separately, so the
// same sentence can come out slightly differently ("review" / "renew"):
// words are compared by their first four letters and most of a sentence
// must match. Returns true when something was removed.
bool liveStripEcho(Segment &m, const std::vector<Segment> &computer)
{
    auto stem = [](const QString &w) { return w.left(4); };
    QSet<QString> heard;
    for (const auto &c : computer)
        if (c.start < m.end + 3.0 && m.start < c.end + 3.0)
            for (const auto &w : wordsOf(c.text))
                heard.insert(stem(w));
    if (heard.isEmpty())
        return false;
    static const QRegularExpression sentenceEnd(R"((?<=[.?!])\s+)");
    QStringList kept;
    bool removed = false;
    for (const QString &sentence : m.text.split(sentenceEnd, Qt::SkipEmptyParts)) {
        const QStringList words = wordsOf(sentence);
        int matched = 0;
        for (const auto &w : words)
            matched += heard.contains(stem(w));
        if (!words.isEmpty() && double(matched) / double(words.size()) >= 0.7) {
            removed = true;
            continue;
        }
        kept << sentence;
    }
    if (removed)
        m.text = kept.join(' ').trimmed();
    return removed;
}

int runLive(whisper_context *ctx, whisper_vad_context *vad, whisper_full_params params, std::vector<Track> tracks)
{
    std::vector<LiveTrack> live;
    for (const auto &t : tracks)
        live.push_back({t, 0, {}});
    std::map<QString, int> votes;
    double lastRunSeconds = 0;  // how long the previous recognition took
    QElapsedTimer previewClock;
    previewClock.start();
    // Detecting the language costs a pass of its own: once the first
    // sentences are in, keep the language they were in.
    const bool autoLanguage = QString::fromLatin1(params.language) == "auto";
    QByteArray fixedLanguage;
    auto recognise = [&](std::vector<float> &piece) {
        if (piece.size() < size_t(kRate) + 1600)
            piece.resize(size_t(kRate) + 1600, 0.f);
        whisper_full_params p = params;
        // Phrases up to ~13 s are encoded with half of whisper's 30 s
        // context: about twice as fast. Shorter contexts are faster still but
        // make the recogniser loop; any loop that slips through is collapsed.
        if (double(piece.size()) / kRate <= 13.0)
            p.audio_ctx = 768;
        if (!fixedLanguage.isEmpty())
            p.language = fixedLanguage.constData();
        return whisper_full(ctx, p, piece.data(), int(piece.size())) == 0;
    };
    emitEvent({{"type", "ready"}});
    while (!g_abort) {
        // Wait between passes; react to a stop request within 100 ms.
        for (int i = 0; i < 15 && !g_abort; ++i)
            QThread::msleep(100);
        if (g_abort)
            break;
#ifndef _WIN32
        if (::getppid() == 1)
            break;  // the app is gone
#endif
        LiveTrack *computerTrack = nullptr, *micTrack = nullptr;
        for (auto &lt : live) {
            if (lt.track.source == "computer")
                computerTrack = &lt;
            else if (lt.track.source == "microphone")
                micTrack = &lt;
        }
        for (auto &lt : live) {
            if (g_abort)
                break;
            std::string err;
            const auto info = afterline::probeOggOpus(lt.track.path.toStdString(), &err);
            if (!info.valid)
                continue;
            const double available = info.seconds();
            if (available - lt.done < 2.0)
                continue;
            auto pcm = afterline::decodeOggOpus(lt.track.path.toStdString(), kRate, &err, lt.done,
                                                available - lt.done);
            if (pcm.empty())
                continue;
            const double bufferSeconds = double(pcm.size()) / kRate;
            if (rmsOf(pcm, 0, pcm.size()) < 2e-4) {
                // Silence: nothing to transcribe, keep a little overlap.
                if (bufferSeconds > 4)
                    lt.done += bufferSeconds - 1.0;
                continue;
            }
            auto windows = speechWindows(vad, pcm);
            if (!vad) {
                windows.clear();
                if (bufferSeconds >= 8)
                    windows.push_back({0, pcm.size()});
            }
            double newDone = -1;
            if (qEnvironmentVariableIsSet("AFTERLINE_LIVE_DEBUG")) {
                std::fprintf(stderr, "cycle %s done=%.1f buffer=%.1f windows:", qPrintable(lt.track.source), lt.done,
                             bufferSeconds);
                for (const auto &w : windows)
                    std::fprintf(stderr, " %.1f-%.1f", double(w.first) / kRate, double(w.second) / kRate);
                std::fprintf(stderr, "\n");
            }
            for (const auto &w : windows) {
                if (g_abort)
                    break;
                const double wEnd = double(w.second) / kRate;
                const double wLen = double(w.second - w.first) / kRate;
                // Speech that may still be going on waits, unless it is long.
                const bool finished = wEnd < bufferSeconds - 0.8 || wLen >= 25.0 || !vad;
                if (!finished) {
                    // A preview of the sentence being spoken, when the
                    // computer keeps up comfortably.
                    const double backlog = bufferSeconds - wEnd + wLen;
                    // At most every 6 s: each preview re-reads the whole
                    // sentence so far, and frequent ones keep the processor
                    // (and the fans) spinning up and down.
                    if (wLen >= 4.0 && lastRunSeconds < 2.5 && backlog < 10 &&
                        previewClock.elapsed() > 6000) {
                        previewClock.restart();
                        std::vector<float> piece(pcm.begin() + long(w.first), pcm.end());
                        if (recognise(piece)) {
                            QString text;
                            for (int i = 0; i < whisper_full_n_segments(ctx); ++i)
                                text += QString::fromUtf8(whisper_full_get_segment_text(ctx, i));
                            text = afterline::collapseRepetition(text.trimmed());
                            if (!isNoise(text, 0))
                                emitEvent({{"type", "partial"}, {"source", lt.track.source}, {"text", text}});
                        }
                    }
                    break;
                }
                std::vector<float> piece(pcm.begin() + long(w.first), pcm.begin() + long(w.second));
                QElapsedTimer timer;
                timer.start();
                if (!recognise(piece))
                    break;
                lastRunSeconds = timer.elapsed() / 1000.0;
                std::fprintf(stderr, "live: %s %.1f s of speech in %.1f s\n", qPrintable(lt.track.source), wLen,
                             lastRunSeconds);
                const QString detected = QString::fromLatin1(whisper_lang_str(whisper_full_lang_id(ctx)));
                if (autoLanguage && fixedLanguage.isEmpty()) {
                    int letters = 0;
                    for (const auto &v : votes)
                        letters += v.second;
                    if (letters > 60) {
                        auto best = std::max_element(votes.begin(), votes.end(),
                                                     [](const auto &a, const auto &b) { return a.second < b.second; });
                        fixedLanguage = best->first.toLatin1();
                    }
                }
                const double offset = lt.done + double(w.first) / kRate;
                std::vector<Segment> found;
                appendSentences(ctx, offset, offset + wLen, lt.track.source, detected, found, votes);
                for (Segment seg : found) {
                    // Speakers let the call leak into the microphone. What was
                    // heard on both tracks belongs to the call: it is removed
                    // from the mic line, whichever track was recognised first.
                    if (seg.source == "microphone" && computerTrack) {
                        liveStripEcho(seg, computerTrack->recent);
                        if (seg.text.isEmpty())
                            continue;
                    }
                    lt.recent.push_back(seg);
                    emitEvent({{"type", "segment"}, {"start", seg.start}, {"end", seg.end},
                               {"source", seg.source}, {"text", seg.text}});
                    if (seg.source == "computer" && micTrack) {
                        auto &mic = micTrack->recent;
                        for (auto it = mic.begin(); it != mic.end();) {
                            if (!liveStripEcho(*it, std::vector<Segment>{seg})) {
                                ++it;
                                continue;
                            }
                            emitEvent({{"type", "retract"}, {"source", "microphone"}, {"start", it->start}});
                            if (it->text.isEmpty()) {
                                it = mic.erase(it);
                            } else {
                                emitEvent({{"type", "segment"}, {"start", it->start}, {"end", it->end},
                                           {"source", it->source}, {"text", it->text}});
                                ++it;
                            }
                        }
                    }
                }
                if (found.empty())
                    emitEvent({{"type", "partial"}, {"source", lt.track.source}, {"text", QString()}});
                newDone = wEnd;
            }
            if (newDone > 0)
                lt.done += newDone;
            else if (windows.empty() && bufferSeconds > 6)
                lt.done += bufferSeconds - 1.5;  // no speech in this stretch
            // Keep only the last minute for the echo check.
            while (!lt.recent.empty() && lt.recent.front().end < lt.done - 60)
                lt.recent.erase(lt.recent.begin());
        }
    }
    return 0;
}

void usage()
{
    std::fputs("usage: afterline-transcriber --model FILE --track SOURCE=FILE... --output FILE\n"
               "       [--vad FILE] [--language CODE|auto] [--threads N] [--quality fast|balanced|accurate]\n"
               "       [--resume] [--gpu]\n"
               "       afterline-transcriber --live --model FILE --track SOURCE=FILE... [options]\n", stderr);
}

}  // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    installSignalHandlers();
    lowerPriority();

    QString modelPath, vadPath, output, language = "auto", quality = "balanced";
    int threads = defaultThreads();
    bool resume = false, gpu = false, live = false;
    std::vector<Track> tracks;
    const QStringList args = app.arguments();
    for (int i = 1; i < args.size(); ++i) {
        const QString a = args[i];
        auto next = [&]() -> QString {
            if (i + 1 >= args.size()) {
                usage();
                std::exit(2);
            }
            return args[++i];
        };
        if (a == "--model") modelPath = next();
        else if (a == "--vad") vadPath = next();
        else if (a == "--output") output = next();
        else if (a == "--language") language = next();
        else if (a == "--threads") threads = std::max(1, next().toInt());
        else if (a == "--quality") quality = next();
        else if (a == "--resume") resume = true;
        else if (a == "--gpu") gpu = true;
        else if (a == "--live") live = true;
        else if (a == "--track") {
            const QString v = next();
            const int eq = v.indexOf('=');
            if (eq <= 0) { usage(); return 2; }
            tracks.push_back({v.left(eq), v.mid(eq + 1)});
        } else if (a == "--version") {
            std::printf("afterline-transcriber %s (whisper.cpp %s)\n", AFTERLINE_VERSION, whisper_version());
            return 0;
        } else if (a == "--stdin-control") {
            // handled by installSignalHandlers()
        } else { usage(); return 2; }
    }
    if (live) {
#ifdef __linux__
        // Stop with the app, even if it is killed.
        ::prctl(PR_SET_PDEATHSIG, SIGTERM);
#endif
        quality = "fast";
    }
    if (modelPath.isEmpty() || (output.isEmpty() && !live) || tracks.empty()) {
        usage();
        return 2;
    }

    // Probe tracks. A track that cannot be read is reported, not fatal: the
    // other one may still hold the meeting.
    QStringList warnings;
    double total = 0;
    if (live) {
        total = 1;
        tracks.erase(std::remove_if(tracks.begin(), tracks.end(), [](const Track &t) { return t.path.isEmpty(); }),
                     tracks.end());
    } else
    for (auto &t : tracks) {
        std::string err;
        const auto info = afterline::probeOggOpus(t.path.toStdString(), &err);
        if (!info.valid) {
            warnings << QString("%1 audio could not be read: %2").arg(t.source, QString::fromStdString(err));
            t.seconds = 0;
            continue;
        }
        t.seconds = info.seconds();
        total += t.seconds;
    }
    if (total <= 0)
        fail("The recording contains no readable audio.");

    // Resume state.
    const QString partialPath = output + ".partial";
    std::vector<Segment> segments;
    std::map<QString, double> resumeAt;
    if (resume) {
        QFile f(partialPath);
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
            if (o["model"].toString() == QFileInfo(modelPath).fileName()) {
                for (const auto v : o["segments"].toArray())
                    segments.push_back(segmentFrom(v.toObject()));
                const QJsonObject pos = o["position"].toObject();
                for (auto it = pos.begin(); it != pos.end(); ++it)
                    resumeAt[it.key()] = it.value().toDouble();
            }
        }
    }

    whisper_log_set(quietLog, nullptr);
    ggml_backend_load_all_from_path(executableDir().toLocal8Bit().constData());
    progress(0, "Loading the speech recognition model");
    whisper_context_params cparams = whisper_context_default_params();
    cparams.use_gpu = gpu;
    whisper_context *ctx = whisper_init_from_file_with_params(modelPath.toLocal8Bit().constData(), cparams);
    if (!ctx)
        fail("The speech recognition model could not be loaded. It may be damaged: remove it in "
             "Settings → AI models and download it again.");

    whisper_full_params params = whisper_full_default_params(
        quality == "accurate" ? WHISPER_SAMPLING_BEAM_SEARCH : WHISPER_SAMPLING_GREEDY);
    params.n_threads = threads;
    params.print_progress = false;
    params.print_realtime = false;
    params.print_timestamps = false;
    params.print_special = false;
    params.translate = false;
    params.suppress_nst = true;
    params.no_speech_thold = 0.6f;
    params.token_timestamps = true;
    params.beam_search.beam_size = 5;
    params.greedy.best_of = quality == "fast" ? 1 : 3;
    if (quality == "fast")
        params.temperature_inc = 0.4f;
    const QByteArray lang = language.toLatin1();
    params.language = language == "auto" ? "auto" : lang.constData();
    params.detect_language = false;
    // Speech detection (Silero VAD). AfterLine feeds whisper only the parts
    // that contain speech, each at its exact position: no invented text in
    // the silent stretches of a meeting, and timestamps that do not drift.
    whisper_vad_context *vad = nullptr;
    if (!vadPath.isEmpty() && QFileInfo::exists(vadPath)) {
        whisper_vad_context_params vp = whisper_vad_default_context_params();
        vp.n_threads = threads;
        vp.use_gpu = false;
        vad = whisper_vad_init_from_file_with_params(vadPath.toLocal8Bit().constData(), vp);
        if (!vad)
            std::fputs("speech detection model could not be loaded; continuing without it\n", stderr);
    }
    params.abort_callback = onAbort;
    params.abort_callback_user_data = nullptr;
    if (live) {
        params.no_context = true;
        params.single_segment = false;
        params.greedy.best_of = 1;
        const int rc = runLive(ctx, vad, params, tracks);
        whisper_free(ctx);
        if (vad)
            whisper_vad_free(vad);
        return rc;
    }
    ProgressState state;
    state.total = total;
    params.abort_callback_user_data = nullptr;

    auto savePartial = [&]() {
        QJsonArray arr;
        for (size_t i = 0; i < segments.size(); ++i)
            arr.append(segmentJson(segments[i], int(i)));
        QJsonObject pos;
        for (const auto &r : resumeAt)
            pos[r.first] = r.second;
        writeJsonFile(partialPath, {{"model", QFileInfo(modelPath).fileName()}, {"segments", arr},
                                    {"position", pos}}, nullptr);
    };

    std::vector<bool> micMask;
    {
        const Track *mic = nullptr, *comp = nullptr;
        for (const auto &t : tracks) {
            if (t.seconds <= 0)
                continue;
            if (t.source == "microphone") mic = &t;
            if (t.source == "computer") comp = &t;
        }
        if (mic && comp) {
            progress(0, "Separating your voice from the call");
            micMask = echoMask(frameLevels(mic->path), frameLevels(comp->path));
        }
    }

    std::map<QString, int> languageVotes;
    for (const auto &t : tracks) {
        if (t.seconds <= 0)
            continue;
        const QString label = t.source == "microphone" ? "Transcribing your microphone"
                                                       : "Transcribing meeting audio";
        double start = resumeAt.count(t.source) ? resumeAt[t.source] : 0;
        state.done += start;
        while (start < t.seconds - 0.05) {
            if (g_abort) {
                savePartial();
                emitEvent({{"type", "cancelled"}});
                whisper_free(ctx);
                return 3;
            }
            double end = std::min(t.seconds, start + kChunkSeconds);
            if (end < t.seconds - 30)
                end = quietBoundary(t.path, end);
            state.chunk = end - start;
            state.stage = label;
            std::string err;
            auto pcm = afterline::decodeOggOpus(t.path.toStdString(), kRate, &err, start, end - start);
            if (t.source == "microphone" && !micMask.empty())
                applyMask(pcm, micMask, start);
            if (!pcm.empty() && rmsOf(pcm, 0, pcm.size()) > 2e-4) {
                const auto windows = speechWindows(vad, pcm);
                for (const auto &w : windows) {
                    if (g_abort)
                        break;
                    std::vector<float> piece(pcm.begin() + long(w.first), pcm.begin() + long(w.second));
                    // whisper needs at least one second of audio.
                    if (piece.size() < size_t(kRate) + 1600)
                        piece.resize(size_t(kRate) + 1600, 0.f);
                    state.stage = label;
                    if (whisper_full(ctx, params, piece.data(), int(piece.size())) != 0) {
                        if (g_abort)
                            break;
                        whisper_free(ctx);
                        fail("Speech recognition failed on this recording.");
                    }
                    const QString detected = QString::fromLatin1(whisper_lang_str(whisper_full_lang_id(ctx)));
                    const double offset = start + double(w.first) / kRate;
                    appendSentences(ctx, offset, offset + double(w.second - w.first) / kRate, t.source, detected,
                                    segments, languageVotes);
                    state.done += 0;  // progress inside the chunk comes from the window position
                    progress((state.done + double(w.second) / kRate) / state.total, label);
                }
                if (g_abort)
                    continue;  // handled at the top of the loop
            }
            state.done += end - start;
            start = end;
            resumeAt[t.source] = start;
            savePartial();
            progress(state.done / state.total, label);
        }
    }
    whisper_free(ctx);
    if (vad)
        whisper_vad_free(vad);

    std::sort(segments.begin(), segments.end(), [](const Segment &a, const Segment &b) {
        return a.start < b.start || (a.start == b.start && a.source < b.source);
    });
    const int echoes = removeEcho(segments);
    mergeFragments(segments);
    // Safety net: the recogniser occasionally repeats a sentence verbatim.
    segments.erase(std::unique(segments.begin(), segments.end(), [](const Segment &a, const Segment &b) {
        return a.source == b.source && a.text == b.text && b.start - a.start < 3.0;
    }), segments.end());

    QString mainLanguage;
    int best = -1;
    for (const auto &v : languageVotes)
        if (v.second > best) {
            best = v.second;
            mainLanguage = v.first;
        }

    QJsonArray arr;
    for (size_t i = 0; i < segments.size(); ++i)
        arr.append(segmentJson(segments[i], int(i)));
    QJsonArray trackArr;
    for (const auto &t : tracks)
        trackArr.append(QJsonObject{{"source", t.source}, {"seconds", t.seconds}});
    QJsonObject out{{"version", 1},
                    {"model", QFileInfo(modelPath).completeBaseName()},
                    {"language", mainLanguage},
                    {"created", QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                    {"tracks", trackArr},
                    {"echoRemoved", echoes},
                    {"warnings", QJsonArray::fromStringList(warnings)},
                    {"segments", arr}};
    QString error;
    if (!writeJsonFile(output, out, &error))
        fail("Could not save the transcript: " + error);
    QFile::remove(partialPath);
    emitEvent({{"type", "done"}, {"segments", int(segments.size())}, {"language", mainLanguage}});
    return 0;
}
