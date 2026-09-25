// Browser hop: load/instantiate/run/call compiled .wasm.
// Host imports match ~/WASMLoader loader/host.go (wasi_snapshot_preview1,
// env.log, env.abort, optional packed string imports). The guest Calls
// this; it does not rewrite ~/WASMWin32.

export const ERRNO = {
  SUCCESS: 0,
  BADF: 8,
  INVAL: 28,
  NOSYS: 52,
};

class ExitError extends Error {
  constructor(code) {
    super("proc_exit " + code);
    this.code = code | 0;
    this.name = "ExitError";
  }
}

// Line-oriented stdin from the page (xterm.js). SharedArrayBuffer:
//   Int32[0] notify word (0 empty, 1 data)
//   Int32[1] byte length
//   bytes at offset 16
function stdinFromSab(sab) {
  const i32 = new Int32Array(sab);
  const u8 = new Uint8Array(sab);
  return {
    take(dst) {
      while (Atomics.load(i32, 1) === 0) {
        const w = Atomics.wait(i32, 0, 0);
        if (w === "timed-out") continue;
      }
      const n = Atomics.load(i32, 1);
      const take = Math.min(n, dst.length);
      dst.set(u8.subarray(16, 16 + take));
      const left = n - take;
      if (left > 0) {
        u8.copyWithin(16, 16 + take, 16 + n);
        Atomics.store(i32, 1, left);
      } else {
        Atomics.store(i32, 1, 0);
        Atomics.store(i32, 0, 0);
      }
      return take;
    },
  };
}

function u32(mem, ptr, v) {
  new DataView(mem.buffer).setUint32(ptr, v >>> 0, true);
}

function u64(mem, ptr, v) {
  const dv = new DataView(mem.buffer);
  dv.setUint32(ptr, v >>> 0, true);
  dv.setUint32(ptr + 4, Math.floor(v / 4294967296) >>> 0, true);
}

function bytes(mem, ptr, n) {
  return new Uint8Array(mem.buffer, ptr, n);
}

function writeCStr(mem, ptr, s) {
  const b = bytes(mem, ptr, s.length + 1);
  for (let i = 0; i < s.length; i++) b[i] = s.charCodeAt(i) & 255;
  b[s.length] = 0;
  return s.length + 1;
}

function writeStrings(mem, ptrs, buf, list) {
  let off = buf;
  for (let i = 0; i < list.length; i++) {
    u32(mem, ptrs + i * 4, off);
    off += writeCStr(mem, off, list[i]);
  }
}

