#pragma once
// LEB128-style unsigned varint, matching Go's encoding/binary
// PutUvarint/Uvarint (7 bits per byte, MSB = continuation).

#include <cstddef>
#include <cstdint>
#include <vector>

namespace wkv {

// Appends the varint encoding of `v` to `out`.
inline void putUvarint(std::vector<uint8_t>* out, uint64_t v) {
  while (v >= 0x80) {
    out->push_back(static_cast<uint8_t>(v) | 0x80);
    v >>= 7;
  }
  out->push_back(static_cast<uint8_t>(v));
}

// Reads a varint starting at data[*pos], advancing *pos past it. Returns
// false if the buffer is exhausted before a terminating byte is found.
inline bool readUvarint(const uint8_t* data, size_t len, size_t* pos, uint64_t* out) {
  uint64_t result = 0;
  int shift = 0;
  while (*pos < len) {
    uint8_t b = data[(*pos)++];
    result |= static_cast<uint64_t>(b & 0x7F) << shift;
    if ((b & 0x80) == 0) {
      *out = result;
      return true;
    }
    shift += 7;
    if (shift >= 64) return false;
  }
  return false;
}

}  // namespace wkv
