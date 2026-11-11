// AfterLine website: reads the latest GitHub Release (via /api/releases),
// marks each download as available or not, lists every file in the release,
// and points out the right download for the visitor's distribution when the
// browser reveals it. Download links work without this script.
(() => {
  const $ = (id) => document.getElementById(id);
  const status = $('release-status');
  const links = document.querySelectorAll('[data-target]');
  const missing = new URLSearchParams(location.search).get('unavailable');

  const formatSize = (bytes) => {
    if (!bytes) return '';
    const units = ['B', 'KB', 'MB', 'GB'];
    let v = bytes, i = 0;
    while (v >= 1000 && i < units.length - 1) { v /= 1000; i++; }
    return `${v.toFixed(i > 1 ? 1 : 0)} ${units[i]}`;
  };
  const tagOf = (type) => ({ deb: 'DEB', rpm: 'RPM', arch: 'ARCH', appimage: 'APP', checksums: 'SUM' }[type] || 'FILE');

  // Which distribution family the browser runs on, when it says so.
  // (Browsers do not report the release number.)
  const detect = () => {
    const ua = navigator.userAgent.toLowerCase();
    if (!ua.includes('linux') || ua.includes('android')) return null;
    for (const [needle, family, name] of [
      ['ubuntu', 'ubuntu', 'Ubuntu'], ['mint', 'mint', 'Linux Mint'], ['fedora', 'fedora', 'Fedora'],
      ['debian', 'debian', 'Debian'], ['manjaro', 'arch', 'Manjaro'], ['arch', 'arch', 'Arch Linux'],
    ]) if (ua.includes(needle)) return { family, name };
    return { family: null, name: 'Linux' };
  };

  const showDetected = () => {
    const d = detect();
    const box = $('detected');
    if (!d) {
      box.textContent = 'AfterLine is a Linux application. Open this page on your Linux computer to download it.';
      box.hidden = false;
      return;
    }
    if (d.family) {
      const card = document.querySelector(`.distro[data-family="${d.family}"]`);
      if (card) card.classList.add('suggested');
      box.textContent = `${d.name} detected. Choose your version below; it is shown in Settings → About.`;
    } else {
      box.textContent = 'Linux detected. Choose the build that matches your distribution, or the AppImage if it is not listed.';
    }
    box.hidden = false;
  };

  const markAll = (targets, available) => {
    for (const link of links) {
      const ok = available && targets && targets[link.dataset.target];
      if (ok) {
        link.classList.remove('unavailable');
        link.removeAttribute('aria-disabled');
        link.title = `${ok.name} (${formatSize(ok.size)})`;
      } else if (link.classList.contains('dl')) {
        link.classList.add('unavailable');
        link.setAttribute('aria-disabled', 'true');
        link.title = 'No installer for this system has been published yet';
        link.addEventListener('click', (e) => e.preventDefault());
      }
    }
  };

  const listAll = (assets) => {
    const list = $('package-list');
    list.replaceChildren();
    for (const a of assets) {
      const row = document.createElement('a');
      row.className = 'package-row';
      row.href = a.target ? `/download/${a.target}` : '#';
      const tag = document.createElement('span');
      tag.className = 'pkg-tag';
      tag.textContent = tagOf(a.type);
      const what = document.createElement('span');
      const label = document.createElement('strong');
      label.textContent = a.label;
      const small = document.createElement('small');
      small.textContent = `${a.name} · ${formatSize(a.size)}`;
      what.append(label, small);
      const go = document.createElement('b');
      go.textContent = 'Download ↓';
      row.append(tag, what, go);
      list.append(row);
    }
    $('all-packages').hidden = assets.length === 0;
  };

  showDetected();
  fetch('/api/releases', { headers: { Accept: 'application/json' } })
    .then((r) => (r.ok ? r.json() : Promise.reject(new Error(String(r.status)))))
    .then((data) => {
      if (!data.available) {
        status.textContent = 'Release downloads are being published. This page is connected to GitHub Releases, and ' +
          'installers appear here automatically once a release has passed its tests.';
        status.classList.add('publishing');
        $('hero-download').textContent = 'Release publishing soon';
        markAll({}, false);
        return;
      }
      const date = data.published ? new Date(data.published).toLocaleDateString() : '';
      status.textContent = `Downloads come straight from GitHub Releases${date ? ', published ' + date : ''}.`;
      const v = $('version');
      v.innerHTML = '';
      v.append('Latest release ', Object.assign(document.createElement('strong'), { textContent: data.version }));
      v.hidden = false;
      markAll(data.targets, true);
      listAll(data.assets || []);
      if (missing) status.textContent += ' There is no installer for that system in this release yet.';
    })
    .catch(() => {
      status.textContent = 'Could not check the latest release right now. The download links still work.';
      if (missing) status.textContent = 'That installer is not available at the moment.';
    });
})();