function makeWasi(opts) {
  const args = opts.args && opts.args.length ? opts.args.slice() : ["wasm"];
  const env = opts.env && opts.env.length ? opts.env.slice() : [];
  const stdout = typeof opts.stdout === "function" ? opts.stdout : (t) => console.log(t);
  const stderr = typeof opts.stderr === "function" ? opts.stderr : (t) => console.error(t);
  let stdin = opts.stdin ? Uint8Array.from(typeof opts.stdin === "string"
    ? [...opts.stdin].map((c) => c.charCodeAt(0))
    : opts.stdin) : new Uint8Array();
  let stdinOff = 0;
  const sabIn = opts.stdinSab ? stdinFromSab(opts.stdinSab) : null;
  const inst = { exports: {} };

  function mem() {
    return inst.exports.memory;
  }

  function fdWrite(fd, iovs, iovsLen, nwritten) {
    const m = mem();
    const dv = new DataView(m.buffer);
    let wrote = 0;
    let out = "";
    for (let i = 0; i < iovsLen; i++) {
      const ptr = dv.getUint32(iovs + i * 8, true);
      const len = dv.getUint32(iovs + i * 8 + 4, true);
      if (len) out += new TextDecoder("utf-8", { fatal: false }).decode(bytes(m, ptr, len));
      wrote += len;
    }
    if (fd === 2) stderr(out);
    else stdout(out);
    u32(m, nwritten, wrote);
    return ERRNO.SUCCESS;
  }

  function fdRead(fd, iovs, iovsLen, nread) {
    if (fd !== 0) return ERRNO.BADF;
    const m = mem();
    const dv = new DataView(m.buffer);
    let got = 0;
    for (let i = 0; i < iovsLen; i++) {
      const ptr = dv.getUint32(iovs + i * 8, true);
      const len = dv.getUint32(iovs + i * 8 + 4, true);
      if (!len) continue;
      const slot = bytes(m, ptr, len);
      if (sabIn) {
        got += sabIn.take(slot);
      } else {
        const n = Math.min(len, stdin.length - stdinOff);
        if (n > 0) {
          slot.set(stdin.subarray(stdinOff, stdinOff + n));
          stdinOff += n;
          got += n;
        }
      }
    }
    u32(m, nread, got);
    return ERRNO.SUCCESS;
  }

  const preview1 = {
    args_sizes_get(argc, bufsz) {
      const m = mem();
      u32(m, argc, args.length);
      u32(m, bufsz, args.reduce((n, s) => n + s.length + 1, 0));
      return ERRNO.SUCCESS;
    },
    args_get(argv, buf) {
      writeStrings(mem(), argv, buf, args);
      return ERRNO.SUCCESS;
    },
    environ_sizes_get(count, bufsz) {
      const m = mem();
      u32(m, count, env.length);
      u32(m, bufsz, env.reduce((n, s) => n + s.length + 1, 0));
      return ERRNO.SUCCESS;
    },
    environ_get(environ, buf) {
      writeStrings(mem(), environ, buf, env);
      return ERRNO.SUCCESS;
    },
    clock_res_get(id, res) {
      u64(mem(), res, 1000000);
      return ERRNO.SUCCESS;
    },
    clock_time_get(id, precision, time) {
      u64(mem(), time, Date.now() * 1e6);
      return ERRNO.SUCCESS;
    },
    random_get(ptr, n) {
      crypto.getRandomValues(bytes(mem(), ptr, n));
      return ERRNO.SUCCESS;
    },
    proc_exit(code) {
      throw new ExitError(code);
    },
    proc_raise() { return ERRNO.NOSYS; },
    sched_yield() { return ERRNO.SUCCESS; },
    fd_write: fdWrite,
    fd_read: fdRead,
    fd_seek(fd, offset, whence, newoff) {
      u64(mem(), newoff, 0);
      return fd <= 2 ? ERRNO.SUCCESS : ERRNO.BADF;
    },
    fd_tell(fd, off) {
      u64(mem(), off, 0);
      return fd <= 2 ? ERRNO.SUCCESS : ERRNO.BADF;
    },
    fd_close(fd) {
      return fd <= 2 ? ERRNO.SUCCESS : ERRNO.BADF;
    },
    fd_fdstat_get(fd, stat) {
      if (fd > 2) return ERRNO.BADF;
      const m = mem();
      const b = bytes(m, stat, 24);
      b.fill(0);
      b[0] = 2; // filetype character
      return ERRNO.SUCCESS;
    },
    fd_fdstat_set_flags() { return ERRNO.SUCCESS; },
    fd_prestat_get() { return ERRNO.BADF; },
    fd_prestat_dir_name() { return ERRNO.BADF; },
    fd_datasync() { return ERRNO.SUCCESS; },
    fd_sync() { return ERRNO.SUCCESS; },
    fd_advise() { return ERRNO.SUCCESS; },
    fd_allocate() { return ERRNO.NOSYS; },
    fd_filestat_get() { return ERRNO.NOSYS; },
    fd_filestat_set_size() { return ERRNO.NOSYS; },
    fd_filestat_set_times() { return ERRNO.NOSYS; },
    fd_pread() { return ERRNO.NOSYS; },
    fd_pwrite() { return ERRNO.NOSYS; },
    fd_readdir() { return ERRNO.NOSYS; },
    fd_renumber() { return ERRNO.NOSYS; },
    path_open() { return ERRNO.NOSYS; },
    path_filestat_get() { return ERRNO.NOSYS; },
    path_filestat_set_times() { return ERRNO.NOSYS; },
    path_create_directory() { return ERRNO.NOSYS; },
    path_unlink_file() { return ERRNO.NOSYS; },
    path_remove_directory() { return ERRNO.NOSYS; },
    path_rename() { return ERRNO.NOSYS; },
    path_symlink() { return ERRNO.NOSYS; },
    path_link() { return ERRNO.NOSYS; },
    path_readlink() { return ERRNO.NOSYS; },
    poll_oneoff() { return ERRNO.NOSYS; },
    sock_accept() { return ERRNO.NOSYS; },
    sock_recv() { return ERRNO.NOSYS; },
    sock_send() { return ERRNO.NOSYS; },
    sock_shutdown() { return ERRNO.NOSYS; },
  };

  return { preview1, inst, ExitError };
}

