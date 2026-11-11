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
- Microphone on its own track, with an on-demand microphone test; the
  microphone is never opened just because the recording page is shown.
- Crash-tolerant Ogg Opus recording (about 15 MB per hour per track) with
  automatic recovery of interrupted meetings, live warnings for disconnected
  devices, low disk space and suspend, and pause/resume.
- Live transcript while recording (preliminary, shown beside the recording and
  kept in the meeting until the full transcript is ready); can be turned off.
- Built-in offline transcription (whisper.cpp) with speech detection,
  sentence-level timestamps, echo suppression, *You*/*Meeting* labels,
  uncertainty marking, corrections, and resumable processing of long meetings.
- Built-in local notes (llama.cpp, Qwen3): summary, key points, decisions,
  action items, open questions and dates, every item cited and checked against
  the transcript; automatic meeting titles.
- Ask AfterLine: a chat with cited answers (play buttons for each source)
  about one meeting or the whole library; questions about tasks are answered
  from the notes' action items.
- My tasks: action items assigned to you across all meetings, tickable,
  each linked to the moment it was agreed. Action items can also be ticked
  off in the notes.
- Folders with chosen icons and colours, drag and drop, and AI filing of new
  meetings (with undo) or suggested new folders; an Organize button files
  existing meetings.
- A choice of where recordings are saved, with a safe move of the library.
- Optional (off by default) suggestion to record when a call app starts
  using the microphone.
- AfterLine's own dialogs instead of the desktop's stock message boxes.
- Meeting library with accent-insensitive full-text search, tags and
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
