#pragma once
// Port of ultimate_db.go sections 4-5 (core MVCC/TTL engine + Redis-like
// wrappers).

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include "wkv/buffer_pool.h"
#include "wkv/wal.h"

namespace wkv {

constexpr int64_t kExpireNever = INT64_MAX;
constexpr const char* kHashPrefix = "H:";

class DB {
 public:
  DB(BufferPool* bp, BatchingWAL* wal) : bp_(bp), wal_(wal) {}

  uint64_t beginTxn() { return nextTxnId_.fetch_add(1) + 1; }

  // ttlNanos <= 0 means "never expires".
  bool write(PageID pageId, uint64_t txnId, const std::string& key, const std::string& value,
             int64_t ttlNanos, std::string* err);

  // On success (true), *found reports whether the key was present and
  // unexpired as of readTxnId; if so *value holds its bytes.
  bool read(PageID pageId, uint64_t readTxnId, const std::string& key, bool* found,
            std::string* value, std::string* err);

  using ScanIter = std::function<bool(const std::string& key, const std::string& value)>;
  // Iterates over all keys on `pageId` with the given prefix, visible as of
  // readTxnId and not expired. Stops early if `iter` returns false.
  bool scan(PageID pageId, uint64_t readTxnId, const std::string& prefix, const ScanIter& iter,
            std::string* err);

  // HSet inserts a hash field, keyed as "H:<hashKey>:<field>".
  bool hset(PageID pageId, uint64_t txnId, const std::string& hashKey, const std::string& field,
            const std::string& value, int64_t ttlNanos, std::string* err);

  bool close(std::string* err);

  BufferPool* bufferPool() const { return bp_; }

  // Used by recoverDB to replay WAL entries without re-appending to the WAL.
  bool restoreWrite(uint64_t txnId, int64_t expiresAt, PageID pageId, const std::string& key,
                     const std::string& value, std::string* err);
  void setNextTxnId(uint64_t id) { nextTxnId_.store(id); }

 private:
  BufferPool* bp_;
  BatchingWAL* wal_;
  std::atomic<uint64_t> nextTxnId_{0};
};

// Replays a WAL file into `db` (via restoreWrite), verifying each entry's
// CRC-32. A missing WAL file is not an error (fresh database). Sets
// db's next-txn-id counter to the highest txnID seen.
bool recoverDB(const std::string& walPath, DB* db, std::string* err);

}  // namespace wkv
