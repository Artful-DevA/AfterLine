// SPDX-License-Identifier: MIT
//
// Windows implementation of Player (see Player.h): the two tracks mixed and
// played through the default output with WASAPI. The output client only runs
// while playing, so an idle meeting keeps the sound device asleep.
#include "audio/Player.h"
#include "audio/win/WasapiCommon.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <thread>

namespace afterline::audio {

using namespace win;

struct Player::Impl {
    Player *q = nullptr;
    std::mutex mutex;  // guards the readers and playback state
    OggOpusReader computer, microphone;
    bool hasComputer = false, hasMicrophone = false;
    float gainComputer = 1.f, gainMicrophone = 1.f;
    double rate = 1.0;
    double fraction = 0;
    uint64_t position = 0;  // samples at 48 kHz
    uint64_t length = 0;
    std::vector<float> a, b, mix, carry;

    std::thread thread;
    HANDLE wake = nullptr;      // play/pause/close
    HANDLE bufferEvent = nullptr;
    std::atomic<bool> quit{false};
    std::string error;

    size_t readMixed(float *out, size_t n)
    {
        a.assign(n, 0.f);
        b.assign(n, 0.f);
        size_t got = 0;
        if (hasComputer)
            got = std::max(got, computer.read(a.data(), n));
        if (hasMicrophone)
            got = std::max(got, microphone.read(b.data(), n));
        for (size_t i = 0; i < n; ++i)
            out[i] = std::clamp(a[i] * gainComputer + b[i] * gainMicrophone, -1.f, 1.f);
        return got;
    }

    // Fills `frames` stereo frames (48 kHz float) and advances the position.
    void render(float *dst, uint32_t frames)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (!q->m_playing) {
            std::memset(dst, 0, size_t(frames) * 2 * sizeof(float));
            return;
        }
        const size_t need = size_t(std::ceil(frames * rate + fraction)) + 2;
        mix.swap(carry);
        carry.clear();
        size_t got = mix.size();
        if (got < need) {
            mix.resize(need);
            got += readMixed(mix.data() + got, need - got);
        }
        double pos = fraction;
        uint32_t i = 0;
        for (; i < frames; ++i) {
            const size_t k = size_t(pos);
            if (k + 1 >= got)
                break;
            const float f = float(pos - double(k));
            const float v = mix[k] * (1 - f) + mix[k + 1] * f;
            dst[2 * i] = dst[2 * i + 1] = v;
            pos += rate;
        }
        for (uint32_t j = i; j < frames; ++j)
            dst[2 * j] = dst[2 * j + 1] = 0.f;
        const size_t consumed = std::min(got, size_t(pos));
        fraction = pos - double(size_t(pos));
        carry.assign(mix.begin() + long(consumed), mix.begin() + long(got));
        position += consumed;
        if (i < frames && position >= length) {
            q->m_playing = false;
            q->m_finished = true;
        }
    }

    void run()
    {
        ComThread com;
        while (!quit) {
            if (!q->m_playing) {
                WaitForSingleObject(wake, INFINITE);
                continue;
            }
            Com<IMMDevice> device = endpoint({}, eRender);
            Com<IAudioClient> client;
            HRESULT hr = device ? device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                                   reinterpret_cast<void **>(client.put()))
                                : E_FAIL;
            WAVEFORMATEX f{};
            f.wFormatTag = WAVE_FORMAT_IEEE_FLOAT;
            f.nChannels = 2;
            f.nSamplesPerSec = 48000;
            f.wBitsPerSample = 32;
            f.nBlockAlign = 8;
            f.nAvgBytesPerSec = 48000 * 8;
            if (SUCCEEDED(hr))
                hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                        AUDCLNT_STREAMFLAGS_EVENTCALLBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                                            AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY,
                                        1000000, 0, &f, nullptr);
            Com<IAudioRenderClient> render;
            UINT32 bufferFrames = 0;
            if (SUCCEEDED(hr))
                hr = client->SetEventHandle(bufferEvent);
            if (SUCCEEDED(hr))
                hr = client->GetBufferSize(&bufferFrames);
            if (SUCCEEDED(hr))
                hr = client->GetService(__uuidof(IAudioRenderClient), reinterpret_cast<void **>(render.put()));
            if (SUCCEEDED(hr))
                hr = client->Start();
            if (FAILED(hr)) {
                error = hresultText(hr);
                q->m_playing = false;
                continue;
            }
            while (!quit && q->m_playing) {
                HANDLE waits[2] = {bufferEvent, wake};
                WaitForMultipleObjects(2, waits, FALSE, 200);
                UINT32 padding = 0;
                if (FAILED(client->GetCurrentPadding(&padding)))
                    break;  // device gone: reopen on the next play
                const UINT32 frames = bufferFrames - padding;
                if (!frames)
                    continue;
                BYTE *data = nullptr;
                if (FAILED(render->GetBuffer(frames, &data)))
                    break;
                this->render(reinterpret_cast<float *>(data), frames);
                render->ReleaseBuffer(frames, 0);
            }
            client->Stop();
        }
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
    m_playing = false;
    impl->wake = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    impl->bufferEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
    impl->thread = std::thread([p = impl.get()] { p->run(); });
    d = std::move(impl);
    return true;
}

void Player::close()
{
    if (!d)
        return;
    m_playing = false;
    d->quit = true;
    SetEvent(d->wake);
    if (d->thread.joinable())
        d->thread.join();
    CloseHandle(d->wake);
    CloseHandle(d->bufferEvent);
    d.reset();
}

bool Player::play()
{
    if (!d)
        return false;
    if (m_finished || d->position >= d->length)
        seek(0);
    m_finished = false;
    m_playing = true;
    SetEvent(d->wake);
    return true;
}

void Player::pause()
{
    m_playing = false;
    if (d)
        SetEvent(d->wake);
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
