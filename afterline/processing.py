"""Local CPU transcription and optional Ollama summarization."""
import json
import os
from pathlib import Path
from urllib.request import Request, urlopen

from .storage import MODEL_DIR

def format_transcript(segments):
    return "\n".join(
        f"[{int(s['start']//3600):02d}:{int(s['start']%3600//60):02d}:{int(s['start']%60):02d}] "
        f"{s['source']}: {s['text']}" for s in sorted(segments, key=lambda s: s["start"]))

def transcribe(folder, progress=lambda message: None):
    from faster_whisper import WhisperModel
    from huggingface_hub import snapshot_download

    folder = Path(folder)
    if not (MODEL_DIR / "model.bin").exists():
        progress("Downloading multilingual transcription model (one-time setup)")
        snapshot_download(repo_id="Systran/faster-whisper-small",
                          local_dir=str(MODEL_DIR))
    progress("Loading offline transcription model")
    model = WhisperModel(str(MODEL_DIR), device="cpu", compute_type="int8",
                         cpu_threads=max(2, min(8, os.cpu_count() or 4)))
    segments = []
    for filename, source in [("computer.wav", "Computer"), ("microphone.wav", "Microphone")]:
        audio = folder / filename
        if not audio.exists() or audio.stat().st_size < 1000:
            continue
        progress("Transcribing " + source.lower() + " audio")
        items, _ = model.transcribe(str(audio), beam_size=3, vad_filter=True)
        for item in items:
            if item.text.strip():
                segments.append({"start": round(item.start, 2), "end": round(item.end, 2),
                                 "source": source, "text": item.text.strip()})
    segments.sort(key=lambda s: (s["start"], s["source"]))
    (folder / "transcript.json").write_text(json.dumps(segments, ensure_ascii=False, indent=2),
                                           encoding="utf-8")
    text = format_transcript(segments)
    (folder / "transcript.txt").write_text(text, encoding="utf-8")
    if not text.strip():
        raise RuntimeError("Recording saved, but no speech could be recognized.")
    return text

def make_notes(folder, transcript, progress=lambda message: None):
    """Summarize consecutive transcript chunks, then synthesize. Local Ollama only."""
    folder = Path(folder)
    if not transcript.strip():
        raise ValueError("Transcript is empty")
    progress("Generating notes with local Ollama")
    def generate(prompt):
        request = Request(
            "http://127.0.0.1:11434/api/generate",
            data=json.dumps({"model": "qwen3:4b", "prompt": prompt, "stream": False,
                             "think": False, "options": {"num_ctx": 8192}}).encode("utf-8"),
            headers={"Content-Type": "application/json"}, method="POST")
        with urlopen(request, timeout=900) as response:
            result = json.load(response).get("response", "").strip()
        if not result:
            raise RuntimeError("Local summary model returned an empty response.")
        return result
    chunks = []
    lines = transcript.splitlines()
    current = []
    length = 0
    for line in lines:
        if length + len(line) > 12000 and current:
            chunks.append("\n".join(current))
            current, length = [], 0
        current.append(line)
        length += len(line) + 1
    if current:
        chunks.append("\n".join(current))
    partials = []
    for index, chunk in enumerate(chunks, 1):
        progress(f"Analyzing meeting section {index}/{len(chunks)}")
        partials.append(generate(
            "Extract only substantiated facts, decisions, action items and timestamps. "
            "Do not invent information. This is a meeting transcript excerpt:\n" + chunk))
    progress("Compiling meeting notes")
    final = generate(
        "Create precise Markdown notes with sections: Summary, Key Points, Decisions, "
        "Action Items. Preserve timestamps and distinguish uncertain information. "
        "If none, write 'Not specified'. Base everything solely on these extracts:\n"
        + "\n\n".join(partials))
    (folder / "notes.md").write_text(final, encoding="utf-8")
    return final