function packedStringImport(arity, fn) {
  return function (memoryHolder) {
    return function () {
      const mem = memoryHolder.exports.memory;
      const args = [];
      for (let i = 0; i < arity; i++) {
        const ptr = arguments[i * 2] >>> 0;
        const len = arguments[i * 2 + 1] >>> 0;
        args.push(len ? new TextDecoder().decode(bytes(mem, ptr, len)) : "");
      }
      let result = "";
      let err = "";
      try {
        const r = fn(args);
        result = r == null ? "" : String(r);
      } catch (e) {
        err = e && e.message ? e.message : String(e);
      }
      const body = JSON.stringify(err
        ? { ok: false, result: "", error: err }
        : { ok: true, result: result });
      const alloc = memoryHolder.exports.alloc;
      if (typeof alloc !== "function") return 0n;
      const ptr = alloc(body.length) >>> 0;
      const u8 = new TextEncoder().encode(body);
      bytes(mem, ptr, u8.length).set(u8);
      return (BigInt(ptr) << 32n) | BigInt(u8.length);
    };
  };
}

function writeLEB(arr, v) {
  v = v >>> 0;
  while (true) {
    const c = v & 0x7f;
    v >>>= 7;
    if (v) arr.push(c | 0x80);
    else {
      arr.push(c);
      return;
    }
  }
}

function readLEB(b, off) {
  let v = 0;
  let shift = 0;
  for (let i = 0; i < 5 && off + i < b.length; i++) {
    const c = b[off + i];
    v |= (c & 0x7f) << shift;
    if ((c & 0x80) === 0) return [v >>> 0, i + 1];
    shift += 7;
  }
  return [0, 0];
}

function asU8(raw) {
  if (raw instanceof Uint8Array) return raw;
  return new Uint8Array(raw);
}

function magicAt(b, off, mag) {
  if (off + mag.length > b.length) return false;
  for (let i = 0; i < mag.length; i++) if (b[off + i] !== mag[i]) return false;
  return true;
}

const CORE_MAGIC = [0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00];
const COMP_MAGIC = [0x00, 0x61, 0x73, 0x6d, 0x0d, 0x00, 0x01, 0x00];

// WASMMimicry wrapWASM: core v1 shell + custom section "unil" holding the
// inner bytes (wasigocvm component). Browser sees version 1; peel recovers
// the guest. Does not retarget the toolchain.
export function wrapMimicryWasm(inner) {
  const payload = asU8(inner);
  const name = [0x75, 0x6e, 0x69, 0x6c]; // "unil"
  const nameLEB = [];
  writeLEB(nameLEB, name.length);
  const customPayloadLen = nameLEB.length + name.length + payload.length;
  const sec = [0];
  writeLEB(sec, customPayloadLen);
  const out = new Uint8Array(8 + sec.length + customPayloadLen);
  out.set(CORE_MAGIC, 0);
  let o = 8;
  out.set(sec, o);
  o += sec.length;
  out.set(nameLEB, o);
  o += nameLEB.length;
  out.set(name, o);
  o += name.length;
  out.set(payload, o);
  return out;
}

export function peelMimicryWasm(raw) {
  const b = asU8(raw);
  if (!magicAt(b, 0, CORE_MAGIC)) return null;
  let off = 8;
  while (off < b.length) {
    const id = b[off++];
    const [n, nlen] = readLEB(b, off);
    if (!nlen) break;
    off += nlen;
    if (off + n > b.length) break;
    const payload = b.subarray(off, off + n);
    off += n;
    if (id !== 0) continue;
    const [nl, nllen] = readLEB(payload, 0);
    if (!nllen) continue;
    const name = new TextDecoder().decode(payload.subarray(nllen, nllen + nl));
    if (name === "unil" || name === "gocos") return payload.subarray(nllen + nl);
  }
  return null;
}

