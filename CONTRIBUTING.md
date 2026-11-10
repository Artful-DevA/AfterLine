# Contributing to AfterLine

Thank you for helping. AfterLine is used for conversations people cannot
repeat, so the rule that overrides everything else is: **never risk the
recording.** A recording without a summary is still useful; a summary without
a reliable recording is not.

## Architecture

```
src/core/      Qt-light building blocks: the Ogg Opus recording format
               (crash-tolerant writer, seeking reader), transcript and notes
               models, exports, paths
src/audio/     PipeWire: graph tracking (PipeWireGraph), capture of devices
               and of applications by identity (CaptureStream), per-track
               writer thread with timeline gap filling (TrackWriter),
               Recorder, Player
src/app/       Meeting library (folders + SQLite FTS5 index), model catalog
               and verified downloads, background processing queue,
               recording controller (warnings, disk space, suspend), settings
src/ui/        Qt Widgets interface
engines/       Separate executables, each with its own ggml:
               transcriber/ (whisper.cpp)  summarizer/ (llama.cpp)
               They talk to the app with JSON lines on stdout.
tests/         QtTest suites; test_pipewire runs against a real PipeWire
               session started by tests/run-pipewire-tests.sh
packaging/     .deb/.rpm, AppImage and Arch build scripts, desktop metadata
website/       Static site; netlify/functions/ serves direct downloads
```

Design rules:

- **Recording never waits on anything.** PipeWire callbacks only copy into a
  buffer; encoding and disk I/O happen on each track's own thread.
- **Every file write is safe.** Audio is appended page by page; JSON and
  Markdown are written to a temporary file and renamed.
- **AI runs in other processes** at low priority and is paused (SIGSTOP)
  while recording. Engine crashes become messages, never data loss.
- **Generated content must be traceable.** Notes cite transcript lines, and
  `engines/summarizer/Notes.cpp` removes what cannot be supported. Do not
  weaken those checks to make output look fuller.
- **Honest interface.** Never present uncertain information (speaker names,
  low-confidence text, unavailable downloads) as fact.

## Development

See the README for build dependencies. Useful options:

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DAFTERLINE_GGML_NATIVE=ON
# Faster iteration on the app alone (uses already-built engines via AFTERLINE_ENGINE_DIR):
cmake -S . -B build-app -G Ninja -DAFTERLINE_BUILD_ENGINES=OFF
AFTERLINE_ENGINE_DIR=build/engines AFTERLINE_DATA_DIR=/tmp/afterline-dev ./build-app/afterline
```

`AFTERLINE_DATA_DIR` keeps a development library away from your real one.

Run all tests before sending a change:

```sh
ctest --test-dir build --output-on-failure -LE pipewire
tests/run-pipewire-tests.sh ctest --test-dir build --output-on-failure -L pipewire
```

CI builds with AddressSanitizer and UndefinedBehaviorSanitizer, runs the
recording tests against PipeWire, transcribes a real speech sample offline
(network disabled), and builds on Fedora.

## Pull requests

- Keep each change focused, and add or update tests for behaviour you change.
  Audio changes need a test in `tests/test_pipewire.cpp`.
- Follow the existing style (C++17, Qt naming, 4-space indentation, 120 columns).
- User-facing text: plain, specific and calm. Say what happened and what to do.
- New dependencies, models or bundled assets must be added to
  `THIRD_PARTY_NOTICES.md` in the same pull request, with their licence.
- Update `CHANGELOG.md` under *Unreleased*.

## Releasing

1. Work through `packaging/RELEASE_CHECKLIST.md`, recording the results.
2. Bump the version in `CMakeLists.txt`, `packaging/arch/PKGBUILD` and the
   metainfo `<releases>`, and update `CHANGELOG.md` and
   `packaging/RELEASE_NOTES.md`.
3. Tag `vX.Y.Z` and push. The *Packages* workflow builds every package in a
   clean container of its distribution (all tests must pass), installs each one
   into a fresh container and runs it, then publishes the release with
   checksums. The website picks up the new installers automatically.

## Reporting bugs

Open an issue with your distribution, desktop, AfterLine version, what you
recorded from, and `~/.local/share/AfterLine/logs/engines.log` if processing
failed. Never attach recordings of other people without their consent.
