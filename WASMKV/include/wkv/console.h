#pragma once
// Text command console tying the ported engine together into something a
// WASI reactor export (or a native REPL) can drive with plain strings,
// without needing a binary-safe wire format for the guest ABI.
//
// Grammar (one command per call, whitespace-separated, trailing argument
// consumes the rest of the line so values/text may contain spaces):
//   SET <page> <key> <value>
//   GET <page> <key>
//   DEL <page> <key>               -- tombstone (empty value); GET returns (nil)
//   SCAN <page> <prefix>
//   HSET <page> <hashKey> <field> <value>
//   INDEX <docId> <text...>
//   SEARCH <query...>
//   CLOSE

#include <memory>
#include <string>

#include "wkv/buffer_pool.h"
#include "wkv/db.h"
#include "wkv/disk_manager.h"
#include "wkv/index.h"
#include "wkv/wal.h"

namespace wkv {

class Console {
 public:
  // dbPath/walPath name the backing files; poolSize is the buffer pool's
  // frame count. Opens (and recovers, if a WAL exists) lazily on first
  // execute() call.
  Console(std::string dbPath, std::string walPath, int poolSize = 64);

  // Runs one command, filling *result with its textual output. Returns
  // false and sets *err on failure (I/O error, unknown command, bad args).
  bool execute(const std::string& command, std::string* result, std::string* err);

 private:
  bool ensureOpen(std::string* err);

  std::string dbPath_;
  std::string walPath_;
  int poolSize_;
  bool opened_ = false;

  std::unique_ptr<DiskManager> disk_;
  std::unique_ptr<BufferPool> bp_;
  std::unique_ptr<BatchingWAL> wal_;
  std::unique_ptr<DB> db_;
  MemIndex index_;
};

}  // namespace wkv
