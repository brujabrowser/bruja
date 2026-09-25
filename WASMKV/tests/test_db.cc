#include <cstdio>
#include <map>

#include "test_framework.h"
#include "wkv/buffer_pool.h"
#include "wkv/db.h"
#include "wkv/disk_manager.h"
#include "wkv/wal.h"

using namespace wkv;

namespace {
const char* kDbPath = "test_db.db";
const char* kWalPath = "test_db.wal";

struct Fixture {
  std::unique_ptr<DiskManager> disk;
  std::unique_ptr<BufferPool> bp;
  std::unique_ptr<BatchingWAL> wal;
  std::unique_ptr<DB> db;

  Fixture() {
    std::remove(kDbPath);
    std::remove(kWalPath);
    std::string err;
    disk = DiskManager::open(kDbPath, &err);
    bp = std::make_unique<BufferPool>(disk.get(), 16);
    wal = BatchingWAL::open(kWalPath, &err);
    db = std::make_unique<DB>(bp.get(), wal.get());
  }
  ~Fixture() {
    std::remove(kDbPath);
    std::remove(kWalPath);
  }
};

}  // namespace

WKV_TEST(MvccReadSeesOnlyVersionsAtOrBeforeReadTxn) {
  Fixture f;
  std::string err;

  uint64_t t1 = f.db->beginTxn();
  WKV_CHECK(f.db->write(0, t1, "k", "v1", 0, &err));
  uint64_t t2 = f.db->beginTxn();
  WKV_CHECK(f.db->write(0, t2, "k", "v2", 0, &err));

  bool found = false;
  std::string value;
  WKV_CHECK(f.db->read(0, t1, "k", &found, &value, &err));
  WKV_CHECK(found);
  WKV_CHECK_EQ(value, std::string("v1"));

  found = false;
  WKV_CHECK(f.db->read(0, t2, "k", &found, &value, &err));
  WKV_CHECK(found);
  WKV_CHECK_EQ(value, std::string("v2"));
}

WKV_TEST(ReadMissingKeyReportsNotFoundWithoutError) {
  Fixture f;
  std::string err;
  bool found = true;
  std::string value;
  WKV_CHECK(f.db->read(0, f.db->beginTxn(), "nope", &found, &value, &err));
  WKV_CHECK(!found);
}

WKV_TEST(ExpiredRecordActsAsTombstone) {
  Fixture f;
  std::string err;
  // Write directly with an expiresAt already in the past, bypassing the
  // wall-clock TTL path so the test is deterministic.
  WKV_CHECK(f.db->restoreWrite(1, /*expiresAt=*/1, 0, "k", "v", &err));

  bool found = true;
  std::string value;
  WKV_CHECK(f.db->read(0, f.db->beginTxn(), "k", &found, &value, &err));
  WKV_CHECK(!found);
}

WKV_TEST(ScanReturnsLatestPerKeyFilteredByPrefix) {
  Fixture f;
  std::string err;
  uint64_t t = f.db->beginTxn();
  WKV_CHECK(f.db->write(0, t, "user:1", "alice", 0, &err));
  WKV_CHECK(f.db->write(0, t, "user:2", "bob", 0, &err));
  WKV_CHECK(f.db->write(0, t, "other:1", "carol", 0, &err));
  uint64_t t2 = f.db->beginTxn();
  WKV_CHECK(f.db->write(0, t2, "user:1", "alice2", 0, &err));  // newer version of user:1

  std::map<std::string, std::string> results;
  uint64_t readTxn = f.db->beginTxn();
  WKV_CHECK(f.db->scan(0, readTxn, "user:", [&](const std::string& k, const std::string& v) {
    results[k] = v;
    return true;
  }, &err));

  WKV_CHECK_EQ(results.size(), static_cast<size_t>(2));
  WKV_CHECK_EQ(results["user:1"], std::string("alice2"));
  WKV_CHECK_EQ(results["user:2"], std::string("bob"));
}

WKV_TEST(HSetStoresUnderCompositeKey) {
  Fixture f;
  std::string err;
  uint64_t t = f.db->beginTxn();
  WKV_CHECK(f.db->hset(0, t, "myhash", "field1", "value1", 0, &err));

  bool found = false;
  std::string value;
  WKV_CHECK(f.db->read(0, f.db->beginTxn(), "H:myhash:field1", &found, &value, &err));
  WKV_CHECK(found);
  WKV_CHECK_EQ(value, std::string("value1"));
}

WKV_TEST(CompactionReclaimsSpaceForNewWrites) {
  Fixture f;
  std::string err;
  // Fill most of a page with a large value under one key, rewritten many
  // times so each rewrite requires compaction to fit.
  std::string big(kPageSize / 4, 'x');
  for (int i = 0; i < 8; ++i) {
    uint64_t t = f.db->beginTxn();
    WKV_CHECK(f.db->write(0, t, "big", big, 0, &err));
  }
  bool found = false;
  std::string value;
  WKV_CHECK(f.db->read(0, f.db->beginTxn(), "big", &found, &value, &err));
  WKV_CHECK(found);
  WKV_CHECK_EQ(value.size(), big.size());
}
