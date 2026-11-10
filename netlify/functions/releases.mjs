// SPDX-License-Identifier: MIT
// /api/releases: which installers the latest release actually contains, so
// the page can mark each download button available or not.
import { TARGETS, RELEASES_URL, assetFor, latestRelease } from './releases-lib.mjs';

export default async () => {
  const headers = { 'Content-Type': 'application/json', 'Cache-Control': 'public, s-maxage=120, max-age=60' };
  try {
    const release = await latestRelease();
    const targets = {};
    for (const key of Object.keys(TARGETS)) {
      const asset = assetFor(release, key);
      targets[key] = asset ? { name: asset.name, size: asset.size } : null;
    }
    return new Response(JSON.stringify({
      available: Boolean(release),
      version: release ? release.tag_name : null,
      published: release ? release.published_at : null,
      releaseUrl: release ? release.html_url : RELEASES_URL,
      targets,
    }), { headers });
  } catch {
    return new Response(JSON.stringify({ available: false, error: 'lookup_failed', releaseUrl: RELEASES_URL, targets: {} }),
      { headers: { ...headers, 'Cache-Control': 'public, max-age=30' } });
  }
};

export const config = { path: '/api/releases' };
