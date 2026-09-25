#pragma once

#include <chrono>
#include <cstdint>

namespace wkv {

inline int64_t nowNanos() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}

}  // namespace wkv
