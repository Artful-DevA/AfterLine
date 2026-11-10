AfterLine records your meetings on Linux and writes a full transcript and clear notes, privately, on your own
computer.

**Which file do I download?**

| Your system | File |
|---|---|
| Ubuntu 26.04 / 24.04 / 22.04, Pop!_OS, Zorin OS | `afterline_*_ubuntu-<release>_amd64.deb` |
| Linux Mint 22 / 21 | the Ubuntu 24.04 / 22.04 `.deb` |
| Debian 13 / 12 | `afterline_*_debian-<release>_amd64.deb` |
| Fedora 44 / 43 | `afterline-*-fedora-<release>.x86_64.rpm` |
| Arch Linux, EndeavourOS, Manjaro | `afterline-*-x86_64.pkg.tar.zst` |
| Other distributions | `AfterLine-*-x86_64.AppImage` |

Open the file with your software installer. On first launch AfterLine offers to download its speech model (190 MB)
and, optionally, its notes model (2.5 GB); after that it works offline.

Requires PipeWire as the sound server (default everywhere except Ubuntu 22.04 and Linux Mint 21, where AfterLine
explains how to switch). Verify downloads with `sha256sum -c SHA256SUMS --ignore-missing`.

What changed: see [CHANGELOG.md](https://github.com/Artful-DevA/AfterLine/blob/main/CHANGELOG.md). Known
limitations: see the README.
