# Release checklist

Do not publish a stable release merely because GitHub Actions reports green.

- [ ] GitHub Actions: unit tests pass.
- [ ] GitHub Actions: Debian/Ubuntu, Fedora, Arch, and AppImage package jobs pass.
- [ ] Test installation and normal launch on a clean Ubuntu 26.04 desktop.
- [ ] Record 30 seconds of system audio, verify audible WAV output.
- [ ] Record a live application playback stream and verify no silent audio or unexpected drop.
- [ ] Test microphone on, microphone off, and switching sources while idle.
- [ ] Verify a 30+ minute call recording and that its WAV headers are valid on playback.
- [ ] Download the transcription model, disconnect networking, and transcribe offline.
- [ ] Generate a multi-hour summary locally with Ollama, checking action item attribution.
- [ ] Test no-mic scenario, audio device disconnect, and capture-stream termination.
- [ ] Confirm that model and bundled binary dependency licenses allow redistribution.
- [ ] Test GitHub Pages download selectors against actual published release assets.
- [ ] Check consent, accessibility, privacy language and screenshots.
- [ ] Sign off on Debian 12/13, Ubuntu 22.04/24.04/26.04, Fedora and Arch as individually verified.

Only then promote a build from development preview to stable.
