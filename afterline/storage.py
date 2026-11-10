"""Local data locations and safe recording metadata."""
import json
from datetime import datetime
from pathlib import Path

DATA_DIR = Path.home() / ".local" / "share" / "afterline"
MODEL_DIR = DATA_DIR / "models" / "whisper-small"

def create_session(source, microphone):
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    stamp = datetime.now().strftime("%Y-%m-%d_%H-%M-%S_%f")
    folder = DATA_DIR / f"recording-{stamp}"
    folder.mkdir()
    metadata = {"started": datetime.now().isoformat(), "source": source,
                "microphone": microphone, "status": "recording"}
    (folder / "session.json").write_text(json.dumps(metadata, indent=2), encoding="utf-8")
    return folder

def finish_session(folder, errors):
    path = Path(folder) / "session.json"
    info = json.loads(path.read_text(encoding="utf-8"))
    info.update({"finished": datetime.now().isoformat(), "status": "saved",
                 "capture_errors": errors})
    path.write_text(json.dumps(info, indent=2), encoding="utf-8")

def sessions():
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    return sorted((path for path in DATA_DIR.glob("recording-*")
                   if path.is_dir()), reverse=True)
