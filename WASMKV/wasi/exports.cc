// Guest export: wkvExec, a single text-command console (see
// include/wkv/console.h for the grammar) tying the whole ported engine
// together without needing a binary-safe wire format for arbitrary KV
// values -- everything goes through the same (ptr,len)->packed-JSON
// convention as ../../guikit/cpp's guest exports.

#include "abi.h"
#include "wkv/console.h"

namespace wkv::wasi {
namespace {

Console& console() {
  static Console c("wkv.db", "wkv.wal");
  return c;
}

}  // namespace
}  // namespace wkv::wasi

extern "C" {

__attribute__((export_name("wkvExec"))) uint64_t wkvExec(uint32_t cmdPtr, uint32_t cmdLen) {
  std::string cmd = wkv::wasi::readString(cmdPtr, cmdLen);
  std::string result, err;
  if (!wkv::wasi::console().execute(cmd, &result, &err)) {
    return wkv::wasi::packEnvelope("", false, err);
  }
  return wkv::wasi::packEnvelope(result, true, "");
}

}  // extern "C"
