// SPDX-License-Identifier: MIT
//
// Windows implementation of CaptureStream (see CaptureStream.h), on WASAPI:
//  - the whole computer or one output: loopback capture of that output;
//  - one application: process loopback (Windows 10 version 2004 and later),
//    which captures the application and its child processes (browsers play
//    sound from helper processes), and nothing else;
//  - a microphone: ordinary capture.
// Each stream runs its own thread that reopens the source when it goes away
// (a headset unplugged, the default device changed, the app restarted).
#include "audio/CaptureStream.h"
#include "audio/win/WasapiCommon.h"

#if __has_include(<audioclientactivationparams.h>)
#include <audioclientactivationparams.h>
#else
// Declarations from audioclientactivationparams.h (Windows SDK 10.0.20348+).
typedef enum AUDIOCLIENT_ACTIVATION_TYPE {
    AUDIOCLIENT_ACTIVATION_TYPE_DEFAULT = 0,
    AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK = 1
} AUDIOCLIENT_ACTIVATION_TYPE;
typedef enum PROCESS_LOOPBACK_MODE {
    PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE = 0,
    PROCESS_LOOPBACK_MODE_EXCLUDE_TARGET_PROCESS_TREE = 1
} PROCESS_LOOPBACK_MODE;
typedef struct AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS {
    DWORD TargetProcessId;
    PROCESS_LOOPBACK_MODE ProcessLoopbackMode;
} AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS;
typedef struct AUDIOCLIENT_ACTIVATION_PARAMS {
    AUDIOCLIENT_ACTIVATION_TYPE ActivationType;
    union {
        AUDIOCLIENT_PROCESS_LOOPBACK_PARAMS ProcessLoopbackParams;
    };
} AUDIOCLIENT_ACTIVATION_PARAMS;
#define VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK L"VAD\\Process_Loopback"
#endif

#include <atomic>
#include <chrono>
#include <thread>

namespace afterline::audio {

using namespace win;

const char *stateName(CaptureStream::State s)
{
    switch (s) {
    case CaptureStream::State::Idle: return "idle";
    case CaptureStream::State::Waiting: return "waiting";
    case CaptureStream::State::Active: return "active";
    case CaptureStream::State::Lost: return "lost";
    case CaptureStream::State::Failed: return "failed";
    }
    return "?";
}

namespace {

// ActivateAudioInterfaceAsync calls back on another thread; this waits for it.
class ActivationHandler : public IActivateAudioInterfaceCompletionHandler, public IAgileObject {
public:
    ActivationHandler() : m_done(CreateEventW(nullptr, TRUE, FALSE, nullptr)) {}
    virtual ~ActivationHandler() { CloseHandle(m_done); }

    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void **out) override
    {
        if (IsEqualIID(riid, __uuidof(IUnknown)) ||
            IsEqualIID(riid, __uuidof(IActivateAudioInterfaceCompletionHandler))) {
            *out = static_cast<IActivateAudioInterfaceCompletionHandler *>(this);
        } else if (IsEqualIID(riid, __uuidof(IAgileObject))) {
            *out = static_cast<IAgileObject *>(this);
        } else {
            *out = nullptr;
            return E_NOINTERFACE;
        }
        AddRef();
        return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ULONG(++m_refs); }
    ULONG STDMETHODCALLTYPE Release() override
    {
        const long n = --m_refs;
        if (n == 0)
            delete this;
        return ULONG(n);
    }
    HRESULT STDMETHODCALLTYPE ActivateCompleted(IActivateAudioInterfaceAsyncOperation *op) override
    {
        HRESULT activated = E_FAIL;
        IUnknown *unknown = nullptr;
        m_result = op->GetActivateResult(&activated, &unknown);
        if (SUCCEEDED(m_result))
            m_result = activated;
        if (SUCCEEDED(m_result) && unknown)
            m_result = unknown->QueryInterface(__uuidof(IAudioClient), reinterpret_cast<void **>(m_client.put()));
        if (unknown)
            unknown->Release();
        SetEvent(m_done);
        return S_OK;
    }
    HRESULT wait(Com<IAudioClient> &client)
    {
        if (WaitForSingleObject(m_done, 5000) != WAIT_OBJECT_0)
            return HRESULT_FROM_WIN32(ERROR_TIMEOUT);
        client = m_client;
        return m_result;
    }

private:
    std::atomic<long> m_refs{1};
    HANDLE m_done;
    HRESULT m_result = E_FAIL;
    Com<IAudioClient> m_client;
};

// The process whose tree plays the application's audio: the topmost process
// with a matching executable (a browser's main process, not its helpers).
DWORD findApplication(const std::string &key)
{
    const auto procs = processes();
    for (const auto &p : procs) {
        NodeInfo n;
        n.appBinary = p.exe;
        if (appKey(n) != key)
            continue;
        bool parentMatches = false;
        for (const auto &q : procs)
            if (q.pid == p.parent && q.exe == p.exe)
                parentMatches = true;
        if (!parentMatches)
            return p.pid;
    }
    return 0;
}

}  // namespace

