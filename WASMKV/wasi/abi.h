#pragma once
// WASI reactor ABI plumbing: guest exports memory, alloc(size:u32)->u32,
// free(ptr:u32,size:u32)->(), and every business export is
// (ptr,len)->i64 packing a JSON envelope {"ok":bool,"result"|"error":string}
// -- same wire convention as ../../guikit/cpp/wasi/abi.h. No GC here either,
// so alloc is a plain malloc, stable until free().

#include <cstdint>
#include <string>

namespace wkv::wasi {

std::string readString(uint32_t ptr, uint32_t len);

// Copies `data` into a freshly alloc'd buffer and returns the packed
// (ptr<<32|len) result every export produces.
uint64_t packBytes(const std::string& data);

// Builds {"ok":...,"result"|"error":...} and packs it, as above.
uint64_t packEnvelope(const std::string& result, bool ok, const std::string& error);

}  // namespace wkv::wasi
