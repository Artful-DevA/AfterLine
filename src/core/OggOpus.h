// SPDX-License-Identifier: MIT
//
// Crash-tolerant Ogg Opus writer and reader (RFC 7845), built directly on
// libopus and libogg.
//
// Recordings are written as a sequence of Ogg pages, each a self-contained,
// checksummed unit. The writer emits a page roughly every second and asks the
// kernel to put it on disk every few seconds, so a crash, a power cut or a
// killed process loses at most those last seconds: everything before them is
// a valid Ogg Opus stream that any player (and the reader below) can decode.
// No header has to be patched at the end, unlike WAV.
#pragma once

#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace afterline {

// Opus always works with a 48 kHz timeline (granule positions count 48 kHz
// samples). The writer takes mono float samples at 48 kHz.
constexpr int kOpusRate = 48000;
constexpr int kOpusFrame = 960;  // 20 ms

class OggOpusWriter {
public:
    struct Options {
        int bitrate = 40000;             // bits/s, mono speech-optimised
        double flushSeconds = 1.0;        // close an Ogg page at least this often
        double syncSeconds = 5.0;         // fdatasync at least this often (0 = never)
        std::string title;                // TITLE comment
    };

    OggOpusWriter();
    ~OggOpusWriter();
    OggOpusWriter(const OggOpusWriter &) = delete;
    OggOpusWriter &operator=(const OggOpusWriter &) = delete;

    // Creates (truncates) the file and writes the stream headers.
    bool open(const std::string &path, const Options &options, std::string *error);
    bool open(const std::string &path, std::string *error) { return open(path, Options{}, error); }
    // Appends mono 48 kHz samples. Values outside [-1, 1] are soft-limited.
    bool write(const float *samples, size_t count);
    bool writeSilence(size_t count);
    // Forces buffered audio into a page and onto disk.
    bool flush(bool sync);
    // Writes the final page (end-of-stream) and closes the file.
    bool close();

    bool isOpen() const { return m_file != nullptr; }
    // Number of real (non-padding) samples accepted so far.
    uint64_t samplesWritten() const { return m_samples; }
    const std::string &lastError() const { return m_error; }

private:
    bool encodeFrame(const float *frame, bool last);
    bool writePages(bool force);
    bool fail(const std::string &message);

    struct Impl;
    std::unique_ptr<Impl> d;
    FILE *m_file = nullptr;
    Options m_options;
    std::vector<float> m_pending;  // < one frame of not yet encoded samples
    uint64_t m_samples = 0;
    uint64_t m_lastFlushSample = 0;
    uint64_t m_lastSyncSample = 0;
    std::string m_error;
};

// Information gathered by scanning page headers only (fast, no decoding).
struct OggOpusInfo {
    bool valid = false;
    int channels = 0;
    int preSkip = 0;
    uint64_t samples = 0;      // decodable 48 kHz samples (after pre-skip)
    bool complete = false;     // end-of-stream page present (cleanly closed)
    uint64_t validBytes = 0;   // bytes up to the end of the last intact page
    uint64_t fileBytes = 0;
    double seconds() const { return samples / double(kOpusRate); }
};

OggOpusInfo probeOggOpus(const std::string &path, std::string *error = nullptr);

// Decodes an Ogg Opus file to mono float at 48000, 24000, 16000, 12000 or
// 8000 Hz. Truncated files decode up to the last intact page.
class OggOpusReader {
public:
    OggOpusReader();
    ~OggOpusReader();
    OggOpusReader(const OggOpusReader &) = delete;
    OggOpusReader &operator=(const OggOpusReader &) = delete;

    bool open(const std::string &path, int outputRate, std::string *error);
    void close();
    // Reads up to `max` samples; returns the number read, 0 at the end.
    size_t read(float *out, size_t max);
    // Positions the reader at `sample` (in output-rate samples).
    bool seek(uint64_t sample);
    uint64_t position() const { return m_position; }
    uint64_t length() const;  // in output-rate samples
    int rate() const { return m_rate; }

private:
    bool decodeNextPacket();
    bool readPage();

    struct Impl;
    std::unique_ptr<Impl> d;
    int m_rate = kOpusRate;
    uint64_t m_position = 0;
};

// Convenience: decodes a whole file (or a range) into memory.
std::vector<float> decodeOggOpus(const std::string &path, int rate, std::string *error,
                                 double startSeconds = 0, double durationSeconds = -1);

}  // namespace afterline