struct CaptureStream::Impl {
    CaptureStream *q = nullptr;
    DataFn onData;
    std::thread thread;
    std::atomic<bool> quit{false};
    HANDLE wake = nullptr;  // set to stop early

    const Config &config() const { return q->m_config; }

    // Opens the source. Returns the client (started) or an error message.
    HRESULT open(Com<IAudioClient> &client, ToMono48k &convert, std::string &openedId, DWORD &appPid,
                 HANDLE event, std::string &message)
    {
        const Kind kind = config().kind;
        WAVEFORMATEX *mix = nullptr;
        HRESULT hr = S_OK;
        DWORD flags = AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
        const REFERENCE_TIME bufferTime = 2000000;  // 200 ms
        if (kind == Kind::Application) {
            appPid = findApplication(config().target);
            if (!appPid) {
                message = "Waiting for the application to start";
                return S_FALSE;
            }
            AUDIOCLIENT_ACTIVATION_PARAMS params{};
            params.ActivationType = AUDIOCLIENT_ACTIVATION_TYPE_PROCESS_LOOPBACK;
            params.ProcessLoopbackParams.TargetProcessId = appPid;
            params.ProcessLoopbackParams.ProcessLoopbackMode = PROCESS_LOOPBACK_MODE_INCLUDE_TARGET_PROCESS_TREE;
            PROPVARIANT prop;
            PropVariantInit(&prop);
            prop.vt = VT_BLOB;
            prop.blob.cbSize = sizeof params;
            prop.blob.pBlobData = reinterpret_cast<BYTE *>(&params);
            auto *handler = new ActivationHandler;
            IActivateAudioInterfaceAsyncOperation *op = nullptr;
            hr = ActivateAudioInterfaceAsync(VIRTUAL_AUDIO_DEVICE_PROCESS_LOOPBACK, __uuidof(IAudioClient), &prop,
                                             handler, &op);
            if (SUCCEEDED(hr))
                hr = handler->wait(client);
            if (op)
                op->Release();
            handler->Release();
            if (FAILED(hr)) {
                message = "Recording one application needs Windows 10 version 2004 or later (" +
                          hresultText(hr) + ")";
                return hr;
            }
            // Process loopback has no mix format: ask for 48 kHz stereo 16-bit.
            WAVEFORMATEX f{};
            f.wFormatTag = WAVE_FORMAT_PCM;
            f.nChannels = 2;
            f.nSamplesPerSec = 48000;
            f.wBitsPerSample = 16;
            f.nBlockAlign = 4;
            f.nAvgBytesPerSec = 48000 * 4;
            hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED,
                                    flags | AUDCLNT_STREAMFLAGS_LOOPBACK | AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM,
                                    bufferTime, 0, &f, nullptr);
            if (FAILED(hr)) {
                message = hresultText(hr);
                return hr;
            }
            convert.setFormat(&f);
            openedId = config().target;
        } else {
            const bool input = CaptureStream::isInput(kind);
            const bool followDefault = kind == Kind::DefaultOutput || kind == Kind::DefaultInput;
            Com<IMMDevice> device = endpoint(followDefault ? std::string() : config().target,
                                             input ? eCapture : eRender);
            if (!device) {
                message = followDefault ? "No audio device is available" : "The selected audio device is not available";
                return S_FALSE;
            }
            openedId = endpointId(device.get());
            hr = device->Activate(__uuidof(IAudioClient), CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void **>(client.put()));
            if (SUCCEEDED(hr))
                hr = client->GetMixFormat(&mix);
            if (SUCCEEDED(hr))
                hr = client->Initialize(AUDCLNT_SHAREMODE_SHARED, flags | (input ? 0 : AUDCLNT_STREAMFLAGS_LOOPBACK),
                                        bufferTime, 0, mix, nullptr);
            if (SUCCEEDED(hr))
                convert.setFormat(mix);
            if (mix)
                CoTaskMemFree(mix);
            if (FAILED(hr)) {
                message = hresultText(hr);
                return hr;
            }
        }
        hr = client->SetEventHandle(event);
        if (SUCCEEDED(hr))
            hr = client->Start();
        if (FAILED(hr))
            message = hresultText(hr);
        return hr;
    }

    void run()
    {
        ComThread com;
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        std::vector<float> out;
        bool everOpened = false;
        const Kind kind = config().kind;
        const bool followDefault = kind == Kind::DefaultOutput || kind == Kind::DefaultInput;
        while (!quit) {
            Com<IAudioClient> client;
            ToMono48k convert;
            std::string openedId, message;
            DWORD appPid = 0;
            const HRESULT hr = open(client, convert, openedId, appPid, event, message);
            if (hr != S_OK) {
                client.reset();
                if (kind == Kind::Application && hr == S_FALSE)
                    q->setState(State::Waiting, message);
                else
                    q->setState(everOpened ? State::Lost : State::Waiting, message);
                WaitForSingleObject(wake, 1000);
                continue;
            }
            Com<IAudioCaptureClient> capture;
            client->GetService(__uuidof(IAudioCaptureClient), reinterpret_cast<void **>(capture.put()));
            if (!capture) {
                q->setState(State::Failed, "The audio device cannot be recorded");
                WaitForSingleObject(wake, 1000);
                continue;
            }
            if (everOpened)
                q->m_reconnects++;
            everOpened = true;
            q->m_linkedStreams = kind == Kind::Application ? 1 : 0;
            q->setState(State::Active, {});
            HANDLE app = appPid ? OpenProcess(SYNCHRONIZE, FALSE, appPid) : nullptr;
            auto lastCheck = std::chrono::steady_clock::now();
            bool reopen = false;
            while (!quit && !reopen) {
                HANDLE waits[3] = {event, wake, app};
                WaitForMultipleObjects(app ? 3 : 2, waits, FALSE, 100);
                if (app && WaitForSingleObject(app, 0) == WAIT_OBJECT_0) {
                    reopen = true;  // the application quit: wait for it to come back
                    break;
                }
                UINT32 packet = 0;
                HRESULT r = capture->GetNextPacketSize(&packet);
                while (SUCCEEDED(r) && packet > 0) {
                    BYTE *data = nullptr;
                    UINT32 frames = 0;
                    DWORD bufferFlags = 0;
                    r = capture->GetBuffer(&data, &frames, &bufferFlags, nullptr, nullptr);
                    if (FAILED(r))
                        break;
                    out.clear();
                    convert.convert(data, frames, bufferFlags & AUDCLNT_BUFFERFLAGS_SILENT, out);
                    capture->ReleaseBuffer(frames);
                    if (!out.empty())
                        onData(out.data(), out.size());
                    r = capture->GetNextPacketSize(&packet);
                }
                if (FAILED(r)) {
                    q->setState(State::Lost, hresultText(r));
                    reopen = true;
                }
                // Follow the default device when it changes in Windows settings.
                const auto now = std::chrono::steady_clock::now();
                if (followDefault && now - lastCheck > std::chrono::seconds(1)) {
                    lastCheck = now;
                    Com<IMMDevice> def = endpoint({}, kind == Kind::DefaultInput ? eCapture : eRender);
                    if (def && endpointId(def.get()) != openedId)
                        reopen = true;
                }
            }
            if (app)
                CloseHandle(app);
            client->Stop();
            q->m_linkedStreams = 0;
            if (!quit && kind == Kind::Application)
                q->setState(State::Waiting, "Waiting for the application to play audio");
        }
        CloseHandle(event);
    }
};

