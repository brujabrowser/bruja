#pragma once
// Port of ultimate_db.go section 2 (BufferPool): fixed-size frame pool with
// LRU eviction over DiskManager-backed pages.

#include <list>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "wkv/disk_manager.h"
#include "wkv/page.h"

namespace wkv {

class BufferPool {
 public:
  BufferPool(DiskManager* disk, int poolSize);

  // Fetches (loading from disk if needed) and pins the page. Returns
  // nullptr and sets *err on failure. Caller must call unpinPage when done.
  Page* fetchPage(PageID id, std::string* err);
  void unpinPage(PageID id, bool isDirty);

  // Extends the backing file by one page and returns it pinned, like
  // fetchPage.
  Page* newPage(std::string* err);

  bool flushAll(std::string* err);

 private:
  int getAvailableFrame(std::string* err);  // returns -1 on failure

  DiskManager* disk_;
  std::vector<std::unique_ptr<Page>> frames_;
  std::unordered_map<PageID, std::list<int>::iterator> pageTable_;
  std::list<int> lru_;  // front = most recently used
  std::vector<int> freeList_;
  std::mutex mu_;
};

}  // namespace wkv