export function extractComponentCore(raw) {
  const b = asU8(raw);
  if (!magicAt(b, 0, COMP_MAGIC)) return null;
  let off = 8;
  let best = null;
  while (off < b.length) {
    const id = b[off++];
    const [n, nlen] = readLEB(b, off);
    if (!nlen) break;
    off += nlen;
    if (off + n > b.length) break;
    const payload = b.subarray(off, off + n);
    off += n;
    if (id !== 1) continue;
    let mod = null;
    if (magicAt(payload, 0, CORE_MAGIC)) mod = payload;
    else {
      const [count, clen] = readLEB(payload, 0);
      if (count && magicAt(payload, clen, CORE_MAGIC)) mod = payload.subarray(clen);
    }
    if (mod && (!best || mod.length > best.length)) best = mod;
  }
  return best;
}

export function guestCore(raw) {
  let b = asU8(raw);
  const peeled = peelMimicryWasm(b);
  if (peeled && peeled.length) b = peeled;
  const core = extractComponentCore(b);
  if (core && core.length) return core;
  return b;
}

export async function inspect(bytesIn) {
  const core = guestCore(bytesIn);
  const mod = await WebAssembly.compile(core);
  const imports = WebAssembly.Module.imports(mod).map((i) => ({
    module: i.module,
    name: i.name,
    kind: i.kind,
  }));
  const exports = WebAssembly.Module.exports(mod).map((e) => ({
    name: e.name,
    kind: e.kind,
  }));
  return { imports, exports, size: core.byteLength };
}

function stubImports(mod, base) {
  const imports = Object.assign({}, base);
  const nosys = () => ERRNO.NOSYS;
  const ok = () => ERRNO.SUCCESS;
  WebAssembly.Module.imports(mod).forEach((i) => {
    if (!imports[i.module]) imports[i.module] = {};
    if (imports[i.module][i.name] == null) {
      if (i.kind === "memory") {
        imports[i.module][i.name] = new WebAssembly.Memory({ initial: 1 });
      } else if (i.kind === "table") {
        imports[i.module][i.name] = new WebAssembly.Table({ initial: 0, element: "anyfunc" });
      } else if (i.kind === "global") {
        imports[i.module][i.name] = new WebAssembly.Global({ value: "i32", mutable: false }, 0);
      } else {
        imports[i.module][i.name] = i.name === "proc_exit" ? base.wasi_snapshot_preview1.proc_exit : nosys;
        if (i.name === "sched_yield") imports[i.module][i.name] = ok;
      }
    }
  });
  return imports;
}

