// SPDX-License-Identifier: MIT
//
// Small helpers shared by AfterLine's Windows audio code (WASAPI): COM
// pointers and initialisation, text conversion, converting whatever format a
// device delivers into mono 48 kHz float, and finding applications' processes.
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <audioclient.h>
#include <mmdeviceapi.h>

#include <cstdint>
#include <string>
#include <vector>

namespace afterline::audio::win {

// Minimal owning COM pointer.
template <class T>
class Com {
public:
    Com() = default;
    ~Com() { reset(); }
    Com(const Com &o) : p(o.p) { if (p) p->AddRef(); }
    Com &operator=(const Com &o)
    {
        if (this != &o) {
            reset();
            p = o.p;
            if (p)
                p->AddRef();
        }
        return *this;
    }
    T *operator->() const { return p; }
    T *get() const { return p; }
    T **put() { reset(); return &p; }
    explicit operator bool() const { return p != nullptr; }
    void reset()
    {
        if (p)
            p->Release();
        p = nullptr;
    }

private:
    T *p = nullptr;
};

// CoInitializeEx for the lifetime of a thread.
struct ComThread {
    HRESULT hr;
    ComThread() : hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~ComThread()
    {
        if (SUCCEEDED(hr))
            CoUninitialize();
    }
};

std::string utf8(const wchar_t *s);
std::wstring wide(const std::string &s);
std::string hresultText(HRESULT hr);

Com<IMMDeviceEnumerator> deviceEnumerator();
// Endpoint by id; empty id = the default device for that direction.
Com<IMMDevice> endpoint(const std::string &id, EDataFlow flow);
std::string endpointId(IMMDevice *device);
std::string friendlyName(IMMDevice *device);

// Turns packets in any PCM/float format into mono float at 48 kHz.
class ToMono48k {
public:
    void setFormat(const WAVEFORMATEX *format);
    // `silent`: the packet is flagged silent (contents undefined).
    void convert(const BYTE *data, UINT32 frames, bool silent, std::vector<float> &out);

private:
    enum class Sample { Float32, Int16, Int24, Int32 } m_sample = Sample::Float32;
    int m_channels = 2;
    int m_rate = 48000;
    int m_blockAlign = 8;
    double m_phase = 0;   // resampler position, in input frames
    float m_last = 0;     // last input sample, for interpolation across packets
    std::vector<float> m_mono;
};

// Processes by executable: lower-case file name without ".exe".
struct ProcessEntry {
    DWORD pid = 0;
    DWORD parent = 0;
    std::string exe;
};
std::vector<ProcessEntry> processes();
// Lower-case executable name (without ".exe") of a process, or empty.
std::string processExe(DWORD pid);

}  // namespace afterline::audio::win
