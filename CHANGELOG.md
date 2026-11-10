# Changelog

All notable changes to AfterLine are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project uses
[semantic versioning](https://semver.org/).

## [Unreleased]

## [0.2.0] - 2026-11-10

A complete rewrite. The Python prototype has been replaced with a native
C++/Qt 6 application.

### Added
- Native PipeWire recording of the entire computer, one output device, or one
  application. Application capture follows the application across stream
  restarts and never includes other applications.
- Microphone on its own track, with a level check before recording.
- Crash-tolerant Ogg Opus recording (about 15 MB per hour per track) with
  automatic recovery of interrupted meetings, live warnings for disconnected
  devices, low disk space and suspend, and pause/resume.
- Built-in offline transcription (whisper.cpp) with speech detection,
  sentence-level timestamps, echo suppression, *You*/*Meeting* labels,
  uncertainty marking, corrections, and resumable processing of long meetings.
- Built-in local notes (llama.cpp, Qwen3): summary, key points, decisions,
  action items, open questions and dates, every item cited and checked against
  the transcript; automatic meeting titles.
- Ask AfterLine: cited answers about one meeting or the whole library.
- Meeting library with accent-insensitive full-text search, folders, tags and
  safe deletion; Notes, Transcript and Audio views with a player and waveform.
- Export to Markdown, text, SRT, WebVTT, JSON and audio.
- First-run setup and model manager with resumable, verified downloads and
  offline import.
- Light and designed dark themes, tray icon, notifications, keyboard shortcuts.
- Packages for Ubuntu 22.04/24.04/26.04, Debian 12/13, Fedora 43/44, Arch and an
  AppImage, built and install-tested in clean containers; a website with
  direct per-distribution downloads.
- Tests against a real PipeWire session, codec crash tests, storage, notes
  validation, and offline transcription of real speech in CI.

### Removed
- The Python/PyInstaller prototype, `parec`-based capture and the Ollama
  dependency.

## [0.1.0] - 2026-10

- Initial prototype (Python, PySide6).
