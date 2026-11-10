"""PulseAudio/PipeWire capture: preserve independent channels as WAV."""
import json
import subprocess
import threading
import wave
from pathlib import Path

RATE = 16000

def pactl(kind):
    result = subprocess.run(["pactl", "-f", "json", "list", kind],
                            capture_output=True, text=True, check=True, timeout=8)
    return json.loads(result.stdout)

def sources():
    options = [("Entire computer (default output)", ("monitor", "@DEFAULT_MONITOR@"))]
    for item in pactl("sources"):
        name = item.get("name", "")
        if name.endswith(".monitor"):
            options.append(("Output: " + (item.get("description") or name), ("monitor", name)))
    for item in pactl("sink-inputs"):
        props = item.get("properties") or {}
        name = props.get("application.name") or props.get("media.name") or "Unknown application"
        options.append((f"App stream: {name} ({item['index']})", ("stream", str(item["index"]))))
    return options

def microphones():
    options = [("Default microphone", "@DEFAULT_SOURCE@")]
    for item in pactl("sources"):
        name = item.get("name", "")
        if not name.endswith(".monitor"):
            options.append((item.get("description") or name, name))
    return options

class Capture:
    def __init__(self, path, mode, value):
        self.path = Path(path)
        self.bytes_written = 0
        self.error = ""
        cmd = ["parec", "--raw", "--format=s16le", f"--rate={RATE}", "--channels=1",
               "--client-name=AfterLine"]
        if mode == "stream":
            cmd += [f"--monitor-stream={value}"]
        else:
            cmd += [f"--device={value}"]
        self.proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.DEVNULL, bufsize=0)
        self.thread = threading.Thread(target=self._write, daemon=True)
        self.thread.start()

    def _write(self):
        try:
            with wave.open(str(self.path), "wb") as output:
                output.setnchannels(1)
                output.setsampwidth(2)
                output.setframerate(RATE)
                while True:
                    block = self.proc.stdout.read(8192)
                    if not block:
                        break
                    output.writeframesraw(block)
                    self.bytes_written += len(block)
        except Exception as error:
            self.error = str(error)

    def stop(self):
        if self.proc.poll() is None:
            self.proc.terminate()
        try:
            self.proc.wait(timeout=4)
        except subprocess.TimeoutExpired:
            self.proc.kill()
            self.proc.wait()
        self.thread.join(timeout=5)
        if self.bytes_written == 0 and not self.error:
            self.error = "No audio was received. Check the selected source."
        return self.error
