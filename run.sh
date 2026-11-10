#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")"
command -v pactl >/dev/null && command -v parec >/dev/null || { echo "Install pulseaudio-utils first" >&2; exit 1; }
if [ ! -d .venv ]; then
  python3 -m venv .venv
  .venv/bin/python -m pip install -r requirements.txt
fi
exec .venv/bin/python main.py
