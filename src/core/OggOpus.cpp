// SPDX-License-Identifier: MIT
#include "core/OggOpus.h"

#include <ogg/ogg.h>
#include <opus.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <random>
#ifdef _WIN32
#include <io.h>
#define AFTERLINE_DATASYNC(f) ::_commit(_fileno(f))
#else
#include <unistd.h>
#define AFTERLINE_DATASYNC(f) ::fdatasync(fileno(f))
#endif

namespace afterline {

namespace {

void putLE16(std::vector<unsigned char> &v, uint16_t x)
{
    v.push_back(x & 0xff);
    v.push_back(x >> 8);
}

void putLE32(std::vector<unsigned char> &v, uint32_t x)
{
    for (int i = 0; i < 4; ++i)
        v.push_back((x >> (8 * i)) & 0xff);
}

void putString(std::vector<unsigned char> &v, const std::string &s)
{
    putLE32(v, uint32_t(s.size()));
    v.insert(v.end(), s.begin(), s.end());
}

// Keeps peaks from clipping harshly when several application streams are
// mixed into one track. Transparent below 0.9.
inline float softLimit(float x)
{
    if (!std::isfinite(x))
        return 0.f;
    const float a = std::fabs(x);
    if (a <= 0.9f)
        return x;
    const float y = 0.9f + 0.1f * std::tanh((a - 0.9f) / 0.1f);
    return x < 0 ? -y : y;
}

bool isHeaderPacket(const ogg_packet &op, const char *magic)
{
    return op.bytes >= 8 && std::memcmp(op.packet, magic, 8) == 0;
}

bool validDecodeRate(int rate)
{
    return rate == 48000 || rate == 24000 || rate == 16000 || rate == 12000 || rate == 8000;
}

}  // namespace

// ---------------------------------------------------------------------------
// Writer
// ---------------------------------------------------------------------------

struct OggOpusWriter::Impl {
    OpusEncoder *encoder = nullptr;
    ogg_stream_state stream{};
    bool streamInit = false;
    int preSkip = 0;
    int64_t packetNo = 0;
    int64_t granule = 0;  // 48 kHz samples encoded so far, including pre-skip
    std::vector<unsigned char> packet = std::vector<unsigned char>(4000);

