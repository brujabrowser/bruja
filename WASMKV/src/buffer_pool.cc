#include "wkv/buffer_pool.h"

namespace wkv {

BufferPool::BufferPool(DiskManager* disk, int poolSize) : disk_(disk) {
  frames_.reserve(static_cast<size_t>(poolSize));
  freeList_.reserve(static_cast<size_t>(poolSize));
  for (int i = 0; i < poolSize; ++i) {
    frames_.push_back(std::make_unique<Page>());
    freeList_.push_back(i);
  }
}

Page* BufferPool::fetchPage(PageID id, std::string* err) {
  std::lock_guard<std::mutex> lock(mu_);

  auto it = pageTable_.find(id);
  if (it != pageTable_.end()) {
    lru_.splice(lru_.begin(), lru_, it->second);  // move to front
    Page* page = frames_[static_cast<size_t>(*it->second)].get();
    page->pinCount.fetch_add(1);
    return page;
  }

  int frameIdx = getAvailableFrame(err);
  if (frameIdx < 0) return nullptr;

  Page* page = frames_[static_cast<size_t>(frameIdx)].get();
  page->id = id;
  page->pinCount.store(1);
  page->isDirty = false;

  if (!disk_->readPage(id, page->data.data(), err)) return nullptr;

  lru_.push_front(frameIdx);
  pageTable_[id] = lru_.begin();
  return page;
}

void BufferPool::unpinPage(PageID id, bool isDirty) {
  std::lock_guard<std::mutex> lock(mu_);
  auto it = pageTable_.find(id);
  if (it == pageTable_.end()) return;
  Page* page = frames_[static_cast<size_t>(*it->second)].get();
  page->pinCount.fetch_sub(1);
  if (isDirty) page->isDirty = true;
}

int BufferPool::getAvailableFrame(std::string* err) {
  if (!freeList_.empty()) {
    int idx = freeList_.front();
    freeList_.erase(freeList_.begin());
    return idx;
  }

  for (auto it = lru_.rbegin(); it != lru_.rend(); ++it) {
    int frameIdx = *it;
    Page* page = frames_[static_cast<size_t>(frameIdx)].get();

    if (page->pinCount.load() == 0) {
      if (page->isDirty) {
        if (!disk_->writePage(page->id, page->data.data(), err)) return -1;
      }
      pageTable_.erase(page->id);
      lru_.erase(std::next(it).base());
      return frameIdx;
    }
  }

  if (err) *err = "buffer pool exhausted";
  return -1;
}

Page* BufferPool::newPage(std::string* err) {
  std::unique_lock<std::mutex> lock(mu_);
  int64_t size = 0;
  if (!disk_->fileSize(&size, err)) return nullptr;
  PageID newId = static_cast<PageID>(size) / kPageSize;
  std::array<uint8_t, kPageSize> empty{};
  if (!disk_->writePage(newId, empty.data(), err)) return nullptr;
  lock.unlock();
  return fetchPage(newId, err);
}

bool BufferPool::flushAll(std::string* err) {
  std::lock_guard<std::mutex> lock(mu_);
  for (auto& page : frames_) {
    if (page->isDirty) {
      if (!disk_->writePage(page->id, page->data.data(), err)) return false;
      page->isDirty = false;
    }
  }
  return true;
}

}  // namespace wkv
