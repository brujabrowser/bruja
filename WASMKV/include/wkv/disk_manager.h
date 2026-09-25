#pragma once
// Port of ultimate_db.go section 2 (DiskManager). Uses portable C stdio
// (fseek/fread/fwrite) rather than POSIX pread/pwrite so the same code
// builds for native test binaries (MinGW lacks pread/pwrite) and for the
// wasm32-wasi reactor (wasi-libc backs stdio with fd_read/fd_write/fd_seek
// unmodified).

#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "wkv/page.h"

namespace wkv {

class DiskManager {
 public:
  ~DiskManager();

  // Opens (creating if needed) the file at `path`. Returns nullptr and sets
  // *err on failure.
  static std::unique_ptr<DiskManager> open(const std::string& path, std::string* err);

  // Reads exactly kPageSize bytes into `data`. A short read past end-of-file
  // is not an error -- the untouched tail of `data` is left as-is (callers
  // pass a zeroed buffer), mirroring the Go port's io.EOF-is-ok behavior.
  bool readPage(PageID id, uint8_t* data, std::string* err);
  bool writePage(PageID id, const uint8_t* data, std::string* err);
  bool fileSize(int64_t* size, std::string* err);

 private:
  explicit DiskManager(FILE* f) : f_(f) {}

  FILE* f_;
  // Used when fopen fails on a host with no filesystem (the browser loader).
  // Native builds still require a real file.
  std::vector<uint8_t> mem_;
  std::mutex mu_;
};

}  // namespace wkv
