#include "abi.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {

__attribute__((export_name("alloc"))) uint32_t wkv_wasi_alloc(uint32_t size) {
  uint32_t n = size == 0 ? 1 : size;  // keep the pointer non-zero/distinguishable from a null result
  void* p = std::malloc(n);
  return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p));
}

__attribute__((export_name("free"))) void wkv_wasi_free(uint32_t ptr, uint32_t /*size*/) {
  std::free(reinterpret_cast<void*>(static_cast<uintptr_t>(ptr)));
}

}  // extern "C"

namespace wkv::wasi {
namespace {

void appendJsonEscaped(std::string* out, const std::string& s) {
  out->push_back('"');
  for (unsigned char c : s) {
    switch (c) {
      case '"':
        *out += "\\\"";
        break;
      case '\\':
        *out += "\\\\";
        break;
      case '\n':
        *out += "\\n";
        break;
      case '\r':
        *out += "\\r";
        break;
      case '\t':
        *out += "\\t";
        break;
      default:
        if (c < 0x20) {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", c);
          *out += buf;
        } else {
          out->push_back(static_cast<char>(c));
        }
    }
  }
  out->push_back('"');
}

}  // namespace

std::string readString(uint32_t ptr, uint32_t len) {
  if (len == 0) return {};
  const char* p = reinterpret_cast<const char*>(static_cast<uintptr_t>(ptr));
  return std::string(p, len);
}

uint64_t packBytes(const std::string& data) {
  uint32_t ptr = wkv_wasi_alloc(static_cast<uint32_t>(data.size()));
  if (!data.empty()) {
    std::memcpy(reinterpret_cast<void*>(static_cast<uintptr_t>(ptr)), data.data(), data.size());
  }
  return (static_cast<uint64_t>(ptr) << 32) | static_cast<uint64_t>(data.size());
}

uint64_t packEnvelope(const std::string& result, bool ok, const std::string& error) {
  std::string json = "{\"ok\":";
  json += ok ? "true" : "false";
  if (ok) {
    json += ",\"result\":";
    appendJsonEscaped(&json, result);
  } else {
    json += ",\"error\":";
    appendJsonEscaped(&json, error);
  }
  json += "}";
  return packBytes(json);
}

}  // namespace wkv::wasi
