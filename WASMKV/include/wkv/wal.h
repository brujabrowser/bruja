#pragma once
// Port of ultimate_db.go section 3 (BatchingWAL).
//
// Simplification vs. the Go original: the Go WAL batches concurrent
// Append() calls through a channel drained by a background goroutine
// (group commit). A WASI reactor guest is single-threaded -- there is no
// second goroutine to batch across -- so this port makes each append()
// synchronously write + flush + fsync its own entry. Behavior (durability,
// on-disk format, recovery) is unchanged; only the concurrent-batching
// performance optimization is dropped.
//
// Uses portable C stdio (fopen "ab" + fwrite), not POSIX open/write, so the
// same code builds for native test binaries (MinGW) and the wasm32-wasi
// reactor (wasi-libc backs stdio with fd_write unmodified).

#include <cstdint>
#include <cstdio>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "wkv/page.h"

namespace wkv {

class BatchingWAL {
 public:
  ~BatchingWAL();

  static std::unique_ptr<BatchingWAL> open(const std::string& path, std::string* err);

  bool append(uint64_t txnId, int64_t expiresAt, PageID pageId, const std::string& key,
              const std::string& value, std::string* err);
  bool close(std::string* err);

 private:
  explicit BatchingWAL(FILE* f) : f_(f) {}

  FILE* f_;
  std::vector<uint8_t> mem_;
  std::mutex mu_;
};

}  // namespace wkv
