// SPDX-License-Identifier: MIT
#include "audio/Player.h"

#include <pipewire/pipewire.h>
#include "audio/PwCompat.h"
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace afterline::audio {

struct Player::Impl {
    Player *q;
    std::mutex mutex;  // guards readers (decoding happens on the PipeWire thread)
    OggOpusReader computer, microphone;
    bool hasComputer = false, hasMicrophone = false;
    float gainComputer = 1.f, gainMicrophone = 1.f;
    double rate = 1.0;
    double fraction = 0;   // resampling phase for rate != 1
    uint64_t position = 0;  // samples at 48 kHz
    uint64_t length = 0;
    pw_stream *stream = nullptr;
    spa_hook listener{};
    std::vector<float> a, b, mix, carry;  // carry: mixed samples read ahead, not yet played

    size_t readMixed(float *out, size_t n)
    {
        a.assign(n, 0.f);
        b.assign(n, 0.f);
        size_t got = 0;
        if (hasComputer)
            got = std::max(got, computer.read(a.data(), n));
        if (hasMicrophone)
            got = std::max(got, microphone.read(b.data(), n));
        for (size_t i = 0; i < n; ++i) {
            const float x = a[i] * gainComputer + b[i] * gainMicrophone;
            out[i] = std::clamp(x, -1.f, 1.f);
        }
        return got;
    }

    static void onProcess(void *data)
    {
        auto *self = static_cast<Impl *>(data);
        pw_buffer *buf = pw_stream_dequeue_buffer(self->stream);
        if (!buf)
            return;
        spa_data &sd = buf->buffer->datas[0];
        auto *dst = static_cast<float *>(sd.data);
        if (!dst) {
            pw_stream_queue_buffer(self->stream, buf);
            return;
        }
        uint32_t frames = sd.maxsize / (2 * sizeof(float));
#if AFTERLINE_PW_HAS_REQUESTED
        if (buf->requested)
            frames = std::min<uint32_t>(frames, uint32_t(buf->requested));
#endif
        {
            std::lock_guard<std::mutex> lock(self->mutex);
            if (!self->q->m_playing) {
                std::memset(dst, 0, frames * 2 * sizeof(float));
            } else {
                // Simple variable-rate playback by linear interpolation.
                const size_t need = size_t(std::ceil(frames * self->rate + self->fraction)) + 2;
                self->mix.swap(self->carry);
                self->carry.clear();
                size_t got = self->mix.size();
                if (got < need) {
                    self->mix.resize(need);
                    got += self->readMixed(self->mix.data() + got, need - got);
                }
                double pos = self->fraction;
                uint32_t i = 0;
                for (; i < frames; ++i) {
                    const size_t k = size_t(pos);
                    if (k + 1 >= got)
                        break;
                    const float f = float(pos - double(k));
                    const float v = self->mix[k] * (1 - f) + self->mix[k + 1] * f;
                    dst[2 * i] = dst[2 * i + 1] = v;
                    pos += self->rate;
                }
                for (uint32_t j = i; j < frames; ++j)
                    dst[2 * j] = dst[2 * j + 1] = 0.f;
                const size_t consumed = std::min(got, size_t(pos));
                self->fraction = pos - double(size_t(pos));
                self->carry.assign(self->mix.begin() + long(consumed), self->mix.begin() + long(got));
                self->position += consumed;
                if (i < frames && self->position >= self->length) {
                    self->q->m_playing = false;
                    self->q->m_finished = true;
                }
            }
        }
        sd.chunk->offset = 0;
        sd.chunk->stride = 2 * sizeof(float);
        sd.chunk->size = frames * 2 * sizeof(float);
        pw_stream_queue_buffer(self->stream, buf);
    }
};

Player::Player(PipeWireGraph &graph) : m_graph(graph) {}

Player::~Player() { close(); }

bool Player::open(const std::string &computerPath, const std::string &microphonePath, std::string *error)
{
    close();
    auto impl = std::make_unique<Impl>();
    impl->q = this;
    std::string err;
    if (!computerPath.empty() && impl->computer.open(computerPath, 48000, &err))
        impl->hasComputer = true;
    if (!microphonePath.empty() && impl->microphone.open(microphonePath, 48000, &err))
        impl->hasMicrophone = true;
    if (!impl->hasComputer && !impl->hasMicrophone) {
        if (error)
            *error = err.empty() ? "No audio to play" : err;
        return false;
    }
    impl->length = std::max(impl->hasComputer ? impl->computer.length() : 0,
                            impl->hasMicrophone ? impl->microphone.length() : 0);
    m_duration = impl->length / 48000.0;
    m_finished = false;
    if (!m_graph.running()) {
        if (error)
            *error = "PipeWire is not running";
        return false;
    }

    PipeWireGraph::Lock lock(m_graph);
    pw_properties *props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
                                             PW_KEY_MEDIA_ROLE, "Music", PW_KEY_APP_NAME, "AfterLine",
                                             PW_KEY_NODE_NAME, "afterline-playback",
                                             PW_KEY_NODE_DESCRIPTION, "AfterLine playback", nullptr);
    impl->stream = pw_stream_new(m_graph.core(), "AfterLine playback", props);
    if (!impl->stream) {
        if (error)
            *error = "Could not create a playback stream";
        return false;
    }
    static const pw_stream_events events = [] {
        pw_stream_events e{};
        e.version = PW_VERSION_STREAM_EVENTS;
        e.process = Impl::onProcess;
        return e;
    }();
    pw_stream_add_listener(impl->stream, &impl->listener, &events, impl.get());
    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.rate = 48000;
    info.channels = 2;
    info.position[0] = SPA_AUDIO_CHANNEL_FL;
    info.position[1] = SPA_AUDIO_CHANNEL_FR;
    uint8_t buffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const spa_pod *params[1] = {spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info)};
    if (pw_stream_connect(impl->stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                          pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params, 1) < 0) {
        pw_stream_destroy(impl->stream);
        if (error)
            *error = "Could not connect the playback stream";
        return false;
    }
    // Stay inactive until play() so idle meetings cost nothing.
    pw_stream_set_active(impl->stream, false);
    d = std::move(impl);
    return true;
}