// WASI 0.2 as flattened core imports (i32 handles + retptrs). Not WIT,
// not a component-model instantiator — wasm-component-ld already lowered
// the names; this is libc-shaped host for those functions.
function makeWasi02(opts, inst) {
  const stdout = typeof opts.stdout === "function" ? opts.stdout : (t) => console.log(t);
  const stderr = typeof opts.stderr === "function" ? opts.stderr : (t) => console.error(t);
  const args = opts.args && opts.args.length ? opts.args.slice() : ["wasm"];
  const envList = opts.env && opts.env.length ? opts.env.slice() : [];
  const sabIn = opts.stdinSab ? stdinFromSab(opts.stdinSab) : null;
  const handles = new Map();
  let nextH = 1;
  function intern(kind) {
    const h = nextH++;
    handles.set(h, kind);
    return h;
  }
  const hIn = intern("in");
  const hOut = intern("out");
  const hErr = intern("err");

  function memory() {
    return inst.exports.memory;
  }
  function realloc(oldPtr, oldLen, align, n) {
    const fn = inst.exports.cabi_realloc;
    if (typeof fn !== "function") return 0;
    return fn(oldPtr >>> 0, oldLen >>> 0, align >>> 0, n >>> 0) >>> 0;
  }
  function i32(ptr, v) {
    new DataView(memory().buffer).setInt32(ptr, v | 0, true);
  }
  function u32get(ptr) {
    return new DataView(memory().buffer).getUint32(ptr, true);
  }
  function u64(ptr, v) {
    const dv = new DataView(memory().buffer);
    dv.setUint32(ptr, v >>> 0, true);
    dv.setUint32(ptr + 4, Math.floor(v / 4294967296) >>> 0, true);
  }
  function putBytes(ptr, arr) {
    bytes(memory(), ptr, arr.length).set(arr);
  }
  function allocBytes(arr) {
    const p = realloc(0, 0, 1, arr.length);
    if (p && arr.length) putBytes(p, arr);
    return p;
  }
  function allocList(strs) {
    const n = strs.length;
    const lp = realloc(0, 0, 4, n * 8);
    for (let i = 0; i < n; i++) {
      const enc = new TextEncoder().encode(strs[i]);
      const sp = allocBytes(enc);
      i32(lp + i * 8, sp);
      i32(lp + i * 8 + 4, enc.length);
    }
    return lp;
  }
  function okEmpty(ret) {
    if (ret) i32(ret, 0);
  }
  function okU64(ret, n) {
    if (!ret) return;
    i32(ret, 0);
    u64(ret + 8, n);
  }
  function errClosed(ret) {
    if (!ret) return;
    i32(ret, 1);
    i32(ret + 4, 1);
  }
  function errGeneric(ret) {
    if (!ret) return;
    i32(ret, 1);
    i32(ret + 4, 0);
  }
  function putListU8(ret, arr) {
    const p = allocBytes(arr);
    i32(ret, 0);
    i32(ret + 4, p);
    i32(ret + 8, arr.length);
  }
  function drop(h) {
    handles.delete(h >>> 0);
  }
  function stdinBlock() {
    if (!opts.stdinSab) return;
    const i32a = new Int32Array(opts.stdinSab);
    while (Atomics.load(i32a, 1) === 0) {
      Atomics.wait(i32a, 0, 0);
    }
  }

  function dispatch(mod, name, a) {
    const m = String(mod);
    const n = String(name);
    if (n.indexOf("[resource-drop]") === 0) {
      drop(a[0]);
      return;
    }
    if (n === "exit" || n === "exit-with-code") {
      throw new ExitError(a[0] | 0);
    }
    if (n === "get-stdout") return hOut;
    if (n === "get-stderr") return hErr;
    if (n === "get-stdin") return hIn;
    if (n === "get-arguments") {
      const lp = allocList(args);
      i32(a[0], lp);
      i32(a[0] + 4, args.length);
      return;
    }
    if (n === "get-environment") {
      const lp = allocList(envList);
      i32(a[0], lp);
      i32(a[0] + 4, envList.length);
      return;
    }
    if (n === "now" && m.indexOf("monotonic-clock") >= 0) {
      return BigInt(Date.now()) * 1000000n;
    }
    if (n === "resolution" && m.indexOf("monotonic-clock") >= 0) {
      return 1000000n;
    }
    if (n === "now" && m.indexOf("wall-clock") >= 0) {
      const ms = Date.now();
      u64(a[0], Math.floor(ms / 1000));
      i32(a[0] + 8, (ms % 1000) * 1000000);
      return;
    }
    if (n === "resolution" && m.indexOf("wall-clock") >= 0) {
      u64(a[0], 0);
      i32(a[0] + 8, 1000000);
      return;
    }
    if (n === "get-random-bytes") {
      const want = Number(a[0]);
      const buf = new Uint8Array(want > 0 && want < 1e6 ? want : 0);
      if (buf.length) crypto.getRandomValues(buf);
      putListU8(a[1], buf);
      return;
    }
    if (n === "[method]output-stream.check-write") {
      okU64(a[1], 4096);
      return;
    }
    if (n === "[method]output-stream.write") {
      const ptr = a[1] >>> 0;
      const len = a[2] >>> 0;
      if (len) {
        const t = new TextDecoder("utf-8", { fatal: false }).decode(bytes(memory(), ptr, len));
        const k = handles.get(a[0] >>> 0);
        if (k === "err") stderr(t);
        else stdout(t);
      }
      okEmpty(a[3]);
      return;
    }
    if (n === "[method]output-stream.blocking-flush") {
      okEmpty(a[1]);
      return;
    }
    if (n === "[method]output-stream.subscribe" || n === "[method]input-stream.subscribe") {
      return intern("poll");
    }
    if (n === "[method]pollable.block") {
      const k = handles.get(a[0] >>> 0);
      if (k === "in" || k === "poll") stdinBlock();
      return;
    }
    if (n === "poll") {
      const inPtr = a[0] >>> 0;
      const inLen = a[1] >>> 0;
      const ret = a[2] >>> 0;
      for (let i = 0; i < inLen; i++) {
        const h = u32get(inPtr + i * 4);
        if (handles.get(h) === "in") stdinBlock();
      }
      const lp = realloc(0, 0, 4, inLen * 4);
      for (let i = 0; i < inLen; i++) i32(lp + i * 4, i);
      i32(ret, lp);
      i32(ret + 4, inLen);
      return;
    }
    if (n === "[method]input-stream.read") {
      const max = Number(a[1]);
      const nmax = max > 0 && max < 65536 ? max : 0;
      const buf = new Uint8Array(nmax);
      let got = 0;
      if (nmax && sabIn) got = sabIn.take(buf);
      putListU8(a[2], buf.subarray(0, got));
      return;
    }
    if (n.indexOf("get-terminal-") === 0) {
      i32(a[0], 0);
      return;
    }
    if (n === "subscribe-instant" || n === "subscribe-duration") {
      return intern("poll");
    }
    if (n === "instance-network") return intern("net");
    if (m.indexOf("filesystem") >= 0 || m.indexOf("sockets") >= 0) {
      errGeneric(a[a.length - 1]);
      return 0;
    }
    if (n.charAt(0) === "[" || a.length && typeof a[a.length - 1] === "number") {
      errClosed(a[a.length - 1]);
    }
    return 0;
  }

  return { dispatch, intern };
}

