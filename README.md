# Bruja Browser Test Framework

A Chromium mojom test bench. The page runs a guest you build from `WASMHolePunch`. Test scripts are stored with `WASMKV`. This repository is the source. Wasm binaries are build output and are not committed.

## Run

```
node serve.mjs
```

Open http://127.0.0.1:8807/

## Page

- **Develop.** Nocode blocks append steps to the script. Run script executes it.
  <img width="1917" height="911" alt="Screenshot 2026-09-25 161020" src="https://github.com/user-attachments/assets/65fed1f6-7cb8-444a-b379-d4d4c93c1c45" />

- **Test.** Scripts are stored and loaded with WASMKV (`wkv.wasm`). Device, network, and tracing can be attached to a run.
  <img width="1916" height="896" alt="Screenshot 2026-09-25 161101" src="https://github.com/user-attachments/assets/369665a3-d272-4c63-8d42-d6690283b326" />

- **Monitor.** Device, network, and tracing output.
  <img width="1917" height="922" alt="Screenshot 2026-09-25 161115" src="https://github.com/user-attachments/assets/c4a0619f-7544-4b65-b4a2-3098e990236e" />

- **Debug.** Console, elements, network, Mojo, Blink, and IWA. The Mojo list is the portfolio in `mojovm-portfolio.txt`, 50 methods per page. **Nocode** on a row opens a page-handler step for that method on Develop.

<img width="1917" height="916" alt="Screenshot 2026-09-25 161141" src="https://github.com/user-attachments/assets/8c1b5872-b166-473d-aed7-335d3f2441ef" />

## Extension

Load `iwa-ext` as an unpacked extension. It keeps an offscreen document and plants the IWA frame on http and https tabs. The content script sends the page document to the service worker, which posts it to `/api/iwa`.

## Files

| File | Role |
| --- | --- |
| `serve.mjs` | Server on port 8807 |
| `index.html` | The four tabs |
| `WASMHolePunch/` | Guest sources |
| `ChromeErrStates/` | Catalog, loader, deputy, and harness sources the guest links |
| `quickjs/` | quickjs-ng sources for `quickjs.c`, `dtoa.c`, `libunicode.c`, and `libregexp.c` |
| `WASMKV/` | Script store sources |
| `ng/` | Binding generators |
| `loader.js` | Wasm host used to instantiate the harness |
| `mojovm-portfolio.txt` | Method list for Mojo and Blink |
| `offscreen_script.js` | Offscreen agent loaded by the IWA page |
| `iwa-ext/` | Unpacked extension |
