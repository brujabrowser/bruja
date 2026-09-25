#pragma once
// Port of ultimate_db.go section 1-2 (constants + Page). Fixed-size disk
// page loaded into a buffer pool frame. Little-endian on-disk layout is
// preserved exactly so tooling written against the original Go format
// still parses these bytes.

#include <array>
#include <atomic>
#include <cstdint>
#include <shared_mutex>

namespace wkv {

constexpr uint32_t kPageSize = 32768;
constexpr uint32_t kPageHeaderSize = 8;    // 4 bytes FreeSpaceOffset + 4 bytes reserved
constexpr uint32_t kRecordHeaderSize = 24; // txnID(8) + expiresAt(8) + keyLen(4) + valLen(4)

using PageID = uint64_t;

inline uint16_t loadLE16(const uint8_t* p) {
  return static_cast<uint16_t>(p[0]) | (static_cast<uint16_t>(p[1]) << 8);
}
inline void storeLE16(uint8_t* p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}
inline uint32_t loadLE32(const uint8_t* p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
         (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline void storeLE32(uint8_t* p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}
inline uint64_t loadLE64(const uint8_t* p) {
  uint64_t v = 0;
  for (int i = 7; i >= 0; --i) v = (v << 8) | p[i];
  return v;
}
inline void storeLE64(uint8_t* p, uint64_t v) {
  for (int i = 0; i < 8; ++i) {
    p[i] = static_cast<uint8_t>(v);
    v >>= 8;
  }
}

struct Page {
  PageID id = 0;
  std::array<uint8_t, kPageSize> data{};
  std::atomic<int32_t> pinCount{0};
  bool isDirty = false;
  std::shared_mutex latch;

  void init() {
    storeLE32(data.data() + 0, kPageHeaderSize);
    storeLE32(data.data() + 4, 0);
  }
  uint32_t freeSpaceOffset() const { return loadLE32(data.data() + 0); }
  void setFreeSpaceOffset(uint32_t offset) { storeLE32(data.data() + 0, offset); }
};

}  // namespace wkv