function bindP2Imports(mod, imports, p2) {
  WebAssembly.Module.imports(mod).forEach((i) => {
    const m = String(i.module);
    if (m.indexOf("wasi:") !== 0) return;
    if (!imports[m]) imports[m] = {};
    const n = i.name;
    imports[m][n] = function () {
      return p2.dispatch(m, n, Array.prototype.slice.call(arguments));
    };
  });
}

// Client file table for goclibc. The unil sandbox (wasmv16 / quickjs-ng)
// is the JS runner, and this page is that client. serve.mjs only delivers
// bytes; it does not instantiate the guest.
function makeGocLibc(mem) {
  const boxes = [null];
  const fds = [null];
  const dirs = new Set();
  let cwd = ".";
  function norm(p) {
    return String(p).replace(/\\/g, "/").replace(/\/+$/, "");
  }
  function parent(p) {
    const s = norm(p);
    const i = s.lastIndexOf("/");
    return i < 0 ? "" : s.slice(0, i);
  }
  function readStr(ptr, n) {
    const m = mem();
    if (!m || n <= 0) return "";
    let s = "";
    const b = bytes(m, ptr, n);
    for (let i = 0; i < b.length; i++) s += String.fromCharCode(b[i]);
    return s;
  }
  function putStat(out, isdir, size) {
    const m = mem();
    const mode = isdir ? 0x4000 | 0o755 : 0x8000 | 0o644;
    u32(m, out, mode);
    u32(m, out + 4, isdir ? 1 : 0);
    u64(m, out + 8, size);
  }
  function newBox(path, data) {
    const box = { path: norm(path), data: data || new Uint8Array(0), refs: 1 };
    boxes.push(box);
    return boxes.length - 1;
  }
  function newFd(box, app, pos) {
    fds.push({ box, pos, append: app });
    return fds.length - 1;
  }
  function fdOk(fd) {
    return fd > 0 && fd < fds.length && fds[fd];
  }
  const api = {
    open(pathPtr, pathLen, flags) {
      const path = norm(readStr(pathPtr, pathLen));
      if (!path) return -22;
      const trunc = flags & 8;
      const creat = flags & 4;
      const app = flags & 16 ? 1 : 0;
      let data = null;
      const hit = boxes.find((b) => b && b.path === path);
      if (!trunc && hit) data = hit.data;
      else if (!trunc && !creat && !hit) return -2;
      else data = new Uint8Array(0);
      const id = newBox(path, data);
      let pos = 0;
      if (app) pos = boxes[id].data.length;
      return newFd(id, app, pos);
    },
    read(fd, ptr, n) {
      if (!fdOk(fd) || n <= 0) return fdOk(fd) ? 0 : -9;
      const slot = fds[fd];
      const data = boxes[slot.box].data;
      if (slot.pos >= data.length) return 0;
      const avail = Math.min(n, data.length - slot.pos);
      bytes(mem(), ptr, avail).set(data.subarray(slot.pos, slot.pos + avail));
      slot.pos += avail;
      return avail;
    },
    write(fd, ptr, n) {
      if (!fdOk(fd)) return -9;
      const slot = fds[fd];
      const box = boxes[slot.box];
      const src = n > 0 ? bytes(mem(), ptr, n) : new Uint8Array(0);
      if (slot.append) slot.pos = box.data.length;
      const end = slot.pos + src.length;
      const next = new Uint8Array(Math.max(box.data.length, end));
      next.set(box.data);
      next.set(src, slot.pos);
      box.data = next;
      slot.pos = end;
      return src.length;
    },
    close(fd) {
      if (!fdOk(fd)) return -9;
      const slot = fds[fd];
      boxes[slot.box].refs--;
      fds[fd] = null;
      return 0;
    },
    seek(fd, off, whence) {
      if (!fdOk(fd)) return -9;
      const slot = fds[fd];
      const len = boxes[slot.box].data.length;
      let base = 0;
      if (whence === 1) base = slot.pos;
      if (whence === 2) base = len;
      const npos = base + (off | 0);
      if (npos < 0) return -22;
      slot.pos = npos;
      return npos;
    },
    flush(fd) {
      return fdOk(fd) ? 0 : -9;
    },
    stat(pathPtr, pathLen, out) {
      const path = norm(readStr(pathPtr, pathLen));
      if (dirs.has(path)) {
        putStat(out, 1, 0);
        return 0;
      }
      const hit = boxes.find((b) => b && b.path === path && b.refs > 0);
      if (!hit) return -2;
      putStat(out, 0, hit.data.length);
      return 0;
    },
    fstat(fd, out) {
      if (!fdOk(fd)) return -9;
      const box = boxes[fds[fd].box];
      putStat(out, dirs.has(box.path) ? 1 : 0, box.data.length);
      return 0;
    },
    mkdir(pathPtr, pathLen) {
      const path = norm(readStr(pathPtr, pathLen));
      if (!path) return -22;
      if (dirs.has(path)) return -17;
      dirs.add(path);
      return 0;
    },
    unlink(pathPtr, pathLen) {
      const path = norm(readStr(pathPtr, pathLen));
      if (dirs.has(path)) return -21;
      let found = false;
      for (let i = 1; i < boxes.length; i++) {
        if (boxes[i] && boxes[i].path === path) {
          boxes[i] = null;
          found = true;
        }
      }
      return found ? 0 : -2;
    },
    rmdir(pathPtr, pathLen) {
      const path = norm(readStr(pathPtr, pathLen));
      if (!dirs.has(path)) return -2;
      dirs.delete(path);
      return 0;
    },
    rename(oldPtr, oldLen, newPtr, newLen) {
      const a = norm(readStr(oldPtr, oldLen));
      const b = norm(readStr(newPtr, newLen));
      if (dirs.has(a)) {
        dirs.delete(a);
        dirs.add(b);
        return 0;
      }
      const hit = boxes.find((x) => x && x.path === a);
      if (!hit) return -2;
      hit.path = b;
      return 0;
    },
    access(pathPtr, pathLen) {
      const path = norm(readStr(pathPtr, pathLen));
      if (dirs.has(path)) return 0;
      return boxes.some((b) => b && b.path === path) ? 0 : -2;
    },
    getcwd(buf, cap) {
      const s = cwd;
      if (cap < s.length + 1) return -22;
      const m = mem();
      const b = bytes(m, buf, s.length + 1);
      for (let i = 0; i < s.length; i++) b[i] = s.charCodeAt(i) & 255;
      b[s.length] = 0;
      return s.length;
    },
    chdir(pathPtr, pathLen) {
      const path = norm(readStr(pathPtr, pathLen));
      if (!dirs.has(path) && path !== "." && path !== "") return -2;
      cwd = path || ".";
      return 0;
    },
    readdir(pathPtr, pathLen, buf, cap) {
      const path = norm(readStr(pathPtr, pathLen));
      const names = [];
      dirs.forEach((d) => {
        if (parent(d) === path) names.push("d" + d.slice(path.length + 1));
      });
      boxes.forEach((b) => {
        if (b && parent(b.path) === path) names.push("f" + b.path.slice(b.path.lastIndexOf("/") + 1));
      });
      const out = [];
      for (let i = 0; i < names.length; i++) {
        const kind = names[i].charCodeAt(0);
        const rest = names[i].slice(1);
        out.push(kind);
        for (let j = 0; j < rest.length; j++) out.push(rest.charCodeAt(j) & 255);
        out.push(0);
      }
      if (out.length > cap) return -22;
      if (out.length) bytes(mem(), buf, out.length).set(out);
      return out.length;
    },
    dup(fd) {
      if (!fdOk(fd)) return -9;
      const slot = fds[fd];
      boxes[slot.box].refs++;
      return newFd(slot.box, slot.append, slot.pos);
    },
  };
  return api;
}

