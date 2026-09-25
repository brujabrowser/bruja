#pragma once
// Standard CRC-32 (IEEE 802.3 polynomial), matching Go's hash/crc32 default
// (crc32.NewIEEE / crc32.ChecksumIEEE) so WAL segments stay bit-compatible.
//
// Usage for a multi-part checksum (mirrors hash.Hash.Write called
// repeatedly before Sum32()):
//   uint32_t s = crc32Begin();
//   s = crc32Append(s, part1, part1Len);
//   s = crc32Append(s, part2, part2Len);
//   uint32_t sum = crc32End(s);

#include <cstddef>
#include <cstdint>

namespace wkv {

constexpr uint32_t kCrc32InitialState = 0xFFFFFFFFu;

inline uint32_t crc32Begin() { return kCrc32InitialState; }
uint32_t crc32Append(uint32_t state, const uint8_t* data, size_t len);
inline uint32_t crc32End(uint32_t state) { return state ^ kCrc32InitialState; }

inline uint32_t crc32(const uint8_t* data, size_t len) {
  return crc32End(crc32Append(crc32Begin(), data, len));
}

}  // namespace wkv
