# WASMKV

A from-scratch C++ port of `ultimate_db.go` (a page-based, MVCC/TTL key-value
engine with a write-ahead log, a B+ tree, and a boolean-query inverted
index), built with [wasi-sdk](https://github.com/WebAssembly/wasi-sdk) as a
`wasm32-wasi` reactor module -- same toolchain and build shape as
[`../guikit/cpp`](../guikit/cpp).

## Layout

```
WASMKV/
  CMakeLists.txt          native-test target + wasm32-wasi reactor target
  build.ps1               builds + tests both
  include/wkv/
    page.h                  PageID, Page, on-disk LE helpers
    disk_manager.h            per-page file I/O
    buffer_pool.h              LRU frame pool over DiskManager
    wal.h                       write-ahead log
    db.h                         MVCC/TTL read/write/scan, WAL recovery, HSet
    btree.h                       B+ tree, cursor, sort-merge join
    index.h                        tokenizer, MemIndex, SegmentSearcher
    query.h                         boolean query AST + parser
    varint.h, crc32.h, time_util.h  small shared utilities
    console.h                        text command console (see below)
  src/                    the above, portable C++, no platform code
  wasi/
    abi.h / abi.cc            alloc/free/pack -- same wire convention as guikit/cpp
    exports.cc                  wkvExec guest export
  tests/                  native unit tests (no wasm runtime needed)
```

## Quick start

```powershell
# Native unit tests
cmake -S . -B build-native -G "MinGW Makefiles"
cmake --build build-native
build-native/wkv_tests.exe

# Wasm reactor guest
cmake -S . -B build-wasm -G "MinGW Makefiles" `
    -DCMAKE_TOOLCHAIN_FILE="$env:WASI_SDK_PATH/share/cmake/wasi-sdk.cmake"
cmake --build build-wasm
# -> build-wasm/wkv.wasm
```

Or just run `build.ps1`, which does both and fails fast if the native tests
don't pass.

## Guest ABI

Same shape as `../guikit/cpp/wasi`: the guest exports `memory`,
`alloc(size:u32)->u32`, `free(ptr:u32,size:u32)->()`, and a single business
export `wkvExec(cmdPtr,cmdLen)->i64` -- one `(ptr,len)` string in, one packed
`(ptr<<32|len)` result pointing at a JSON envelope
`{"ok":bool,"result"|"error":string}`. Built as a reactor (`_initialize`,
no `main()`), assembled the same way as guikit/cpp: `-nostartfiles` +
`crt1-reactor.o` + `--no-entry` (see guikit/cpp/README.md for why, on this
wasi-sdk install, that's used instead of `-mexec-model=reactor`).

`wkvExec` takes one line of text and runs it against a lazily-opened
database (`wkv.db` / `wkv.wal` in the guest's working directory) via
`wkv::Console` (`include/wkv/console.h`):

```
SET <page> <key> <value>          -- MVCC write, no TTL
GET <page> <key>                  -- latest value visible now
DEL <page> <key>                  -- tombstone (empty value); GET returns (nil)
SCAN <page> <prefix>              -- "key=value\n" per match
HSET <page> <hashKey> <field> <value>
INDEX <docId> <text>              -- add to the in-memory inverted index
SEARCH <query>                    -- boolean query, e.g. "(a OR b) AND c NOT d"
CLOSE                             -- flush + close; next command reopens
```

A single text command was chosen over one export per operation so that
arbitrary key/value bytes don't need a binary-safe encoding layered on top
of the JSON-envelope convention -- everything is already a string. The
lower-level engine (`wkv::DB`, `wkv::BTree`, `wkv::BufferPool`, ...) is a
normal static library (`wkv_core`) and can be linked and driven directly by
a native host that doesn't need the reactor ABI at all; `wkv::BTree` /
`wkv::BTreeCursor` / `sortMergeJoin` in particular aren't wired into the
console and are only reachable that way today.

## Porting notes (differences from `ultimate_db.go`)

- **WAL is synchronous, not batched.** The Go original batches concurrent
  `Append()` calls through a channel drained by a background goroutine
  (group commit). A WASI reactor guest is single-threaded -- there's no
  second goroutine to batch across -- so `BatchingWAL::append` here
  synchronously writes, flushes, and fsyncs each entry. On-disk format and
  recovery are unchanged; only the concurrent-batching performance
  optimization is dropped.
- **Real file I/O**, not a host import bridge. Unlike `../guikit/cpp` (which
  proxies DB/file access to a JS/Go host because it targets an
  app-in-a-browser sandbox), this port does direct `fopen`/`fseek`/
  `fread`/`fwrite` I/O, which wasi-libc backs with real `fd_*` WASI
  syscalls -- it needs a preopened directory (e.g. `wasmtime run --dir=.`)
  but no host-side glue code.
- **`BTreePage::rightmostChildId()` is 4 bytes**, not 8, even though every
  other `PageID` field in the header and every internal cell's child
  pointer is 8 bytes. This is `ultimate_db.go`'s own on-disk layout
  (`binary.LittleEndian.Uint32` on that one field) and is preserved
  bit-for-bit here for wire compatibility with files an unmodified
  `ultimate_db.go` wrote, not "fixed."
- **`tokenize()` is ASCII-only.** The Go original calls `strings.ToLower`
  (full Unicode) before splitting, but its `clean()` predicate only ever
  recognizes ASCII letters/digits regardless of input -- so for the
  predicate's actual behavior, ASCII-only lowering is equivalent for any
  input that matters to tokenization.
- Errors are `bool` return + `std::string* err` out-param throughout
  (no exceptions anywhere in the engine), matching guikit/cpp's own
  no-exceptions convention for the wasm guest -- `-fno-exceptions` is
  passed for the wasm build, which would turn an accidental `throw` back
  into a compile error rather than a silent link failure.
