#include <algorithm>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

#include "test_framework.h"
#include "wkv/btree.h"
#include "wkv/buffer_pool.h"
#include "wkv/disk_manager.h"

using namespace wkv;

namespace {
const char* kPath = "test_btree.db";

// Allocates page 0 as an empty leaf root on a fresh file and returns a
// BTree rooted there.
std::unique_ptr<BTree> makeTree(BufferPool* bp, std::string* err) {
  Page* root = bp->newPage(err);
  if (!root) return nullptr;
  root->latch.lock();
  BTreePage node(root);
  node.btreeInit();
  node.setPageType(kPageTypeLeaf);
  root->latch.unlock();
  bp->unpinPage(root->id, true);
  return std::make_unique<BTree>(bp, root->id);
}

}  // namespace

WKV_TEST(InsertAndScanSingleLeaf) {
  std::remove(kPath);
  std::string err;
  auto disk = DiskManager::open(kPath, &err);
  BufferPool bp(disk.get(), 16);
  auto tree = makeTree(&bp, &err);
  WKV_CHECK(tree != nullptr);

  WKV_CHECK(tree->insert("b", "2", &err));
  WKV_CHECK(tree->insert("a", "1", &err));
  WKV_CHECK(tree->insert("c", "3", &err));

  std::vector<std::string> keys, values;
  WKV_CHECK(tree->scan("", &keys, &values, &err));
  WKV_CHECK_EQ(keys.size(), static_cast<size_t>(3));
  if (keys.size() == 3) {
    WKV_CHECK_EQ(keys[0], std::string("a"));
    WKV_CHECK_EQ(keys[1], std::string("b"));
    WKV_CHECK_EQ(keys[2], std::string("c"));
  }

  std::remove(kPath);
}

WKV_TEST(ManyInsertsForceSplitsAndCursorSeesAllKeysSorted) {
  std::remove(kPath);
  std::string err;
  auto disk = DiskManager::open(kPath, &err);
  BufferPool bp(disk.get(), 64);
  auto tree = makeTree(&bp, &err);
  WKV_CHECK(tree != nullptr);

  const int kCount = 500;
  std::vector<std::string> expectedKeys;
  for (int i = 0; i < kCount; ++i) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "k%05d", i);
    expectedKeys.push_back(buf);
  }
  // Insert out of order to exercise mid-leaf insertion and node splitting.
  std::vector<std::string> insertOrder = expectedKeys;
  std::sort(insertOrder.begin(), insertOrder.end(), [](const std::string& a, const std::string& b) {
    return a > b;
  });
  for (auto& k : insertOrder) {
    WKV_CHECK(tree->insert(k, "v-" + k, &err));
  }

  auto cursor = BTreeCursor::create(tree.get(), &err);
  WKV_CHECK(cursor != nullptr);

  std::vector<std::string> seen;
  std::string key, value;
  bool eof = false;
  while (cursor->next(&key, &value, &eof, &err)) {
    seen.push_back(key);
  }
  WKV_CHECK(eof);
  std::sort(expectedKeys.begin(), expectedKeys.end());
  WKV_CHECK_EQ(seen.size(), expectedKeys.size());
  WKV_CHECK(seen == expectedKeys);

  std::remove(kPath);
}

WKV_TEST(SortMergeJoinMatchesCommonKeys) {
  const char* leftPath = "test_btree_left.db";
  const char* rightPath = "test_btree_right.db";
  std::remove(leftPath);
  std::remove(rightPath);
  std::string err;

  auto leftDisk = DiskManager::open(leftPath, &err);
  BufferPool leftBp(leftDisk.get(), 16);
  auto leftTree = makeTree(&leftBp, &err);

  auto rightDisk = DiskManager::open(rightPath, &err);
  BufferPool rightBp(rightDisk.get(), 16);
  auto rightTree = makeTree(&rightBp, &err);

  WKV_CHECK(leftTree != nullptr && rightTree != nullptr);
  for (const std::string k : {"a", "b", "c"}) leftTree->insert(k, "L-" + k, &err);
  for (const std::string k : {"b", "c", "d"}) rightTree->insert(k, "R-" + k, &err);

  std::vector<JoinResult> results;
  WKV_CHECK(sortMergeJoin(leftTree.get(), rightTree.get(), &results, &err));
  WKV_CHECK_EQ(results.size(), static_cast<size_t>(2));
  if (results.size() == 2) {
    WKV_CHECK_EQ(results[0].key, std::string("b"));
    WKV_CHECK_EQ(results[1].key, std::string("c"));
  }

  std::remove(leftPath);
  std::remove(rightPath);
}
