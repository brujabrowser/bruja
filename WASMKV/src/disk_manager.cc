#include "wkv/disk_manager.h"

#include <cerrno>
#include <cstring>

namespace wkv {

std::unique_ptr<DiskManager> DiskManager::open(const std::string& path, std::string* err) {
  FILE* f = std::fopen(path.c_str(), "r+b");
  if (!f) {
    f = std::fopen(path.c_str(), "w+b");  // didn't exist -- create it
  }
  if (!f) {
#if defined(__wasi__)
    (void)err;
    return std::unique_ptr<DiskManager>(new DiskManager(nullptr));
#else
    if (err) *err = std::strerror(errno);
    return nullptr;
#endif
  }
  return std::unique_ptr<DiskManager>(new DiskManager(f));
}

DiskManager::~DiskManager() {
  if (f_) std::fclose(f_);
}

bool DiskManager::readPage(PageID id, uint8_t* data, std::string* err) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!f_) {
    const size_t off = static_cast<size_t>(id) * kPageSize;
    if (off >= mem_.size()) {
      std::memset(data, 0, kPageSize);
      return true;
    }
    size_t n = mem_.size() - off;
    if (n > kPageSize) n = kPageSize;
    std::memcpy(data, mem_.data() + off, n);
    if (n < kPageSize) std::memset(data + n, 0, kPageSize - n);
    return true;
  }
  if (std::fseek(f_, static_cast<long>(id * kPageSize), SEEK_SET) != 0) {
    if (err) *err = std::strerror(errno);
    return false;
  }
  // A short (or empty) read past EOF is fine -- but the tail must be
  // explicitly zeroed here, not assumed to already be zero. That
  // assumption held for a brand-new Page (whose data{} starts
  // zero-initialized) but not for a BufferPool frame being recycled for a
  // different page: it still holds its *previous* occupant's real bytes,
  // including a nonzero freeSpaceOffset that has nothing to do with this
  // page. Leaving that tail untouched let a fresh page inherit a stale
  // offset, which writeRecordToPage's `if (freeOffset == 0) init()` check
  // never catches (it's not zero -- it's just wrong), corrupting the page
  // (silently, since the resulting write can still look successful right
  // up until eviction pressure changes which frame gets reused when, at
  // which point recovery's replay -- with its own different fetch/evict
  // order -- can tip an already-corrupted offset into a genuine "page
  // overflow"). fseek() clears any prior EOF indicator.
  size_t n = std::fread(data, 1, kPageSize, f_);
  if (n < kPageSize) {
    std::memset(data + n, 0, kPageSize - n);
  }
  return true;
}

bool DiskManager::writePage(PageID id, const uint8_t* data, std::string* err) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!f_) {
    const size_t off = static_cast<size_t>(id) * kPageSize;
    if (mem_.size() < off + kPageSize) mem_.resize(off + kPageSize, 0);
    std::memcpy(mem_.data() + off, data, kPageSize);
    return true;
  }
  if (std::fseek(f_, static_cast<long>(id * kPageSize), SEEK_SET) != 0) {
    if (err) *err = std::strerror(errno);
    return false;
  }
  size_t written = std::fwrite(data, 1, kPageSize, f_);
  if (written != kPageSize) {
    if (err) *err = "short write";
    return false;
  }
  std::fflush(f_);
  return true;
}

bool DiskManager::fileSize(int64_t* size, std::string* err) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!f_) {
    *size = static_cast<int64_t>(mem_.size());
    return true;
  }
  long cur = std::ftell(f_);
  if (std::fseek(f_, 0, SEEK_END) != 0) {
    if (err) *err = std::strerror(errno);
    return false;
  }
  long end = std::ftell(f_);
  if (end < 0) {
    if (err) *err = std::strerror(errno);
    return false;
  }
  std::fseek(f_, cur, SEEK_SET);
  *size = static_cast<int64_t>(end);
  return true;
}

}  // namespace wkv
