#include <cstdio>

#include "test_framework.h"
#include "wkv/buffer_pool.h"
#include "wkv/db.h"
#include "wkv/disk_manager.h"
#include "wkv/wal.h"

using namespace wkv;

namespace {
const char* kDbPath = "test_wal_recovery.db";
const char* kWalPath = "test_wal_recovery.wal";
}  // namespace

WKV_TEST(RecoverDBReplaysWalAfterCrash) {
  std::remove(kDbPath);
  std::remove(kWalPath);
  std::string err;

  {
    auto disk = DiskManager::open(kDbPath, &err);
    BufferPool bp(disk.get(), 8);
    auto wal = BatchingWAL::open(kWalPath, &err);
    WKV_CHECK(wal != nullptr);
    DB db(&bp, wal.get());

    uint64_t t1 = db.beginTxn();
    WKV_CHECK(db.write(0, t1, "a", "1", 0, &err));
    uint64_t t2 = db.beginTxn();
    WKV_CHECK(db.write(0, t2, "b", "2", 0, &err));
    // No db.close(): simulates a crash before the buffer pool is flushed --
    // only the WAL (already fsynced per-append) survives.
    WKV_CHECK(wal->close(&err));
  }

  {
    auto disk = DiskManager::open(kDbPath, &err);
    BufferPool bp(disk.get(), 8);
    auto wal = BatchingWAL::open(kWalPath, &err);
    DB db(&bp, wal.get());
    WKV_CHECK(recoverDB(kWalPath, &db, &err));

    bool found = false;
    std::string value;
    uint64_t readTxn = db.beginTxn();
    WKV_CHECK(db.read(0, readTxn, "a", &found, &value, &err));
    WKV_CHECK(found);
    WKV_CHECK_EQ(value, std::string("1"));

    found = false;
    WKV_CHECK(db.read(0, readTxn, "b", &found, &value, &err));
    WKV_CHECK(found);
    WKV_CHECK_EQ(value, std::string("2"));
  }

  std::remove(kDbPath);
  std::remove(kWalPath);
}

WKV_TEST(RecoverDBOnMissingFileIsNotAnError) {
  std::remove("test_wal_missing.db");
  std::string err;
  auto disk = DiskManager::open("test_wal_missing.db", &err);
  BufferPool bp(disk.get(), 4);
  auto wal = BatchingWAL::open("test_wal_missing.wal", &err);
  DB db(&bp, wal.get());
  WKV_CHECK(recoverDB("test_wal_missing_does_not_exist.wal", &db, &err));
  std::remove("test_wal_missing.db");
  std::remove("test_wal_missing.wal");
}
