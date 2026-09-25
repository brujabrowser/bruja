# WASMLime

The process-bootstrap rung of the `~/WASM*` stack: a small, real analog of
[`fuchsia_web/webengine/web_engine_main.cc`](https://chromium.googlesource.com/chromium/src/+/refs/heads/main/fuchsia_web/webengine/web_engine_main.cc)
and its `WebEngineMainDelegate`/`ContextProviderMain`. Every other repo in
this family ports one specific Mojo layer or `chrome.*` API; this one ports
the *entry point* that ties them together, so a consumer's `main()` can
shrink to what Fuchsia's own `main()` already is:

```
sockets  →  HolePunch  →  Mojo C System  →  Mojo C++ System  →  Bindings  →  APIs
whp::net    whp::punch     WASMThunker        WASMCadidumKernel   WASMCadidumBindings   WASMProcess, ...
                                                                                              ^
                                                                                    lime::Main() drives
                                                                                    the process around
                                                                                    whichever of these
                                                                                    a MainDelegate wires up
```

## The mapping

| `fuchsia_web/webengine` | WASMLime |
|---|---|
| `web_engine_main.cc`'s `main()` | `examples/lime/main.cc`'s `main()` -- read it first |
| `base::CommandLine::Init`/`::ForCurrentProcess` | `lime::CommandLine::Init`/`::ForCurrentProcess` |
| `switches::kProcessType` (narrowed) | `lime::switches::kRole` |
| `switches::kContextProvider` | `lime::switches::kContextProvider` |
| `WebEngineMainDelegate : content::ContentMainDelegate` | your class `: lime::MainDelegate` |
| `content::ContentMainParams` | `lime::MainParams` |
| `content::ContentMain()` | `lime::Main()` |
| `ContextProviderMain()` | `lime::ContextProviderMain()` |

`lime::MainDelegate`'s hooks are `ContentMainDelegate`'s, narrowed to what
has a real counterpart once there's no sandbox and no browser/renderer/gpu
process split:

| `ContentMainDelegate` | `lime::MainDelegate` |
|---|---|
| `BasicStartupComplete()` | `BasicStartupComplete()` |
| `PreSandboxStartup()` | `PreTransportStartup()` -- runs before WASMHolePunch/WASMCadidumKernel/WASMCadidumBindings are touched |
| `PreBrowserMain()` | `PreRunLoop()` |
| `RunProcess(process_type, params)` | `RunProcess(role)` -- `role` is `--role=offerer`/`--role=peer`, the same split `wpr_cadmium` already hardcodes as `--offerer` |
| *(owned by `ContentMainRunner`)* | `ProcessExiting()` -- every embedder needs a symmetric teardown hook |

`lime::Main()` runs the hooks in order, short-circuiting on the first one
that returns an exit code, and otherwise falls into the default run loop,
`whp::Executor::Current().Run()`:

```
BasicStartupComplete() -> PreTransportStartup() -> PreRunLoop()
  -> RunProcess(role) -> [whp::Executor::Current().Run()] -> ProcessExiting()
```

## `ContextProviderMain`

Real Fuchsia: an always-resident FIDL service; each
`fuchsia.web.ContextProvider/Create` request spawns a fresh **sandboxed OS
process** (a Context) and hands the caller a channel into it.

Nothing in this stack spawns processes, so `lime::ContextProviderMain` is
an honest analog, not a port: it's still a long-running dispatcher (loops
until `max_contexts` is reached, or forever by default) that mints one
fresh **`whp::platform::Invitation`** (one dedicated Mojo pipe over one
freshly bound loopback UDP port) per request, instead of a process. The
control connection asking for one is a two-line plain-text TCP protocol,
not FIDL:

```
client -> TCP connect to control_bind
client -> "CREATE <client_udp_host>:<client_udp_port>\n"
server -> "PORT <freshly bound udp port>\n"          (then closes the TCP conn)
both sides -> whp::punch::Punch + whp::platform::InviteOver over that UDP pair
```

`lime::RequestContext(control_address, &invitation)` is the client side of
that exchange. See `tests/context_provider/roundtrip.cc` for both halves
running against each other over real loopback sockets.

## Known simplifications (documented, not hidden)

- **No process sandbox, no multi-process split.** A `MainDelegate::RunProcess`
  `role` distinguishes at most "offerer" vs. "peer" (`wpr_cadmium`'s existing
  split) inside one binary -- there is no separate browser/renderer/gpu
  process the way real Content has.
- **`ContextProviderMain` mints a Mojo pipe, not an OS process.** See above.
  The control-plane protocol is a two-line TCP exchange invented for this
  repo, not FIDL, and has no retry/backoff: a client must call
  `RequestContext` promptly after the port comes back, same tradeoff
  `wpr_cadmium`'s own two-terminal demo already has (a human has to start
  the offerer before the peer connects).
- **`whp::Executor::Current().Run()` returns as soon as its queue is empty**,
  not just on `Quit()` (see `WASMHolePunch/src/base/executor.cc`) -- so
  `lime::Main()`'s "default run loop" only actually blocks for as long as
  whatever the delegate posted onto it keeps posting more work. A real
  long-lived process (e.g. hosting an `Invitation`'s pump loop) needs its
  own explicit polling loop in `RunProcess()`, same as `wpr_cadmium`'s
  `BridgeLoop` already does -- `lime::Main()` doesn't invent a substitute
  for that.
- **WASI target is the shared reactor convention** (`--export-all
  --no-entry`, matching `whp`/`wck`/`wcb`/`wpr`'s own `_wasm` targets), not
  a standalone `_start` command module, even though this repo's whole job
  is being a process entry point. `lime::Main()` is an ordinary exported
  C++ function either way; a real standalone command build is just a
  different link line.

## Build (native, Windows)

MinGW g++ or MSVC. Requires the sibling `../WASMHolePunch` checkout.

```
cmake -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

## TLS / OpenSSL

TLS is go++ **wasigocvm** OpenSSL 3 (`~/go++/toolchain/openssl-wasm`
`libssl.a` / `libcrypto.a`), memory BIOs, the same TlsTransport shape
`wasigocvm_tls.hpp` uses (`SSL_do_handshake`, SNI). Not Schannel, not
vcpkg mingw DLLs, not Bytecode Alliance `wasi:crypto`. CMake imports
those static libs before WASMv16 so libdatachannel is not FetchContent'd
(`WASMTTY_WITH_NATIVE=OFF`).

## WASM (wasigocvm)

FrameWindow occupancy is WASMWin32 on wasigocvm (guest is Windows). Build
with the go++ wasigocvm clang, not wasi-sdk Preview 1 as the machine:

```
cmake -B build-wasigocvm -DCMAKE_TOOLCHAIN_FILE=$HOME/go++/toolchain/share/cmake/wasigocvm.cmake -DWASIGO_GOCVM=1
cmake --build build-wasigocvm --target wlm
```

## WASI (legacy note)

Prefer wasigocvm above. The old wasi-sdk line is not the TLS/socket ABI.

```
cmake -B build-wasi -DCMAKE_TOOLCHAIN_FILE=cmake/wasi-sdk.cmake
cmake --build build-wasi
```

## Examples

```
build/wlm_example --role=offerer --bind 127.0.0.1:0 --peer 127.0.0.1:<port>
build/wlm_example --role=peer    --bind 127.0.0.1:0 --peer 127.0.0.1:<port>
build/wlm_example --context-provider
```

## License

New code is BSD-3-Clause. Chromium reference files remain Copyright The
Chromium Authors.
