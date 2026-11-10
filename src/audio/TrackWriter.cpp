// SPDX-License-Identifier: MIT
#include "audio/TrackWriter.h"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>

namespace afterline::audio {

using Clock = std::chrono::steady_clock;

TrackWriter::~TrackWriter()
{
    if (running())
        stop();
}

bool TrackWriter::start(const Options &options, std::string *error)
{
    m_options = options;
    OggOpusWriter::Options wo;
    wo.bitrate = options.bitrate;
    wo.title = options.title;
    if (!m_writer.open(options.audioPath, wo, error))
        return false;
    if (!options.peaksPath.empty()) {
        m_peaks = std::fopen(options.peaksPath.c_str(), "wb");
        if (!m_peaks) {
            if (error)
                *error = "Cannot create " + options.peaksPath + ": " + std::strerror(errno);
            m_writer.close();
            return false;
        }
    }
    m_stop = false;
    m_paused = false;
    m_hadData = false;
    m_activeTime = {};
    m_written = 0;
    m_padded = 0;
    m_failed = false;
    m_tickTime = Clock::now();
    m_thread = std::thread([this] { run(); });
    return true;
}

void TrackWriter::push(const float *samples, size_t count)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_paused || m_stop)
        return;
    // Bound memory if the writer thread is stuck (e.g. disk hung): keep the
    // newest 30 s rather than growing without limit.
    const size_t limit = size_t(kOpusRate) * 30;
    if (m_incoming.size() + count > limit)
        m_incoming.erase(m_incoming.begin(),
                         m_incoming.begin() + std::min(m_incoming.size(), m_incoming.size() + count - limit));
    m_incoming.insert(m_incoming.end(), samples, samples + count);
    m_lastData = Clock::now();
    m_hadData = true;
}

void TrackWriter::setPaused(bool paused)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_paused = paused;
    if (paused)
        m_incoming.clear();
    m_cv.notify_all();
}

double TrackWriter::secondsSinceData() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    if (!m_hadData)
        return -1;
    return std::chrono::duration<double>(Clock::now() - m_lastData).count();
}

std::string TrackWriter::error() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_error;
}

void TrackWriter::writeBlock(const float *data, size_t n)
{
    if (m_failed)
        return;
    if (!m_writer.write(data, n)) {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_error = m_writer.lastError();
        m_failed = true;
        return;
    }
    // Peaks for the waveform, one per 50 ms of timeline.
    const int perPeak = kOpusRate / kPeaksPerSecond;
    for (size_t i = 0; i < n; ++i) {
        m_peakAcc = std::max(m_peakAcc, std::fabs(data[i]));
        if (++m_peakCount == perPeak) {
            if (m_peaks) {
                // sqrt scale keeps quiet speech visible.
                const unsigned char v =
                    (unsigned char)std::lround(std::sqrt(std::min(1.f, m_peakAcc)) * 255.f);
                std::fputc(v, m_peaks);
            }
            m_peakAcc = 0;
            m_peakCount = 0;
        }
    }
    m_written += n;
}

void TrackWriter::run()
{
    std::vector<float> block;
    std::vector<float> zeros;
    float level = 0.f;
    for (;;) {
        bool stop, paused;
        Clock::time_point lastData;
        bool hadData;
        {
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.wait_for(lock, std::chrono::milliseconds(50), [this] { return m_stop; });
            stop = m_stop;
            paused = m_paused;
            lastData = m_lastData;
            hadData = m_hadData;
            block.swap(m_incoming);
            m_incoming.clear();
        }
        const auto now = Clock::now();
        if (!paused)
            m_activeTime += now - m_tickTime;
        m_tickTime = now;

        float peak = 0.f;
        for (float x : block)
            peak = std::max(peak, std::fabs(x));
        level = std::max(peak, level * 0.6f);
        m_level = paused ? 0.f : level;

        if (!block.empty())
            writeBlock(block.data(), block.size());

        // Bridge gaps with silence so all tracks share one timeline.
        if (!paused) {
            const double expected = std::chrono::duration<double>(m_activeTime).count() * kOpusRate;
            const double idle = hadData ? std::chrono::duration<double>(now - lastData).count() : 1e9;
            const double behind = expected - double(m_written.load());
            if (block.empty() && idle > m_options.gapSeconds && behind > m_options.gapSeconds * kOpusRate) {
                const size_t n = size_t(behind);
                zeros.assign(std::min(n, size_t(kOpusRate) * 10), 0.f);
                size_t left = n;
                while (left) {
                    const size_t k = std::min(left, zeros.size());
                    writeBlock(zeros.data(), k);
                    left -= k;
                }
                m_padded += n;
            }
        }
        if (stop)
            break;
    }
    if (m_peaks) {
        if (m_peakCount > 0)
            std::fputc((unsigned char)std::lround(std::sqrt(std::min(1.f, m_peakAcc)) * 255.f), m_peaks);
        std::fclose(m_peaks);
        m_peaks = nullptr;
    }
}

bool TrackWriter::stop()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_stop = true;
        m_cv.notify_all();
    }
    if (m_thread.joinable())
        m_thread.join();
    const bool closed = m_writer.close();
    if (!closed) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_error.empty())
            m_error = m_writer.lastError();
    }
    return closed && !m_failed;
}

}  // namespace afterline::audio
