import http from "node:http";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const root = path.dirname(fileURLToPath(import.meta.url));
const loader = path.join(root, "loader.js");
const wasm = path.join(root, "chrome-guest.wasm");
const portfolio = path.join(root, "mojovm-portfolio.txt");
const offscreenScript = path.join(root, "offscreen_script.js");
const port = 8807;
const iwa = { id: "bruja-debugger", url: "/iwa/", hits: [] };

function note(kind, detail) {
  iwa.hits.push({ at: new Date().toISOString(), kind, detail: String(detail || "") });
  if (iwa.hits.length > 200) iwa.hits.shift();
}

const iwaPage = `<!doctype html><html><head><meta charset="utf-8">
<link rel="manifest" href="/iwa/manifest.webmanifest">
<title>Bruja IWA</title></head><body data-frame="iwa" hidden>
<img src="/pix.svg?thumbprint=bruja-debugger" width="1" height="1" alt="" hidden>
<script src="/offscreen_script.js"></script>
<script>
function ownTraffic(detail) {
  var s = String(detail || "");
  return s.indexOf("127.0.0.1:8807") >= 0 || s.indexOf("/harness.wasm") >= 0 || s.indexOf("/loader/") >= 0 || s.indexOf("/pix.svg") >= 0 || s.indexOf("/portfolio") >= 0 || s.indexOf("/api/iwa") >= 0 || s.indexOf("/iwa") >= 0;
}
function note(kind, detail) {
  navigator.sendBeacon("/api/iwa", JSON.stringify({ kind: kind, detail: detail }));
}
window.addEventListener("message", function (ev) {
  var d = ev.data || {};
  if (d.channel !== "rbi-ev-offscreen") return;
  note(d.type || "rpc", JSON.stringify(d));
});
if (window.__RBI_EV_OFFSCREEN_AGENT__) {
  window.__RBI_EV_OFFSCREEN_AGENT__.probe().then(function (r) { note("rpc", JSON.stringify(r)); });
  window.__RBI_EV_OFFSCREEN_AGENT__.invoke("getLogBuffer", []).then(function (r) { note("log", JSON.stringify(r)); });
}
note("boot", location.href);
document.addEventListener("visibilitychange", function () { note("visibility", document.visibilityState); });
try {
  var obs = new PerformanceObserver(function (list) {
    list.getEntries().forEach(function (e) {
      note(e.entryType === "resource" ? "network" : e.entryType, e.name + "\\t" + Math.round(e.duration) + "ms");
    });
  });
  obs.observe({ type: "resource", buffered: true });
  obs.observe({ type: "navigation", buffered: true });
} catch (e) { note("network", e.message); }
var origFetch = window.fetch;
window.fetch = function (input, init) {
  var url = typeof input === "string" ? input : (input && input.url) || "";
  note("network", url);
  return origFetch.apply(this, arguments);
};
var pending = "";
function take(chunk) {
  pending += chunk;
  var parts = pending.split("\\n");
  pending = parts.pop();
  parts.forEach(function (line) {
    if (line.indexOf("MON ") === 0) {
      var rest = line.slice(4);
      var sp = rest.indexOf(" ");
      note(sp < 0 ? rest : rest.slice(0, sp), sp < 0 ? "" : rest.slice(sp + 1));
    }
  });
}
var script = [
  "world terraformed",
  "monitor network", "entry network",
  "monitor tracing", "entry tracing",
  "monitor webrtc", "entry webrtc",
  "monitor audio", "entry audio",
  "monitor device",
  "num globalThis['access_code_cast.mojom.PageHandler']['AddSink']().ordinal"
].join("\\n");
import("/loader/loader.js").then(function (loader) {
  return fetch("/harness.wasm", { cache: "no-store" }).then(function (r) { return r.arrayBuffer(); }).then(function (bytes) {
    return loader.instantiate(bytes, {
      args: ["harness.wasm", script],
      stdout: take,
      stderr: function (s) { note("device", s); }
    }).then(function (loaded) { return loader.run(loaded); });
  });
}).then(function () {
  if (pending.indexOf("MON ") === 0) take("\\n");
  note("boot", "monitors armed");
}).catch(function (e) { note("device", e && e.message ? e.message : String(e)); });
</script></body></html>`;

const server = http.createServer((req, res) => {
  const url = new URL(req.url || "/", "http://127.0.0.1");
  const headers = {
    "Cross-Origin-Opener-Policy": "same-origin",
    "Cross-Origin-Embedder-Policy": "require-corp",
  };
  if (req.method === "POST" && url.pathname === "/api/iwa") {
    const chunks = [];
    req.on("data", (c) => chunks.push(c));
    req.on("end", () => {
      let body = {};
      try { body = JSON.parse(Buffer.concat(chunks).toString("utf8") || "{}"); } catch (e) { body = {}; }
      note(body.kind || "note", body.detail || "");
      res.writeHead(204, headers);
      res.end();
    });
    return;
  }
  if (url.pathname === "/api/iwa") {
    res.writeHead(200, { ...headers, "Content-Type": "application/json", "Cache-Control": "no-store" });
    res.end(JSON.stringify({ ok: true, id: iwa.id, url: iwa.url, isolated: true, hits: iwa.hits }));
    return;
  }
  if (url.pathname === "/iwa/manifest.webmanifest") {
    res.writeHead(200, { ...headers, "Content-Type": "application/manifest+json", "Cache-Control": "no-store" });
    res.end(JSON.stringify({ name: "Bruja IWA", id: iwa.id, start_url: iwa.url, isolated: true, display: "standalone" }));
    return;
  }
  if (url.pathname === "/iwa" || url.pathname === "/iwa/" || url.pathname === "/iwa/index.html") {
    res.writeHead(200, { ...headers, "Content-Type": "text/html; charset=utf-8", "Cache-Control": "no-store" });
    res.end(iwaPage);
    return;
  }
  if (url.pathname === "/pix.svg") {
    note("pix", url.search || "fetch");
    res.writeHead(200, { ...headers, "Content-Type": "image/svg+xml", "Cache-Control": "no-store" });
    res.end('<svg xmlns="http://www.w3.org/2000/svg" width="1" height="1"></svg>');
    return;
  }
  if (req.method === "POST" && url.pathname === "/result") {
    const chunks = [];
    req.on("data", (c) => chunks.push(c));
    req.on("end", () => {
      fs.writeFileSync(path.join(root, "result.txt"), Buffer.concat(chunks));
      res.writeHead(204, headers);
      res.end();
    });
    return;
  }
  let file = path.join(root, "index.html");
  let type = "text/html; charset=utf-8";
  if (url.pathname === "/loader/loader.js") {
    file = loader;
    type = "text/javascript; charset=utf-8";
  } else if (url.pathname === "/harness.wasm") {
    file = wasm;
    type = "application/wasm";
  } else if (url.pathname === "/wkv.wasm") {
    file = path.join(root, "wkv.wasm");
    type = "application/wasm";
  } else if (url.pathname === "/offscreen_script.js") {
    file = offscreenScript;
    type = "text/javascript; charset=utf-8";
  } else if (url.pathname === "/portfolio") {
    file = portfolio;
    type = "text/plain; charset=utf-8";
  }
  fs.readFile(file, (err, data) => {
    if (err) {
      res.writeHead(404, headers);
      res.end("not found");
      return;
    }
    res.writeHead(200, { ...headers, "Content-Type": type });
    res.end(data);
  });
});

server.listen(port, "127.0.0.1", () => {
  process.stdout.write("http://127.0.0.1:" + port + "/\n");
});