void Player::close()
{
    if (!d)
        return;
    {
        PipeWireGraph::Lock lock(m_graph);
        if (d->stream) {
            spa_hook_remove(&d->listener);
            pw_stream_destroy(d->stream);
        }
    }
    d.reset();
    m_playing = false;
}

bool Player::play()
{
    if (!d)
        return false;
    if (m_finished || d->position >= d->length)
        seek(0);
    m_finished = false;
    m_playing = true;
    PipeWireGraph::Lock lock(m_graph);
    pw_stream_set_active(d->stream, true);
    return true;
}

void Player::pause()
{
    m_playing = false;
    if (!d)
        return;
    PipeWireGraph::Lock lock(m_graph);
    pw_stream_set_active(d->stream, false);
}

void Player::seek(double seconds)
{
    if (!d)
        return;
    std::lock_guard<std::mutex> lock(d->mutex);
    const uint64_t s = std::min<uint64_t>(uint64_t(std::max(0.0, seconds) * 48000), d->length);
    if (d->hasComputer)
        d->computer.seek(s);
    if (d->hasMicrophone)
        d->microphone.seek(s);
    d->position = s;
    d->fraction = 0;
    d->carry.clear();
    m_finished = false;
}

double Player::position() const
{
    if (!d)
        return 0;
    std::lock_guard<std::mutex> lock(d->mutex);
    return d->position / 48000.0;
}

void Player::setVolumes(float computer, float microphone)
{
    if (!d)
        return;
    std::lock_guard<std::mutex> lock(d->mutex);
    d->gainComputer = computer;
    d->gainMicrophone = microphone;
}

void Player::setRate(double rate)
{
    if (!d)
        return;
    std::lock_guard<std::mutex> lock(d->mutex);
    d->rate = std::clamp(rate, 0.5, 2.0);
}

}  // namespace afterline::audio
