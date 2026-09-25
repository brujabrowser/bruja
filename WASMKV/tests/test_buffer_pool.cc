#include <cstdio>
#include <cstring>

#include "test_framework.h"
#include "wkv/buffer_pool.h"
#include "wkv/disk_manager.h"

using namespace wkv;

namespace {
const char* kPath = "test_buffer_pool.db";
}

WKV_TEST(EvictionFlushesDirtyPageToDisk) {
  std::remove(kPath);
  std::string err;

  auto disk = DiskManager::open(kPath, &err);
  WKV_CHECK(disk != nullptr);
  BufferPool bp(disk.get(), /*poolSize=*/1);

  Page* p0 = bp.fetchPage(0, &err);
  WKV_CHECK(p0 != nullptr);
  std::memcpy(p0->data.data(), "hello", 5);
  bp.unpinPage(0, /*isDirty=*/true);

  // Pool size 1: fetching a second page must evict page 0, flushing it.
  Page* p1 = bp.fetchPage(1, &err);
  WKV_CHECK(p1 != nullptr);
  bp.unpinPage(1, false);

  Page* p0Again = bp.fetchPage(0, &err);
  WKV_CHECK(p0Again != nullptr);
  WKV_CHECK(std::memcmp(p0Again->data.data(), "hello", 5) == 0);
  bp.unpinPage(0, false);

  std::remove(kPath);
}

WKV_TEST(BufferPoolExhaustedWhenAllFramesPinned) {
  std::remove(kPath);
  std::string err;

  auto disk = DiskManager::open(kPath, &err);
  BufferPool bp(disk.get(), /*poolSize=*/1);

  Page* p0 = bp.fetchPage(0, &err);
  WKV_CHECK(p0 != nullptr);
  // p0 stays pinned -- the pool has no other frame to give out.
  Page* p1 = bp.fetchPage(1, &err);
  WKV_CHECK(p1 == nullptr);
  WKV_CHECK(err == "buffer pool exhausted");

  bp.unpinPage(0, false);
  std::remove(kPath);
}

WKV_TEST(FlushAllPersistsAcrossReopen) {
  std::remove(kPath);
  std::string err;

  {
    auto disk = DiskManager::open(kPath, &err);
    BufferPool bp(disk.get(), 4);
    Page* p = bp.fetchPage(2, &err);
    WKV_CHECK(p != nullptr);
    std::memcpy(p->data.data(), "persisted", 9);
    bp.unpinPage(2, true);
    WKV_CHECK(bp.flushAll(&err));
  }

  {
    auto disk = DiskManager::open(kPath, &err);
    BufferPool bp(disk.get(), 4);
    Page* p = bp.fetchPage(2, &err);
    WKV_CHECK(p != nullptr);
    WKV_CHECK(std::memcmp(p->data.data(), "persisted", 9) == 0);
    bp.unpinPage(2, false);
  }

  std::remove(kPath);
}
