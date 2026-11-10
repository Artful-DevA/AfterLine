import json
from pathlib import Path
from afterline import storage

def test_session_lifecycle(tmp_path, monkeypatch):
    monkeypatch.setattr(storage, "DATA_DIR", tmp_path)
    folder = storage.create_session("Test output", False)
    assert folder in storage.sessions()
    assert json.loads((folder / "session.json").read_text())["status"] == "recording"
    storage.finish_session(folder, [])
    assert json.loads((folder / "session.json").read_text())["status"] == "saved"
