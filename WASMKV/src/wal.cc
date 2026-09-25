#include "wkv/wal.h"

#include <cerrno>
#include <cstring>
#include <vector>

#include "wkv/crc32.h"

#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif

namespace wkv {
namespace {

bool syncFile(FILE* f) {
#if defined(_WIN32)
  return _commit(_fileno(f)) == 0;
#else
  return ::fsync(fileno(f)) == 0;
#endif
}

}  // namespace

std::unique_ptr<BatchingWAL> BatchingWAL::open(const std::string& path, std::string* err) {
  FILE* f = std::fopen(path.c_str(), "ab");
  if (!f) {
#if defined(__wasi__)
    (void)err;
    return std::unique_ptr<BatchingWAL>(new BatchingWAL(nullptr));
#else
    if (err) *err = std::strerror(errno);
    return nullptr;
#endif
  }
  return std::unique_ptr<BatchingWAL>(new BatchingWAL(f));
}

BatchingWAL::~BatchingWAL() {
  if (f_) std::fclose(f_);
}

bool BatchingWAL::append(uint64_t txnId, int64_t expiresAt, PageID pageId, const std::string& key,
                          const std::string& value, std::string* err) {
  std::lock_guard<std::mutex> lock(mu_);

  // 36-byte header: CRC(4) + TxnID(8) + ExpiresAt(8) + PageID(8) + KeyLen(4) + ValLen(4)
  uint8_t header[36];
  storeLE64(header + 4, txnId);
  storeLE64(header + 12, static_cast<uint64_t>(expiresAt));
  storeLE64(header + 20, static_cast<uint64_t>(pageId));
  storeLE32(header + 28, static_cast<uint32_t>(key.size()));
  storeLE32(header + 32, static_cast<uint32_t>(value.size()));

  uint32_t crc = crc32Begin();
  crc = crc32Append(crc, header + 4, 32);
  crc = crc32Append(crc, reinterpret_cast<const uint8_t*>(key.data()), key.size());
  crc = crc32Append(crc, reinterpret_cast<const uint8_t*>(value.data()), value.size());
  storeLE32(header + 0, crc32End(crc));

  std::vector<uint8_t> buf;
  buf.reserve(36 + key.size() + value.size());
  buf.insert(buf.end(), header, header + 36);
  buf.insert(buf.end(), key.begin(), key.end());
  buf.insert(buf.end(), value.begin(), value.end());

  if (!f_) {
    mem_.insert(mem_.end(), buf.begin(), buf.end());
    return true;
  }
  if (std::fwrite(buf.data(), 1, buf.size(), f_) != buf.size()) {
    if (err) *err = "short write";
    return false;
  }
  if (std::fflush(f_) != 0 || !syncFile(f_)) {
    if (err) *err = std::strerror(errno);
    return false;
  }
  return true;
}

bool BatchingWAL::close(std::string* err) {
  std::lock_guard<std::mutex> lock(mu_);
  if (!f_) return true;
  FILE* f = f_;
  f_ = nullptr;
  if (std::fclose(f) != 0) {
    if (err) *err = std::strerror(errno);
    return false;
  }
  return true;
}

}  // namespace wkv
