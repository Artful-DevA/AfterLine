// SPDX-License-Identifier: MIT
// /download/<target>: redirects straight to the matching installer of the
// latest release (e.g. the Ubuntu 26.04 .deb). When no such installer has been
// published, the visitor is sent back to the download section, which says so
// plainly instead of pretending a file exists.
import { TARGETS, assetFor, latestRelease } from './releases-lib.mjs';

export default async (request) => {
  const url = new URL(request.url);
  const target = url.searchParams.get('target') || url.pathname.split('/').filter(Boolean).pop();
  const site = new URL('/', url);
  if (!TARGETS[target]) {
    return Response.redirect(new URL('/#download', site), 302);
  }
  try {
    const asset = assetFor(await latestRelease(), target);
    if (asset) {
      return new Response(null, {
        status: 302,
        headers: { Location: asset.browser_download_url, 'Cache-Control': 'public, max-age=60' },
      });
    }
  } catch {
    // Fall through to the honest "not available" state.
  }
  return Response.redirect(new URL(`/?unavailable=${encodeURIComponent(target)}#download`, site), 302);
};

export const config = { path: '/download/:target' };
