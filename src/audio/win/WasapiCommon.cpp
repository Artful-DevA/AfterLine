// SPDX-License-Identifier: MIT
#include "audio/win/WasapiCommon.h"

// Defines the property key here (instead of needing an import library).
#include <initguid.h>
#include <functiondiscoverykeys_devpkey.h>
#include <ksmedia.h>
#include <tlhelp32.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace afterline::audio::win {

std::string utf8(const wchar_t *s)
{
    if (!s || !*s)
        return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    std::string out(size_t(std::max(0, n - 1)), '\0');
    if (n > 1)
        WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), n, nullptr, nullptr);
    return out;
}

std::wstring wide(const std::string &s)
{
    if (s.empty())
        return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring out(size_t(std::max(0, n - 1)), L'\0');
    if (n > 1)
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, out.data(), n);
    return out;
}

std::string hresultText(HRESULT hr)
{
    switch (hr) {
    case AUDCLNT_E_DEVICE_INVALIDATED: return "The audio device was disconnected";
    case AUDCLNT_E_DEVICE_IN_USE: return "The audio device is in use by another application";
    case AUDCLNT_E_SERVICE_NOT_RUNNING: return "The Windows audio service is not running";
    case E_ACCESSDENIED: return "Windows denied access to the microphone (check Settings → Privacy → Microphone)";
    default: break;
    }
    char buf[64];
    std::snprintf(buf, sizeof buf, "Windows audio error 0x%08lX", static_cast<unsigned long>(hr));
    return buf;
}

Com<IMMDeviceEnumerator> deviceEnumerator()
{
    Com<IMMDeviceEnumerator> e;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                     reinterpret_cast<void **>(e.put()));
    return e;
}

Com<IMMDevice> endpoint(const std::string &id, EDataFlow flow)
{
    Com<IMMDevice> device;
    Com<IMMDeviceEnumerator> e = deviceEnumerator();
    if (!e)
        return device;
    if (id.empty())
        e->GetDefaultAudioEndpoint(flow, eConsole, device.put());
    else
        e->GetDevice(wide(id).c_str(), device.put());
    if (device) {
        DWORD state = 0;
        if (FAILED(device->GetState(&state)) || state != DEVICE_STATE_ACTIVE)
            device.reset();
    }
    return device;
}

std::string endpointId(IMMDevice *device)
{
    if (!device)
        return {};
    LPWSTR id = nullptr;
    if (FAILED(device->GetId(&id)))
        return {};
    std::string out = utf8(id);
    CoTaskMemFree(id);
    return out;
}

std::string friendlyName(IMMDevice *device)
{
    Com<IPropertyStore> props;
    if (!device || FAILED(device->OpenPropertyStore(STGM_READ, props.put())))
        return {};
    PROPVARIANT v;
    PropVariantInit(&v);
    std::string out;
    if (SUCCEEDED(props->GetValue(PKEY_Device_FriendlyName, &v)) && v.vt == VT_LPWSTR)
        out = utf8(v.pwszVal);
    PropVariantClear(&v);
    return out;
}

// ---------------------------------------------------------------------------

void ToMono48k::setFormat(const WAVEFORMATEX *f)
{
    m_channels = std::max<int>(1, f->nChannels);
    m_rate = int(f->nSamplesPerSec);
    m_blockAlign = f->nBlockAlign;
    bool isFloat = f->wFormatTag == WAVE_FORMAT_IEEE_FLOAT;
    int bits = f->wBitsPerSample;
    if (f->wFormatTag == WAVE_FORMAT_EXTENSIBLE && f->cbSize >= 22) {
        const auto *x = reinterpret_cast<const WAVEFORMATEXTENSIBLE *>(f);
        isFloat = IsEqualGUID(x->SubFormat, KSDATAFORMAT_SUBTYPE_IEEE_FLOAT);
        if (x->Samples.wValidBitsPerSample)
            bits = f->wBitsPerSample;  // container size decides the layout
    }
    if (isFloat)
        m_sample = Sample::Float32;
    else if (bits == 16)
        m_sample = Sample::Int16;
    else if (bits == 24)
        m_sample = Sample::Int24;
    else
        m_sample = Sample::Int32;
    m_phase = 0;
    m_last = 0;
}

void ToMono48k::convert(const BYTE *data, UINT32 frames, bool silent, std::vector<float> &out)
{
    m_mono.resize(frames);
    if (silent || !data) {
        std::fill(m_mono.begin(), m_mono.end(), 0.f);
    } else {
        const int bytes = m_blockAlign / m_channels;
        for (UINT32 i = 0; i < frames; ++i) {
            const BYTE *frame = data + size_t(i) * size_t(m_blockAlign);
            float sum = 0;
            for (int c = 0; c < m_channels; ++c) {
                const BYTE *s = frame + c * bytes;
                float v = 0;
                switch (m_sample) {
                case Sample::Float32: std::memcpy(&v, s, 4); break;
                case Sample::Int16: { int16_t x; std::memcpy(&x, s, 2); v = x / 32768.f; break; }
                case Sample::Int24: {
                    const int32_t x = (int32_t(s[0]) << 8 | int32_t(s[1]) << 16 | int32_t(s[2]) << 24) >> 8;
                    v = x / 8388608.f;
                    break;
                }
                case Sample::Int32: { int32_t x; std::memcpy(&x, s, 4); v = float(x / 2147483648.0); break; }
                }
                sum += v;
            }
            m_mono[i] = sum / float(m_channels);
        }
    }
    if (m_rate == 48000) {
        out.insert(out.end(), m_mono.begin(), m_mono.end());
        return;
    }
    // Linear resampling to 48 kHz; m_phase carries the position between packets
    // (an index into [m_last, m_mono...]).
    const double step = double(m_rate) / 48000.0;
    double pos = m_phase;
    while (pos < double(frames)) {
        const size_t k = size_t(pos);
        const float frac = float(pos - double(k));
        const float a = k == 0 ? m_last : m_mono[k - 1];
        const float b = m_mono[std::min<size_t>(k, frames - 1)];
        out.push_back(a + (b - a) * frac);
        pos += step;
    }
    m_phase = pos - double(frames);
    if (frames)
        m_last = m_mono[frames - 1];
}

// ---------------------------------------------------------------------------

namespace {
std::string normaliseExe(const wchar_t *name)
{
    std::string s = utf8(name);
    const size_t slash = s.find_last_of("\\/");
    if (slash != std::string::npos)
        s = s.substr(slash + 1);
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::tolower(c)); });
    if (s.size() > 4 && s.compare(s.size() - 4, 4, ".exe") == 0)
        s.erase(s.size() - 4);
    return s;
}
}  // namespace

std::vector<ProcessEntry> processes()
{
    std::vector<ProcessEntry> out;
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE)
        return out;
    PROCESSENTRY32W e{};
    e.dwSize = sizeof e;
    for (BOOL ok = Process32FirstW(snap, &e); ok; ok = Process32NextW(snap, &e))
        out.push_back({e.th32ProcessID, e.th32ParentProcessID, normaliseExe(e.szExeFile)});
    CloseHandle(snap);
    return out;
}

std::string processExe(DWORD pid)
{
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!h)
        return {};
    wchar_t path[MAX_PATH * 2];
    DWORD size = DWORD(std::size(path));
    std::string out;
    if (QueryFullProcessImageNameW(h, 0, path, &size))
        out = normaliseExe(path);
    CloseHandle(h);
    return out;
}

}  // namespace afterline::audio::win
