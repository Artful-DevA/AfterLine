// AfterLine website: mark each download as available or not, from the
// latest GitHub Release (via /api/releases). Download links work without
// this script; it only makes the page honest about what exists.
(() => {
  const status = document.getElementById('release-status');
  const links = document.querySelectorAll('[data-target]');
  const params = new URLSearchParams(location.search);
  const missing = params.get('unavailable');

  const markAll = (targets, available) => {
    for (const link of links) {
      const ok = available && targets && targets[link.dataset.target];
      if (ok) {
        link.classList.remove('unavailable');
        link.removeAttribute('aria-disabled');
        const mb = Math.round(ok.size / 1e6);
        if (mb > 0) link.title = `${ok.name} (${mb} MB)`;
      } else if (link.classList.contains('dl')) {
        link.classList.add('unavailable');
        link.setAttribute('aria-disabled', 'true');
        link.title = 'No installer for this system has been published yet';
        link.addEventListener('click', (e) => e.preventDefault());
      }
    }
  };

  fetch('/api/releases', { headers: { Accept: 'application/json' } })
    .then((r) => (r.ok ? r.json() : Promise.reject(new Error(String(r.status)))))
    .then((data) => {
      if (!data.available) {
        status.textContent = 'No release has been published yet. Installers appear here as soon as one passes its tests.';
        markAll({}, false);
        return;
      }
      const date = data.published ? new Date(data.published).toLocaleDateString() : '';
      status.textContent = `Latest release: ${data.version}${date ? ' · ' + date : ''}`;
      markAll(data.targets, true);
      if (missing) {
        status.textContent += ' · There is no installer for that system in this release yet.';
      }
    })
    .catch(() => {
      // Offline or API unavailable: the links still try the direct download.
      status.textContent = 'Could not check the latest release right now. The download links still work.';
      if (missing) status.textContent = 'That installer is not available at the moment.';
    });
})();
