# Release checklist

A release is published only when every item below is checked and the result
is recorded here. "Packaging commands completed" is not a pass: packages must
install, run and do their job.

## Automated gates (GitHub Actions)

These must be green for the tagged commit:

- [ ] **CI**: Ubuntu 24.04 build with AddressSanitizer and UBSan; unit, storage
      and model-catalog tests; recording tests against a real PipeWire session;
      the app starts.
- [ ] **CI**: offline transcription of real speech (JFK sample) with networking
      disabled; the expected sentence and timestamp are checked.
- [ ] **CI**: Fedora build and tests.
- [ ] **Packages**: every package builds in a clean container of its
      distribution, with all tests (including recording) as a gate:
      Ubuntu 22.04 / 24.04 / 26.04, Debian 12 / 13, Fedora 43 / 44, Arch,
      AppImage.
- [ ] **Packages**: every package installs into a *fresh* container, `afterline`
      starts, both engines start, desktop entry, icon and metainfo are in
      place, and the package uninstalls cleanly.
- [ ] Tag matches the version in `CMakeLists.txt`; `SHA256SUMS` published.

## Manual verification on real desktops

Record on real hardware (not only virtual devices) before calling a release
stable. For each, note distribution, desktop, hardware and result.

| Check | Ubuntu 26.04 GNOME | Ubuntu 24.04 | Mint 22 Cinnamon | Fedora KDE | Arch |
|---|---|---|---|---|---|
| Install from the website download button, launch from the menu | | | | | |
| First-run setup downloads and verifies the speech model | | | | | |
| Record *Entire computer* during a real call | | | | | |
| Record only Firefox/Chrome while music plays in another app | | | | | |
| Record Zoom / Teams / Discord desktop app | | | | | |
| Built-in and USB microphone; unplug and replug during recording | | | | | |
| Bluetooth headset (mic + output) | | | | | |
| Suspend and resume during a recording | | | | | |
| `kill -9` during a recording, then relaunch: meeting recovered | | | | | |
| 2-hour meeting: recording, transcription, notes complete | | | | | |
| English and Romanian meetings with real speakers | | | | | |
| Offline: Wi-Fi off, transcription + notes + search work | | | | | |
| Dark mode, keyboard navigation, screen reader announces controls | | | | | |
| Tray: closing the window keeps recording; stop from tray | | | | | |

## Licensing and security

- [ ] `THIRD_PARTY_NOTICES.md` matches the dependencies, pinned engine
      commits, models and bundled assets of this release.
- [ ] Model SHA-256 values in `src/app/ModelCatalog.cpp` re-checked against
      Hugging Face.
- [ ] No network access besides model downloads (review new code for
      `QNetwork*`, sockets, `curl`).
- [ ] Known limitations in the README are current.

## Results for 0.2.0 (2026-11-10)

Verified during development of 0.2.0:

| What | Where | Result |
|---|---|---|
| Recording format: round trip, exact length, seeking, truncated-file recovery, soft clipping (ASan/UBSan) | Ubuntu 24.04 | 9/9 pass |
| Recording against real PipeWire: discovery, whole computer, single app isolated from another app (other app's energy ≈ 1e-10 of the meeting's), app stream restart, mic on its own track, default mic, mic unplug/replug, pause, destroy while recording, missing device (ASan/UBSan) | PipeWire 1.0.5, WirePlumber | 12/12 pass |
| Same suite | Ubuntu 22.04 container, PipeWire 0.3.48, pipewire-media-session, Qt 6.2 | 11 pass, 1 skipped: that PipeWire's virtual null sink delivers silent monitors (stock `pw-record` too), so whole-computer capture there is **not yet verified on real hardware** |
| Storage, search (incl. Romanian without diacritics), corrupt index rebuild, crash recovery, notes validation, exports, model catalog | Ubuntu 24.04, Ubuntu 22.04 | all pass |
| Ubuntu 26.04 `.deb` built in a clean container with all tests, installed into a fresh 26.04 container, app and engines start, installed engine transcribes real speech correctly with runtime CPU-variant selection, clean removal | Docker, ubuntu:26.04 | pass |
| End to end through the GUI: first-run setup with a real verified download; Zoom selected while “Spotify” plays; mic with echo; stop; automatic transcription and notes; Ask with citations; playback; dark mode | Ubuntu 24.04, Xvfb, PipeWire 1.0.5 | pass (screenshots in `website/screenshots/`) |
| `kill -9` during recording, relaunch recovers 24 s of a 25 s recording and explains it | same | pass |
| Website download routing: each target maps to its exact asset; missing installers show “not available”, never another distribution's file; GitHub outage handled | Node with mocked GitHub API | 12/12 pass |

Not yet verified for 0.2.0 (required before announcing a stable release):
the manual table above on physical hardware, Debian/Fedora/Arch packages on
real desktops, very long (2 h+) meetings end to end, and AppImage on a
non-Ubuntu desktop.