CaptureStream::CaptureStream(PipeWireGraph &graph, Config config, DataFn onData)
    : m_graph(graph), m_config(std::move(config))
{
    d = std::make_unique<Impl>();
    d->q = this;
    d->onData = std::move(onData);
}

CaptureStream::~CaptureStream() { stop(); }

void CaptureStream::setState(State s, const std::string &message)
{
    m_state = s;
    std::lock_guard<std::mutex> lock(m_messageMutex);
    m_message = message;
}

std::string CaptureStream::message() const
{
    std::lock_guard<std::mutex> lock(m_messageMutex);
    return m_message;
}

bool CaptureStream::start(std::string *error)
{
    if (d->thread.joinable())
        return true;
    if (m_config.kind == Kind::Application && m_config.target.empty()) {
        if (error)
            *error = "No application selected";
        return false;
    }
    if (m_config.kind == Kind::OutputDevice || m_config.kind == Kind::InputDevice) {
        ComThread com;
        if (!endpoint(m_config.target, m_config.kind == Kind::InputDevice ? eCapture : eRender)) {
            if (error)
                *error = "The selected audio device is not available";
            return false;
        }
    }
    d->quit = false;
    d->wake = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    setState(State::Waiting, "Connecting");
    d->thread = std::thread([impl = d.get()] { impl->run(); });
    return true;
}

void CaptureStream::stop()
{
    if (!d || !d->thread.joinable())
        return;
    d->quit = true;
    SetEvent(d->wake);
    d->thread.join();
    CloseHandle(d->wake);
    d->wake = nullptr;
    m_state = State::Idle;
}

}  // namespace afterline::audio
