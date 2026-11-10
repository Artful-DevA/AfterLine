# Contributing to AfterLine

Bug reports should include Linux distribution, desktop session (Wayland/X11), audio server, source selection, reproduction steps, and sanitized console logs. Never upload meeting recordings, transcripts, personal data, or API credentials without consent.

Set up with `python3 -m venv .venv && .venv/bin/pip install -e . pytest`. Run `.venv/bin/python -m pytest -q` before proposing changes.

A change affecting audio capture should be manually checked on a PipeWire desktop and documented before a release. Keep transcription offline and never add telemetry or remote upload without explicit user opt-in.
