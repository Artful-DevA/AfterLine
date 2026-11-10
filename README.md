# AfterLine

**Conversations, kept.**

AfterLine is an offline-first Linux meeting recorder with multilingual transcription and optional local AI notes. Record an entire system output, a selected output, or a currently active application's audio stream. Include or exclude your microphone. Audio and transcripts remain on your computer.

> **Pre-release, not yet verified on real Ubuntu 26.04 meeting audio.** Do not trust it as the only recorder for an important meeting until you've tested it. Application-stream capture depends on PulseAudio-compatible monitor-stream support and can fail on individual systems. No claim of zero bugs.

## Ubuntu 26.04 (source run)

Install prerequisites:

```sh
sudo apt update
sudo apt install python3 python3-venv pulseaudio-utils
./run.sh
```

This will download Python packages once, into a local virtual environment. On your first transcription, AfterLine downloads the multilingual faster-whisper Small model. Once models are present, transcription runs without internet.

To generate AI notes, separately install [Ollama](https://ollama.com/) and download `qwen3:4b` with `ollama pull qwen3:4b`. This is a local model, not a cloud API.

## Try it safely

1. Start a meeting or a video so its audio stream is visible.
2. Click **Sources** to refresh the list. Pick **Entire computer** or the active application's audio stream.
3. Enable **Include microphone** if needed.
4. Record 30 seconds and stop. Open the WAV files under `~/.local/share/afterline/` and listen for actual sound.
5. Click **Transcribe recording**, then inspect timestamps and text.
6. Once Ollama is configured, try **Transcript + AI notes**.

**Per-application capture limitation:** The audio dropdown shows individual PulseAudio playback streams, not a persistent app-wide capture policy. A stream ending or reconnecting may interrupt capture. For critical meetings, **Entire computer** is safer, and use a backup recorder until verified.

## Files and privacy

Session files are kept in `~/.local/share/afterline/recording-...`, including `computer.wav`, optional `microphone.wav`, `session.json`, `transcript.txt`, `transcript.json`, and optional `notes.md`. After model downloads, AfterLine's transcription is local. Optional Ollama is contacted only at localhost. The website does not receive recordings.

Each audio source is recorded separately. Transcript labels distinguish **Computer** and **Microphone** inputs, not individual people. Source clocks can differ slightly. Do not record people without the permissions legally required where you are.

## Requirements

Target: Ubuntu 26.04 with PipeWire-Pulse or PulseAudio and 16 GB RAM; an ordinary CPU can transcribe with quantized Whisper Small, but transcription may be slower than real time depending on CPU and recording length. Linux Mint, Debian, Fedora, Arch and derivatives are packaging targets, not yet fully validated.

## Build and release

`pytest` runs unit/structural tests. The GitHub Actions CI also checks Python compilation and static website files. Release workflows build packages and publish them on version tags; package-build success and installation tests must be checked in Actions, and actual GUI/audio behavior must be tested on desktop Linux before announcing a stable release.

The public [website](https://artful-deva.github.io/AfterLine/) is built from `website/` through GitHub Pages. To activate Pages: Settings → Pages → Build and deployment → GitHub Actions.

## Limitations and release gates

- First model acquisition requires internet. There is no automatic offline installation bundle.
- Recording is not automatically transcribed live; processing begins after recording.
- No pause/resume, VU meter, diarization, automatic device failover, audio-file import, speaker ID, or automatic update mechanism yet.
- Ollama must be installed and started separately to summarize.
- PyInstaller builds and Linux package workflows require verification across distributions.
- Test coverage includes unit checks, not a real system-audio or end-to-end Linux GUI test.
- Long sessions generate sizable uncompressed WAV files.

## License and security

Project source is MIT licensed in [LICENSE](LICENSE). Third-party packages and AI models have their **own licenses**. Before a public binary release, publish a third-party dependency and model-license inventory and verify binary distribution terms. See [SECURITY.md](SECURITY.md) and [CONTRIBUTING.md](CONTRIBUTING.md).