export async function instantiate(bytes, opts) {
  opts = opts || {};
  const core = guestCore(bytes);
  const mod = await WebAssembly.compile(core);
  const wasi = makeWasi(opts);
  const p2 = makeWasi02(opts, wasi.inst);
  const env = {
    log(ptr, len) {
      const m = wasi.inst.exports.memory;
      if (!m || !len) return;
      const t = new TextDecoder("utf-8", { fatal: false }).decode(bytes(m, ptr, len));
      const stdout = typeof opts.stdout === "function" ? opts.stdout : (s) => console.log(s);
      stdout(t);
    },
    abort(code) {
      throw new ExitError(code | 0);
    },
  };
  if (opts.stringImports) {
    for (const name of Object.keys(opts.stringImports)) {
      const spec = opts.stringImports[name];
      env[name] = packedStringImport(spec.arity, spec.fn)(wasi.inst);
    }
  }
  const base = {
    wasi_snapshot_preview1: wasi.preview1,
    wasi_unstable: wasi.preview1,
    env,
    goclibc: makeGocLibc(() => wasi.inst.exports.memory),
  };
  const imports = stubImports(mod, base);
  bindP2Imports(mod, imports, p2);
  const instance = await WebAssembly.instantiate(mod, imports);
  wasi.inst.exports = instance.exports;
  return { module: mod, instance, exports: instance.exports, ExitError };
}

export async function run(loaded) {
  const exp = loaded.exports || loaded.instance.exports;
  try {
    if (typeof exp._start === "function") {
      exp._start();
      return 0;
    }
    const names = Object.keys(exp);
    for (let i = 0; i < names.length; i++) {
      const n = names[i];
      if (typeof exp[n] !== "function") continue;
      if (n === "run" || n.indexOf("#run") >= 0) {
        const got = exp[n]();
        return got | 0;
      }
    }
    return 0;
  } catch (e) {
    if (e && (e.name === "ExitError" || typeof e.code === "number")) return e.code | 0;
    throw e;
  }
}

export async function call(loaded, name, args) {
  const fn = (loaded.exports || loaded.instance.exports)[name];
  if (typeof fn !== "function") throw new Error("unknown export " + name);
  const a = (args || []).map((x) => x | 0);
  try {
    const got = fn(...a);
    return got;
  } catch (e) {
    if (e && (e.name === "ExitError" || typeof e.code === "number")) return e.code | 0;
    throw e;
  }
}

export { ExitError };
