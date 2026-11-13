# Flatpak and Flathub

`io.github.Artful_DevA.AfterLine.yml` builds AfterLine as a Flatpak. The
**Flatpak** GitHub workflow builds it on every change and uploads
`AfterLine-x86_64.flatpak`, which installs with:

    flatpak install --user AfterLine-x86_64.flatpak

## Submitting to Flathub (once)

Flathub reviews each new app by hand. The submission is a pull request from
the GitHub account that owns the app ID (`io.github.Artful_DevA.*` must come
from Artful-DevA).

1. Fork https://github.com/flathub/flathub and create a branch from `new-pr`.
2. Add a copy of the manifest at the top level of the fork, with the first
   source (`type: dir`) replaced by the release:

       - type: git
         url: https://github.com/Artful-DevA/AfterLine.git
         tag: v0.2.3
         commit: <the full commit hash of that tag>

3. Open a pull request against the `new-pr` branch titled
   "Add io.github.Artful_DevA.AfterLine" and answer the reviewers.
4. Once merged, Flathub creates `flathub/io.github.Artful_DevA.AfterLine`;
   each new release is a small pull request there bumping the tag and commit.

Inside Flatpak, AfterLine does not check for new versions itself (Flathub
updates it), and suggesting a recording when a call starts is not available:
the sandbox cannot see other running programs.
