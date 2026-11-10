# Security and privacy

## Reporting a vulnerability

Please report security problems privately through GitHub's
[security advisory form](https://github.com/Artful-DevA/AfterLine/security/advisories/new)
rather than a public issue. Include the version, your distribution and steps to
reproduce. You will get an answer within a week, and fixes are released as soon
as they are ready.

## What AfterLine does with your data

- **Recording** uses PipeWire on your computer. With *Include my microphone*
  off, the microphone is never opened. With a single application selected,
  only that application's audio streams are linked into the recording.
- **Storage.** Meetings are plain files in `~/.local/share/AfterLine/meetings/`,
  readable only by your user account (standard home-directory permissions).
  They are **not encrypted by AfterLine**; use full-disk or home-directory
  encryption on shared or portable computers.
- **Processing.** Transcription and notes run in local helper processes
  (`afterline-transcriber`, `afterline-summarizer`) that read the meeting's
  files and write results next to them. They make no network connections.
- **Network.** AfterLine contacts the network only to download models from
  `huggingface.co`, and only when you click *Download*. The request contains the
  file name and a generic `AfterLine/<version>` user agent, nothing about you or
  your meetings. Each file is verified against a SHA-256 pinned in the source
  code before it is used. There is no telemetry, no update check and no crash
  reporting.
- **Deletion** moves the meeting folder (audio, transcript, notes) to your
  desktop trash. Empty the trash to remove it for good.

## Verifying offline operation

After the models are installed:

1. Disconnect from the network (or run `unshare -rn afterline` on a system
   that allows unprivileged user namespaces).
2. Record, transcribe, write notes, search and ask questions. Everything works.

CI checks this for transcription by running the engine with networking
disabled.

## Supply chain

- whisper.cpp and llama.cpp are fetched at pinned commits, and the build fails
  if the checked-out commit differs (`cmake/EngineCommon.cmake`).
- Release packages are built in clean containers by GitHub Actions from the
  tagged source, and published with a `SHA256SUMS` file.
- Model files are pinned by SHA-256 (`src/app/ModelCatalog.cpp`); an imported
  file is accepted only if its hash matches a known model.

## Recording law

You are responsible for telling people they are recorded and getting their
consent where the law requires it. AfterLine shows a reminder on the record
screen and an unmistakable indicator while recording.
