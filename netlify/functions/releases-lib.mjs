// SPDX-License-Identifier: MIT
// Maps download targets to GitHub Release assets. Shared by the download
// redirect and the release-info endpoint.
export const REPO = 'Artful-DevA/AfterLine';
export const RELEASES_URL = `https://github.com/${REPO}/releases`;

// Every target the website offers, and how its asset is named by
// packaging/build-package.sh, the PKGBUILD and build-appimage.sh.
export const TARGETS = {
  'ubuntu-26.04': { label: 'Ubuntu 26.04', match: (n) => /_ubuntu-26\.04_amd64\.deb$/.test(n) },
  'ubuntu-24.04': { label: 'Ubuntu 24.04', match: (n) => /_ubuntu-24\.04_amd64\.deb$/.test(n) },
  'ubuntu-22.04': { label: 'Ubuntu 22.04', match: (n) => /_ubuntu-22\.04_amd64\.deb$/.test(n) },
  'debian-13': { label: 'Debian 13', match: (n) => /_debian-13_amd64\.deb$/.test(n) },
  'debian-12': { label: 'Debian 12', match: (n) => /_debian-12_amd64\.deb$/.test(n) },
  'fedora-44': { label: 'Fedora 44', match: (n) => /-fedora-44\.x86_64\.rpm$/.test(n) },
  'fedora-43': { label: 'Fedora 43', match: (n) => /-fedora-43\.x86_64\.rpm$/.test(n) },
  'arch': { label: 'Arch Linux', match: (n) => /^afterline-\d[^/]*-x86_64\.pkg\.tar\.zst$/.test(n) },
  'appimage': { label: 'AppImage', match: (n) => /-x86_64\.AppImage$/.test(n) },
  'checksums': { label: 'SHA256SUMS', match: (n) => n === 'SHA256SUMS' },
};

export async function latestRelease() {
  const headers = { Accept: 'application/vnd.github+json', 'User-Agent': 'AfterLine-Website' };
  if (process.env.GITHUB_TOKEN) headers.Authorization = `Bearer ${process.env.GITHUB_TOKEN}`;
  const res = await fetch(`https://api.github.com/repos/${REPO}/releases/latest`, { headers });
  if (res.status === 404) return null;  // no published release yet
  if (!res.ok) throw new Error(`GitHub returned ${res.status}`);
  return res.json();
}

export function assetFor(release, target) {
  const spec = TARGETS[target];
  if (!release || !spec) return null;
  return (release.assets || []).find((a) => spec.match(a.name)) || null;
}
