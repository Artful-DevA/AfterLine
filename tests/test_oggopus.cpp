// SPDX-License-Identifier: MIT
// Recording-format tests: round trips, exact lengths, seeking, and recovery
// from files that were cut off mid-write (crash, power loss, kill -9).
#include "core/OggOpus.h"

#include <QTemporaryDir>
#include <QtTest>

#include <cmath>
#include <fstream>

using namespace afterline;

namespace {

std::vector<float> tone(double seconds, double freq, int rate = kOpusRate)
{
    std::vector<float> v(size_t(seconds * rate));
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = 0.5f * float(std::sin(2 * M_PI * freq * double(i) / rate));
    return v;
}

double rms(const float *v, size_t n)
{
    double s = 0;
    for (size_t i = 0; i < n; ++i)
        s += double(v[i]) * v[i];
    return n ? std::sqrt(s / double(n)) : 0;
}

// Estimates the frequency of a sine by counting zero crossings.
double frequency(const std::vector<float> &v, int rate)
{
    int crossings = 0;
    for (size_t i = 1; i < v.size(); ++i)
        if ((v[i - 1] < 0) != (v[i] < 0))
            ++crossings;
    return crossings / 2.0 / (double(v.size()) / rate);
}

}  // namespace

class OggOpusTest : public QObject {
    Q_OBJECT
    QTemporaryDir dir;
    std::string path(const char *name) { return dir.filePath(name).toStdString(); }

private Q_SLOTS:
    void roundTripExactLength()
    {
        OggOpusWriter w;
        std::string err;
        QVERIFY(w.open(path("a.opus"), &err));
        auto t = tone(3.0137, 440);  // deliberately not a frame multiple
        // Feed in awkward block sizes, as PipeWire does.
        size_t i = 0, step = 333;
        while (i < t.size()) {
            const size_t n = std::min(step, t.size() - i);
            QVERIFY(w.write(t.data() + i, n));
            i += n;
            step = step == 333 ? 1024 : 333;
        }
        QCOMPARE(w.samplesWritten(), uint64_t(t.size()));
        QVERIFY(w.close());

        auto info = probeOggOpus(path("a.opus"), &err);
        QVERIFY2(info.valid, err.c_str());
        QVERIFY(info.complete);
        QCOMPARE(info.samples, uint64_t(t.size()));
        QCOMPARE(info.channels, 1);

        auto pcm = decodeOggOpus(path("a.opus"), 48000, &err);
        QCOMPARE(pcm.size(), t.size());
        QVERIFY(std::abs(frequency(pcm, 48000) - 440) < 5);
        QVERIFY(std::abs(rms(pcm.data(), pcm.size()) - 0.3535) < 0.03);
    }

    void decodesAt16kForTranscription()
    {
        OggOpusWriter w;
        QVERIFY(w.open(path("b.opus"), nullptr));
        auto t = tone(2.0, 1000);
        QVERIFY(w.write(t.data(), t.size()));
        QVERIFY(w.close());
        std::string err;
        auto pcm = decodeOggOpus(path("b.opus"), 16000, &err);
        QCOMPARE(pcm.size(), size_t(32000));
        QVERIFY(std::abs(frequency(pcm, 16000) - 1000) < 10);
    }

    void seekLandsOnTheRightAudio()
    {
        // 1 s of 300 Hz then 1 s of 1200 Hz, repeated: seeking must land in
        // the matching tone.
        OggOpusWriter w;
        QVERIFY(w.open(path("c.opus"), nullptr));
        for (int k = 0; k < 10; ++k) {
            auto t = tone(1.0, k % 2 ? 1200 : 300);
            QVERIFY(w.write(t.data(), t.size()));
        }
        QVERIFY(w.close());
        OggOpusReader r;
        std::string err;
        QVERIFY(r.open(path("c.opus"), 16000, &err));
        QCOMPARE(r.length(), uint64_t(160000));
        for (int second : {7, 2, 9, 0, 5}) {
            QVERIFY(r.seek(uint64_t(second) * 16000 + 4000));
            std::vector<float> v(8000);
            QCOMPARE(r.read(v.data(), v.size()), v.size());
            const double f = frequency(v, 16000);
            QVERIFY2(std::abs(f - (second % 2 ? 1200 : 300)) < 30,
                     qPrintable(QString("second %1 -> %2 Hz").arg(second).arg(f)));
        }
    }

    void truncatedFileIsRecoverable()
    {
        // Simulate a crash: write 10 s, flush, never close, then chop the file
        // in the middle of a page.
        std::string err;
        {
            OggOpusWriter w;
            OggOpusWriter::Options o;
            o.flushSeconds = 1.0;
            QVERIFY(w.open(path("d.opus"), o, &err));
            auto t = tone(10.0, 500);
            QVERIFY(w.write(t.data(), t.size()));
            QVERIFY(w.flush(true));
            // Copy the file as it is on disk *now*, before close() runs.
            std::ifstream in(path("d.opus"), std::ios::binary);
            std::string bytes((std::istreambuf_iterator<char>(in)), {});
            std::ofstream(path("crash.opus"), std::ios::binary)
                .write(bytes.data(), std::streamsize(bytes.size() - 37));
        }
        auto info = probeOggOpus(path("crash.opus"), &err);
        QVERIFY2(info.valid, err.c_str());
        QVERIFY(!info.complete);
        QVERIFY2(info.seconds() > 8.5, qPrintable(QString::number(info.seconds())));
        auto pcm = decodeOggOpus(path("crash.opus"), 16000, &err);
        QCOMPARE(pcm.size(), size_t(info.samples / 3));
        QVERIFY(std::abs(frequency(pcm, 16000) - 500) < 10);
    }

    void silenceAndLongRecordingsStaySmall()
    {
        OggOpusWriter w;
        QVERIFY(w.open(path("e.opus"), nullptr));
        QVERIFY(w.writeSilence(size_t(kOpusRate) * 600));  // 10 minutes
        QVERIFY(w.close());
        auto info = probeOggOpus(path("e.opus"));
        QCOMPARE(info.samples, uint64_t(kOpusRate) * 600);
        // Opus DTX-free silence is still tiny compared with 57 MB of WAV.
        QVERIFY(info.fileBytes < 3'500'000);
    }

    void rejectsGarbage()
    {
        std::ofstream(path("junk.opus")) << "this is not audio";
        std::string err;
        QVERIFY(!probeOggOpus(path("junk.opus"), &err).valid);
        QVERIFY(!err.empty());
        OggOpusReader r;
        QVERIFY(!r.open(path("missing.opus"), 16000, &err));
    }

    void clipsSoftlyInsteadOfWrapping()
    {
        OggOpusWriter w;
        QVERIFY(w.open(path("f.opus"), nullptr));
        // A badly overdriven voice (3x full scale) plus a corrupt sample.
        auto loud = tone(1.0, 300);
        for (float &x : loud)
            x *= 6.0f;
        loud[1000] = std::nanf("");
        QVERIFY(w.write(loud.data(), loud.size()));
        QVERIFY(w.close());
        auto pcm = decodeOggOpus(path("f.opus"), 48000, nullptr);
        for (float x : pcm)
            QVERIFY(std::isfinite(x) && std::fabs(x) < 1.25f);
    }
};

QTEST_GUILESS_MAIN(OggOpusTest)
#include "test_oggopus.moc"
