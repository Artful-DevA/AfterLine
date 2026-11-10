# Third-party notices

AfterLine's own code is MIT licensed (see [LICENSE](LICENSE)). It is built on
the components below. This file is installed with every package
(`/usr/share/doc/afterline/`) and lists what is redistributed in AfterLine's
binaries, what is only linked from the system, and what is downloaded by the
user at run time.

## Compiled into AfterLine's engines (redistributed in every package)

| Component | Version | Licence | Use |
|---|---|---|---|
| [whisper.cpp](https://github.com/ggml-org/whisper.cpp) | v1.9.5 (`d1be6fd`) | MIT, © The ggml authors | Speech recognition engine (`afterline-transcriber`) |
| [llama.cpp](https://github.com/ggml-org/llama.cpp) | b12248 (`3837bf0`) | MIT, © The ggml authors | Notes and Q&A engine (`afterline-summarizer`) |
| [ggml](https://github.com/ggml-org/ggml) | bundled with each of the above | MIT, © The ggml authors | Tensor library and CPU backends |

The exact commits are pinned and verified at build time
(`cmake/EngineCommon.cmake`). Their full licence texts are in the source
trees fetched during the build and reproduced here:

> MIT License — Copyright (c) 2023-2026 The ggml authors. Permission is hereby
> granted, free of charge, to any person obtaining a copy of this software and
> associated documentation files (the "Software"), to deal in the Software
> without restriction, including without limitation the rights to use, copy,
> modify, merge, publish, distribute, sublicense, and/or sell copies of the
> Software, and to permit persons to whom the Software is furnished to do so,
> subject to the following conditions: The above copyright notice and this
> permission notice shall be included in all copies or substantial portions of
> the Software. THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND.

## Linked from the system (not redistributed in .deb, .rpm and Arch packages)

| Component | Licence | Notes |
|---|---|---|
| Qt 6 (Core, Gui, Widgets, Network, Svg) | LGPL-3.0 | Dynamically linked. The AppImage bundles Qt's unmodified shared libraries and plugins; they can be replaced, as the LGPL requires. Qt source: <https://download.qt.io/> |
| PipeWire client library (libpipewire-0.3) and SPA headers | MIT | Audio capture and playback |
| libopus | BSD-3-Clause | Recording format (Opus) |
| libogg | BSD-3-Clause | Recording container (Ogg) |
| SQLite | Public domain | Library index and search |
| C/C++ runtime (glibc, libstdc++, libgcc) | LGPL-2.1+ / GPL-3.0 with GCC Runtime Library Exception | System libraries |

The **AppImage** additionally bundles shared libraries from Ubuntu 22.04
needed by Qt (for example fontconfig, freetype, harfbuzz, libpng, ICU, glib,
libxkbcommon and the xcb libraries). Each keeps its own licence; their
copyright files are available from the Ubuntu 22.04 packages they come from.
No GPL-only library is bundled.

## Downloaded by the user at run time (not redistributed)

AfterLine never ships AI models. When the user asks, it downloads them from
Hugging Face and verifies them against the SHA-256 pinned in
`src/app/ModelCatalog.cpp`.

| Model | Licence | Source |
|---|---|---|
| Whisper base / small / large-v3-turbo (ggml conversions, quantised) | MIT (OpenAI Whisper weights) | huggingface.co/ggerganov/whisper.cpp |
| Silero VAD v6.2.0 (ggml conversion) | MIT (Silero Team) | huggingface.co/ggml-org/whisper-vad |
| Qwen3-4B-Instruct-2507, Qwen3-1.7B (GGUF, Q4_K_M) | Apache-2.0 (Alibaba Cloud Qwen team) | huggingface.co/unsloth |

## Artwork and fonts

- The AfterLine icon and all interface icons were drawn for this project (MIT).
- The desktop application uses the system's fonts.
- The website loads DM Sans and DM Serif Display from Google Fonts (SIL Open Font
  License 1.1).

## Review status

This inventory was reviewed for release 0.2.0. Anyone adding a dependency,
model or bundled asset must update this file in the same change (see
[CONTRIBUTING.md](CONTRIBUTING.md) and the release checklist).
