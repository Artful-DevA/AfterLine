const REPO = 'Artful-DevA/AfterLine';
const root = 'https://github.com/' + REPO;
const status = document.querySelector('#release-status');
const dialog = document.querySelector('#choose-download');
const options = document.querySelector('#options');
document.querySelector('#close-dialog').addEventListener('click', () => dialog.close());
const types = {
  deb: /\.deb$/i,
  rpm: /\.rpm$/i,
  arch: /\.pkg\.tar\.zst$/i,
  appimage: /\.AppImage$/i
};
for (const link of document.querySelectorAll('[data-format]')) {
  link.href = root + '/releases';
}
fetch('https://api.github.com/repos/' + REPO + '/releases/latest', {
  headers: {Accept: 'application/vnd.github+json'}
}).then(async response => {
  if (!response.ok) throw new Error('No published release');
  return response.json();
}).then(release => {
  const assets = release.assets || [];
  status.textContent = 'Latest available release: ' + release.tag_name + ' · Hosted on GitHub';
  for (const link of document.querySelectorAll('[data-format]')) {
    const matches = assets.filter(asset => types[link.dataset.format].test(asset.name));
    if (matches.length === 1) {
      link.href = matches[0].browser_download_url;
    } else if (matches.length > 1) {
      link.addEventListener('click', event => {
        event.preventDefault();
        options.replaceChildren();
        for (const asset of matches) {
          const choice = document.createElement('a');
          choice.className = 'option';
          choice.textContent = asset.name + ' ↗';
          choice.href = asset.browser_download_url;
          options.appendChild(choice);
        }
        dialog.showModal();
      });
    }
  }
}).catch(() => {
  status.textContent = 'No public binaries released yet. Packages will appear after successful build and release checks.';
});