    ~Impl()
    {
        if (encoder)
            opus_encoder_destroy(encoder);
        if (streamInit)
            ogg_stream_clear(&stream);
    }
};

OggOpusWriter::OggOpusWriter() = default;

OggOpusWriter::~OggOpusWriter()
{
    if (m_file)
        close();
}

bool OggOpusWriter::fail(const std::string &message)
{
    m_error = message;
    return false;
}

bool OggOpusWriter::open(const std::string &path, const Options &options, std::string *error)
{
    if (m_file)
        close();
    d = std::make_unique<Impl>();
    m_options = options;
    m_samples = m_lastFlushSample = m_lastSyncSample = 0;
    m_pending.clear();
    m_error.clear();

    auto bail = [&](const std::string &message) {
        m_error = message;
        if (error)
            *error = message;
        if (m_file) {
            std::fclose(m_file);
            m_file = nullptr;
        }
        d.reset();
        return false;
    };

    int err = 0;
    d->encoder = opus_encoder_create(kOpusRate, 1, OPUS_APPLICATION_VOIP, &err);
    if (err != OPUS_OK || !d->encoder)
        return bail(std::string("Opus encoder: ") + opus_strerror(err));
    opus_encoder_ctl(d->encoder, OPUS_SET_BITRATE(options.bitrate));
    // Complexity 5 costs about a third of the default's CPU with no audible
    // difference for speech; recording must stay light during a call.
    opus_encoder_ctl(d->encoder, OPUS_SET_COMPLEXITY(5));
    opus_int32 lookahead = 0;
    opus_encoder_ctl(d->encoder, OPUS_GET_LOOKAHEAD(&lookahead));
    d->preSkip = int(lookahead);

    std::random_device rd;
    if (ogg_stream_init(&d->stream, int(rd() & 0x7fffffff)) != 0)
        return bail("Ogg stream initialisation failed");
    d->streamInit = true;

    m_file = std::fopen(path.c_str(), "wb");
    if (!m_file)
        return bail("Cannot create " + path + ": " + std::strerror(errno));

    // Identification header (RFC 7845 §5.1), alone on the first page.
    std::vector<unsigned char> head = {'O', 'p', 'u', 's', 'H', 'e', 'a', 'd', 1, 1};
    putLE16(head, uint16_t(d->preSkip));
    putLE32(head, kOpusRate);
    putLE16(head, 0);  // output gain
    head.push_back(0); // channel mapping family 0 (mono/stereo)
    ogg_packet op{};
    op.packet = head.data();
    op.bytes = long(head.size());
    op.b_o_s = 1;
    op.packetno = d->packetNo++;
    ogg_stream_packetin(&d->stream, &op);
    if (!writePages(true))
        return bail(m_error);

    // Comment header (§5.2).
    std::vector<unsigned char> tags = {'O', 'p', 'u', 's', 'T', 'a', 'g', 's'};
    putString(tags, opus_get_version_string());
    std::vector<std::string> comments = {"ENCODER=AfterLine"};
    if (!options.title.empty())
        comments.push_back("TITLE=" + options.title);
    putLE32(tags, uint32_t(comments.size()));
    for (const auto &c : comments)
        putString(tags, c);
    op = ogg_packet{};
    op.packet = tags.data();
    op.bytes = long(tags.size());
    op.packetno = d->packetNo++;
    ogg_stream_packetin(&d->stream, &op);
    if (!writePages(true) || !flush(options.syncSeconds > 0))
        return bail(m_error);
    return true;
}

bool OggOpusWriter::writePages(bool force)
{
    ogg_page page;
    for (;;) {
        const int got = force ? ogg_stream_flush(&d->stream, &page) : ogg_stream_pageout(&d->stream, &page);
        if (!got)
            break;
        if (std::fwrite(page.header, 1, size_t(page.header_len), m_file) != size_t(page.header_len) ||
            std::fwrite(page.body, 1, size_t(page.body_len), m_file) != size_t(page.body_len))
            return fail(std::string("Write failed: ") + std::strerror(errno));
    }
    return true;
}

bool OggOpusWriter::encodeFrame(const float *frame, bool last)
{
    const opus_int32 bytes = opus_encode_float(d->encoder, frame, kOpusFrame, d->packet.data(),
                                               opus_int32(d->packet.size()));
    if (bytes < 0)
        return fail(std::string("Opus encoding failed: ") + opus_strerror(bytes));
    d->granule += kOpusFrame;
    ogg_packet op{};
    op.packet = d->packet.data();
    op.bytes = bytes;
    op.packetno = d->packetNo++;
    // The final page's granule position trims the padding (end trimming).
    op.granulepos = last ? int64_t(m_samples) + d->preSkip : d->granule;
    op.e_o_s = last ? 1 : 0;
    ogg_stream_packetin(&d->stream, &op);
    return writePages(last);
}

bool OggOpusWriter::write(const float *samples, size_t count)
{
    if (!m_file)
        return fail("Writer is not open");
    float frame[kOpusFrame];
    size_t i = 0;
    // Complete a partially filled frame first.
    if (!m_pending.empty()) {
        while (i < count && m_pending.size() < size_t(kOpusFrame))
            m_pending.push_back(softLimit(samples[i++]));
        if (m_pending.size() < size_t(kOpusFrame)) {
            m_samples += count;
            return true;
        }
        if (!encodeFrame(m_pending.data(), false))
            return false;
        m_pending.clear();
    }
    for (; i + kOpusFrame <= count; i += kOpusFrame) {
        for (int k = 0; k < kOpusFrame; ++k)
            frame[k] = softLimit(samples[i + k]);
        if (!encodeFrame(frame, false))
            return false;
    }
    for (; i < count; ++i)
        m_pending.push_back(softLimit(samples[i]));
    m_samples += count;

    if (m_samples - m_lastFlushSample >= uint64_t(m_options.flushSeconds * kOpusRate)) {
        const bool sync = m_options.syncSeconds > 0 &&
                          m_samples - m_lastSyncSample >= uint64_t(m_options.syncSeconds * kOpusRate);
        return flush(sync);
    }
    return true;
}

bool OggOpusWriter::writeSilence(size_t count)
{
    static const float zeros[kOpusFrame] = {};
    while (count) {
        const size_t n = std::min(count, size_t(kOpusFrame));
        if (!write(zeros, n))
            return false;
        count -= n;
    }
    return true;
}

bool OggOpusWriter::flush(bool sync)
{
    if (!m_file)
        return false;
    if (!writePages(true))
        return false;
    if (std::fflush(m_file) != 0)
        return fail(std::string("Write failed: ") + std::strerror(errno));
    m_lastFlushSample = m_samples;
    if (sync) {
        // fdatasync can fail on some FUSE filesystems; the data is still in
        // the page cache, so this is not treated as a recording failure.
        AFTERLINE_DATASYNC(m_file);
        m_lastSyncSample = m_samples;
    }
    return true;
}

bool OggOpusWriter::close()
{
    if (!m_file)
        return true;
    bool ok = true;
    // Feed enough padding to push the encoder's look-ahead out, then mark the
    // final packet so the stream ends exactly at the last real sample.
    std::vector<float> tail = m_pending;
    m_pending.clear();
    tail.resize(tail.size() + size_t(d->preSkip), 0.f);
    const size_t frames = std::max<size_t>(1, (tail.size() + kOpusFrame - 1) / kOpusFrame);
    tail.resize(frames * kOpusFrame, 0.f);
    for (size_t f = 0; f < frames && ok; ++f)
        ok = encodeFrame(tail.data() + f * kOpusFrame, f + 1 == frames);
    ok = ok && writePages(true);
    if (std::fflush(m_file) != 0)
        ok = fail(std::string("Write failed: ") + std::strerror(errno));
    AFTERLINE_DATASYNC(m_file);
    if (std::fclose(m_file) != 0 && ok)
        ok = fail(std::string("Close failed: ") + std::strerror(errno));
    m_file = nullptr;
    d.reset();
    return ok;
}

// ---------------------------------------------------------------------------
// Page scanning
// ---------------------------------------------------------------------------

namespace {

struct PageEntry {
    int64_t granule;   // end granule of the page (-1 when no packet ends here)
    uint64_t end;      // byte offset just after the page
};

struct Scan {
    OggOpusInfo info;
    int serial = -1;
    uint64_t audioStart = 0;      // offset of the first audio page
    std::vector<PageEntry> pages; // audio pages only
};

bool scanFile(FILE *file, Scan &scan, std::string *error)
{
    ogg_sync_state sync;
    ogg_sync_init(&sync);
    ogg_stream_state stream{};
    bool streamInit = false;
    int headerPackets = 0;
    uint64_t offset = 0;
    int64_t lastGranule = 0;
    bool ok = true;

    std::fseek(file, 0, SEEK_END);
    scan.info.fileBytes = uint64_t(std::ftell(file));
    std::fseek(file, 0, SEEK_SET);

    for (;;) {
        ogg_page page;
        const long n = ogg_sync_pageseek(&sync, &page);
        if (n < 0) {  // skipped garbage (e.g. a torn page)
            offset += uint64_t(-n);
            continue;
        }
        if (n == 0) {
            char *buffer = ogg_sync_buffer(&sync, 65536);
            const size_t got = std::fread(buffer, 1, 65536, file);
            if (got == 0)
                break;
            ogg_sync_wrote(&sync, long(got));
            continue;
        }
        offset += uint64_t(n);
        if (!streamInit) {
            if (!ogg_page_bos(&page))
                continue;
            scan.serial = ogg_page_serialno(&page);
            ogg_stream_init(&stream, scan.serial);
            streamInit = true;
        }
        if (ogg_page_serialno(&page) != scan.serial)
            continue;
        if (headerPackets < 2) {
            ogg_stream_pagein(&stream, &page);
            ogg_packet op;
            while (headerPackets < 2 && ogg_stream_packetout(&stream, &op) == 1) {
                if (headerPackets == 0) {
                    if (!isHeaderPacket(op, "OpusHead") || op.bytes < 19) {
                        if (error)
                            *error = "Not an Ogg Opus file";
                        ok = false;
                        break;
                    }
                    scan.info.channels = op.packet[9];
                    scan.info.preSkip = op.packet[10] | (op.packet[11] << 8);
                } else if (!isHeaderPacket(op, "OpusTags")) {
                    if (error)
                        *error = "Missing Opus comment header";
                    ok = false;
                    break;
                }
                ++headerPackets;
            }
            if (!ok)
                break;
            if (headerPackets == 2) {
                scan.audioStart = offset;
                scan.info.valid = true;
            }
            scan.info.validBytes = offset;
            continue;
        }
        const int64_t granule = ogg_page_granulepos(&page);
        scan.pages.push_back({granule, offset});
        if (granule >= 0)
            lastGranule = granule;
        scan.info.validBytes = offset;
        if (ogg_page_eos(&page)) {
            scan.info.complete = true;
            break;
        }
    }
    if (streamInit)
        ogg_stream_clear(&stream);
    ogg_sync_clear(&sync);
    if (ok && !scan.info.valid && error && error->empty())
        *error = "File contains no complete Opus headers";
    scan.info.samples = uint64_t(std::max<int64_t>(0, lastGranule - scan.info.preSkip));
    return ok && scan.info.valid;
}

}  // namespace

OggOpusInfo probeOggOpus(const std::string &path, std::string *error)
{
    FILE *file = std::fopen(path.c_str(), "rb");
    if (!file) {
        if (error)
            *error = "Cannot open " + path + ": " + std::strerror(errno);
        return {};
    }
    Scan scan;
    scanFile(file, scan, error);
    std::fclose(file);
    return scan.info;
}

// ---------------------------------------------------------------------------
// Reader
// ---------------------------------------------------------------------------

struct OggOpusReader::Impl {
    FILE *file = nullptr;
    Scan scan;
    ogg_sync_state sync{};
    ogg_stream_state stream{};
    OpusDecoder *decoder = nullptr;
    std::vector<float> decoded;
    size_t decodedPos = 0;
    uint64_t toSkip = 0;  // output samples still to discard
    bool atEnd = false;

