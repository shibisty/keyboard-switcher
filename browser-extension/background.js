// Keyboard Switcher browser extension.
// Sends ONLY the domain of the active tab ("github.com") to the Keyboard Switcher app
// running on this computer (http://127.0.0.1:<port>/tab). Nothing leaves the machine.
const api = globalThis.browser ?? globalThis.chrome;
const DEFAULT_PORT = 47863;

async function port() {
  const { port } = await api.storage.local.get({ port: DEFAULT_PORT });
  return Number(port) || DEFAULT_PORT;
}

function domainOf(url) {
  try {
    const u = new URL(url);
    if (u.protocol !== 'http:' && u.protocol !== 'https:') return '';
    return u.hostname.toLowerCase().replace(/^www\./, '');
  } catch {
    return '';
  }
}

async function send(tab) {
  if (!tab) return;
  const domain = domainOf(tab.url || tab.pendingUrl || '');
  try {
    // text/plain keeps it a "simple" request (no CORS preflight).
    await fetch(`http://127.0.0.1:${await port()}/tab`, {
      method: 'POST',
      headers: { 'Content-Type': 'text/plain' },
      body: JSON.stringify({ domain }),
    });
  } catch {
    // app not running
  }
}

async function sendActive(windowId) {
  const query = windowId === undefined ? { active: true, lastFocusedWindow: true } : { active: true, windowId };
  const [tab] = await api.tabs.query(query);
  await send(tab);
}

api.tabs.onActivated.addListener(({ windowId }) => sendActive(windowId));
api.tabs.onUpdated.addListener((tabId, change, tab) => {
  if (change.url && tab.active) sendActive(tab.windowId);
});
api.windows.onFocusChanged.addListener((windowId) => {
  if (windowId !== api.windows.WINDOW_ID_NONE) sendActive(windowId);
});
