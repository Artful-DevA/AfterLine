// SPDX-License-Identifier: MIT
// Test helper: converts a 16-bit PCM WAV (any rate, mono/stereo) to an
// AfterLine track (mono 48 kHz Ogg Opus), optionally placed after a delay.
//   wav2opus in.wav out.opus [leading-silence-seconds] [gain]
#include "core/OggOpus.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

int main(int argc, char **argv)
{
    if (argc < 3) {
        std::fprintf(stderr, "usage: wav2opus in.wav out.opus [delay] [gain]\n");
        return 2;
    }
    std::ifstream in(argv[1], std::ios::binary);
    std::vector<char> data((std::istreambuf_iterator<char>(in)), {});
    if (data.size() < 44 || std::memcmp(data.data(), "RIFF", 4) != 0) {
        std::fprintf(stderr, "not a WAV file\n");
        return 1;
    }
    int channels = 1, rate = 16000, bits = 16;
    size_t pos = 12, pcmStart = 0, pcmBytes = 0;
    while (pos + 8 <= data.size()) {
        uint32_t len;
        std::memcpy(&len, &data[pos + 4], 4);
        if (std::memcmp(&data[pos], "fmt ", 4) == 0) {
            channels = *reinterpret_cast<uint16_t *>(&data[pos + 10]);
            rate = int(*reinterpret_cast<uint32_t *>(&data[pos + 12]));
            bits = *reinterpret_cast<uint16_t *>(&data[pos + 22]);
        } else if (std::memcmp(&data[pos], "data", 4) == 0) {
            pcmStart = pos + 8;
            pcmBytes = std::min<size_t>(len, data.size() - pcmStart);
            break;
        }
        pos += 8 + len + (len & 1);
    }
    if (bits != 16 || !pcmStart) {
        std::fprintf(stderr, "only 16-bit PCM WAV is supported\n");
        return 1;
    }
    const auto *pcm = reinterpret_cast<const int16_t *>(&data[pcmStart]);
    const size_t frames = pcmBytes / 2 / size_t(channels);
    const float gain = argc > 4 ? float(std::atof(argv[4])) : 1.f;
    // Linear-interpolation resample to 48 kHz (good enough for tests).
    const size_t outFrames = size_t(double(frames) * 48000 / rate);
    std::vector<float> out(outFrames);
    for (size_t i = 0; i < outFrames; ++i) {
        const double src = double(i) * rate / 48000;
        const size_t a = size_t(src), b = std::min(a + 1, frames - 1);
        const double f = src - double(a);
        auto sample = [&](size_t k) {
            double s = 0;
            for (int c = 0; c < channels; ++c)
                s += pcm[k * size_t(channels) + size_t(c)];
            return s / channels / 32768.0;
        };
        out[i] = float((sample(a) * (1 - f) + sample(b) * f) * gain);
    }
    afterline::OggOpusWriter w;
    std::string err;
    if (!w.open(argv[2], &err)) {
        std::fprintf(stderr, "%s\n", err.c_str());
        return 1;
    }
    if (argc > 3)
        w.writeSilence(size_t(std::atof(argv[3]) * 48000));
    w.write(out.data(), out.size());
    return w.close() ? 0 : 1;
}