    ~Impl()
    {
        if (decoder)
            opus_decoder_destroy(decoder);
        ogg_stream_clear(&stream);
        ogg_sync_clear(&sync);
        if (file)
            std::fclose(file);
    }
};

OggOpusReader::OggOpusReader() = default;
OggOpusReader::~OggOpusReader() = default;

void OggOpusReader::close() { d.reset(); }

bool OggOpusReader::open(const std::string &path, int outputRate, std::string *error)
{
    d.reset();
    if (!validDecodeRate(outputRate)) {
        if (error)
            *error = "Unsupported decode rate";
        return false;
    }
    auto impl = std::make_unique<Impl>();
    impl->file = std::fopen(path.c_str(), "rb");
    if (!impl->file) {
        if (error)
            *error = "Cannot open " + path + ": " + std::strerror(errno);
        return false;
    }
    if (!scanFile(impl->file, impl->scan, error))
        return false;
    if (impl->scan.info.channels < 1 || impl->scan.info.channels > 2) {
        if (error)
            *error = "Unsupported channel layout";
        return false;
    }
    int err = 0;
    // Decoding straight to the requested rate avoids a separate resampler.
    impl->decoder = opus_decoder_create(outputRate, 1, &err);
    if (err != OPUS_OK) {
        if (error)
            *error = std::string("Opus decoder: ") + opus_strerror(err);
        return false;
    }
    ogg_sync_init(&impl->sync);
    ogg_stream_init(&impl->stream, impl->scan.serial);
    d = std::move(impl);
    m_rate = outputRate;
    return seek(0);
}

uint64_t OggOpusReader::length() const
{
    if (!d)
        return 0;
    return d->scan.info.samples * uint64_t(m_rate) / kOpusRate;
}

bool OggOpusReader::seek(uint64_t sample)
{
    if (!d)
        return false;
    sample = std::min(sample, length());
    const uint64_t target48 = sample * kOpusRate / uint64_t(m_rate);
    const int64_t preSkip = d->scan.info.preSkip;
    // Start decoding at least 80 ms before the target so the decoder has
    // converged (RFC 7845 §4.6), from the end of the last page before it.
    const int64_t want = int64_t(target48) + preSkip - 3840;
    uint64_t offset = d->scan.audioStart;
    int64_t granule = 0;
    for (const auto &p : d->scan.pages) {
        if (p.granule < 0)
            continue;
        if (p.granule > want)
            break;
        offset = p.end;
        granule = p.granule;
    }
    std::fseek(d->file, long(offset), SEEK_SET);
    ogg_sync_reset(&d->sync);
    ogg_stream_reset(&d->stream);
    opus_decoder_ctl(d->decoder, OPUS_RESET_STATE);
    d->decoded.clear();
    d->decodedPos = 0;
    d->atEnd = false;
    const uint64_t skip48 = uint64_t(int64_t(target48) + preSkip - granule);
    d->toSkip = skip48 * uint64_t(m_rate) / kOpusRate;
    m_position = sample;
    return true;
}

bool OggOpusReader::readPage()
{
    for (;;) {
        ogg_page page;
        const int r = ogg_sync_pageout(&d->sync, &page);
        if (r == 1) {
            if (ogg_page_serialno(&page) != d->scan.serial)
                continue;
            ogg_stream_pagein(&d->stream, &page);
            return true;
        }
        if (r < 0)
            continue;  // resynchronising past damage
        char *buffer = ogg_sync_buffer(&d->sync, 65536);
        const size_t got = std::fread(buffer, 1, 65536, d->file);
        if (got == 0)
            return false;
        ogg_sync_wrote(&d->sync, long(got));
    }
}

bool OggOpusReader::decodeNextPacket()
{
    float pcm[5760];  // 120 ms at 48 kHz, the largest Opus frame
    for (;;) {
        ogg_packet op;
        const int r = ogg_stream_packetout(&d->stream, &op);
        if (r == 0) {
            if (!readPage())
                return false;
            continue;
        }
        if (r < 0)
            continue;
        if (isHeaderPacket(op, "OpusHead") || isHeaderPacket(op, "OpusTags"))
            continue;
        const int n = opus_decode_float(d->decoder, op.packet, opus_int32(op.bytes), pcm, 5760, 0);
        if (n < 0)
            continue;  // a damaged packet; keep going
        size_t start = 0;
        if (d->toSkip) {
            start = size_t(std::min<uint64_t>(d->toSkip, uint64_t(n)));
            d->toSkip -= start;
        }
        d->decoded.assign(pcm + start, pcm + n);
        d->decodedPos = 0;
        if (!d->decoded.empty())
            return true;
    }
}

size_t OggOpusReader::read(float *out, size_t max)
{
    if (!d)
        return 0;
    const uint64_t remaining = length() - std::min(length(), m_position);
    max = size_t(std::min<uint64_t>(max, remaining));
    size_t done = 0;
    while (done < max) {
        if (d->decodedPos >= d->decoded.size()) {
            if (d->atEnd || !decodeNextPacket()) {
                d->atEnd = true;
                break;
            }
        }
        const size_t n = std::min(max - done, d->decoded.size() - d->decodedPos);
        std::memcpy(out + done, d->decoded.data() + d->decodedPos, n * sizeof(float));
        d->decodedPos += n;
        done += n;
    }
    m_position += done;
    return done;
}

std::vector<float> decodeOggOpus(const std::string &path, int rate, std::string *error,
                                 double startSeconds, double durationSeconds)
{
    OggOpusReader reader;
    std::vector<float> out;
    if (!reader.open(path, rate, error))
        return out;
    const uint64_t start = uint64_t(std::max(0.0, startSeconds) * rate);
    uint64_t end = reader.length();
    if (durationSeconds >= 0)
        end = std::min(end, start + uint64_t(durationSeconds * rate));
    if (start >= end)
        return out;
    reader.seek(start);
    out.resize(size_t(end - start));
    out.resize(reader.read(out.data(), out.size()));
    return out;
}

}  // namespace afterline
