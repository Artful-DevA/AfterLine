// SPDX-License-Identifier: MIT
//
// Test helper: an "application" that plays a sine tone through PipeWire with
// a chosen identity, standing in for Firefox, Zoom or a music player in the
// recording tests. Works with every PipeWire version AfterLine supports
// (pw-play gained -P only in later releases).
//
//   afterline-fake-app --binary zoom --freq 440 --seconds 8 [--target node.name]
#include "audio/PwCompat.h"

#include <pipewire/pipewire.h>
#include <spa/param/audio/format-utils.h>
#include <spa/pod/builder.h>

#include <cmath>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {

struct App {
    pw_main_loop *loop = nullptr;
    pw_stream *stream = nullptr;
    double freq = 440, phase = 0;
    long remaining = 0;  // frames
};

void onProcess(void *data)
{
    auto *app = static_cast<App *>(data);
    pw_buffer *b = pw_stream_dequeue_buffer(app->stream);
    if (!b)
        return;
    spa_data &d = b->buffer->datas[0];
    auto *out = static_cast<float *>(d.data);
    if (!out) {
        pw_stream_queue_buffer(app->stream, b);
        return;
    }
    uint32_t frames = d.maxsize / (2 * sizeof(float));
#if AFTERLINE_PW_HAS_REQUESTED
    if (b->requested)
        frames = std::min<uint32_t>(frames, uint32_t(b->requested));
#endif
    for (uint32_t i = 0; i < frames; ++i) {
        const float v = app->remaining > 0 ? float(0.3 * std::sin(app->phase)) : 0.f;
        app->phase += 2 * M_PI * app->freq / 48000.0;
        if (app->phase > 2 * M_PI)
            app->phase -= 2 * M_PI;
        out[2 * i] = out[2 * i + 1] = v;
        --app->remaining;
    }
    d.chunk->offset = 0;
    d.chunk->stride = 2 * sizeof(float);
    d.chunk->size = frames * 2 * sizeof(float);
    pw_stream_queue_buffer(app->stream, b);
    if (app->remaining < -4800)
        pw_main_loop_quit(app->loop);
}

App *g_app = nullptr;
void onSignal(int) { if (g_app) pw_main_loop_quit(g_app->loop); }

}  // namespace

int main(int argc, char **argv)
{
    std::string binary = "fakeapp", target;
    double freq = 440, seconds = 5;
    for (int i = 1; i + 1 < argc; i += 2) {
        const std::string a = argv[i];
        if (a == "--binary") binary = argv[i + 1];
        else if (a == "--freq") freq = std::atof(argv[i + 1]);
        else if (a == "--seconds") seconds = std::atof(argv[i + 1]);
        else if (a == "--target") target = argv[i + 1];
    }
    pw_init(&argc, &argv);
    App app;
    g_app = &app;
    app.freq = freq;
    app.remaining = long(seconds * 48000);
    app.loop = pw_main_loop_new(nullptr);
    std::signal(SIGTERM, onSignal);
    std::signal(SIGINT, onSignal);

    pw_properties *props = pw_properties_new(PW_KEY_MEDIA_TYPE, "Audio", PW_KEY_MEDIA_CATEGORY, "Playback",
                                             PW_KEY_MEDIA_ROLE, "Music", PW_KEY_APP_NAME, binary.c_str(),
                                             PW_KEY_APP_PROCESS_BINARY, binary.c_str(), nullptr);
    if (!target.empty()) {
        pw_properties_set(props, PW_KEY_TARGET_OBJECT, target.c_str());
        pw_properties_set(props, "node.target", target.c_str());
    }
    app.stream = pw_stream_new_simple(pw_main_loop_get_loop(app.loop), binary.c_str(), props,
                                      [] {
                                          static pw_stream_events e{};
                                          e.version = PW_VERSION_STREAM_EVENTS;
                                          e.process = onProcess;
                                          return &e;
                                      }(),
                                      &app);
    spa_audio_info_raw info{};
    info.format = SPA_AUDIO_FORMAT_F32;
    info.rate = 48000;
    info.channels = 2;
    info.position[0] = SPA_AUDIO_CHANNEL_FL;
    info.position[1] = SPA_AUDIO_CHANNEL_FR;
    uint8_t buffer[1024];
    spa_pod_builder builder = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const spa_pod *params[1] = {spa_format_audio_raw_build(&builder, SPA_PARAM_EnumFormat, &info)};
    pw_stream_connect(app.stream, PW_DIRECTION_OUTPUT, PW_ID_ANY,
                      pw_stream_flags(PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS), params, 1);
    pw_main_loop_run(app.loop);
    pw_stream_destroy(app.stream);
    pw_main_loop_destroy(app.loop);
    pw_deinit();
    return 0;
}
