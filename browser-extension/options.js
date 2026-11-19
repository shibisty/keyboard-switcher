const api = globalThis.browser ?? globalThis.chrome;
const DEFAULT_PORT = 47863;
const $ = (id) => document.getElementById(id);

document.documentElement.dir = api.i18n.getMessage('@@bidi_dir') || 'ltr';
for (const el of document.querySelectorAll('[data-i18n]'))
  el.textContent = api.i18n.getMessage(el.dataset.i18n);

api.storage.local.get({ port: DEFAULT_PORT }).then(({ port }) => { $('port').value = port; });

$('save').addEventListener('click', async () => {
  const port = Number($('port').value) || DEFAULT_PORT;
  await api.storage.local.set({ port });
  $('status').textContent = api.i18n.getMessage('optionsSaved');
});

$('test').addEventListener('click', async () => {
  const port = Number($('port').value) || DEFAULT_PORT;
  try {
    const r = await fetch(`http://127.0.0.1:${port}/ping`);
    const ok = r.ok && (await r.text()) === 'keyboard-changer';
    $('status').textContent = api.i18n.getMessage(ok ? 'optionsConnected' : 'optionsNotApp');
  } catch {
    $('status').textContent = api.i18n.getMessage('optionsNotRunning');
  }
});
