#include <cstdio>

#include "test_framework.h"
#include "wkv/index.h"

using namespace wkv;

namespace {

const char* kSegPath = "test_index.seg";

std::vector<uint8_t> readFile(const char* path) {
  FILE* f = std::fopen(path, "rb");
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> data(static_cast<size_t>(size));
  if (!data.empty()) std::fread(data.data(), 1, data.size(), f);
  std::fclose(f);
  return data;
}

}  // namespace

WKV_TEST(TokenizeLowercasesAndSplitsOnNonAlnum) {
  auto toks = tokenize("Hello, World! 123");
  WKV_CHECK_EQ(toks.size(), static_cast<size_t>(3));
  if (toks.size() == 3) {
    WKV_CHECK_EQ(toks[0], std::string("hello"));
    WKV_CHECK_EQ(toks[1], std::string("world"));
    WKV_CHECK_EQ(toks[2], std::string("123"));
  }
}

WKV_TEST(SegmentRoundTripFetchesPostings) {
  std::remove(kSegPath);
  MemIndex idx;
  idx.add(1, "the quick brown fox");
  idx.add(2, "the lazy dog");
  idx.add(3, "quick fox jumps");

  std::string err;
  WKV_CHECK(idx.writeSegment(kSegPath, &err));

  SegmentSearcher searcher(readFile(kSegPath));
  auto quickPostings = searcher.fetchPostings("quick");
  WKV_CHECK((quickPostings == std::vector<uint64_t>{1, 3}));

  auto missing = searcher.fetchPostings("nonexistent");
  WKV_CHECK(missing.empty());

  std::remove(kSegPath);
}

WKV_TEST(BooleanSearchAndOrNot) {
  std::remove(kSegPath);
  MemIndex idx;
  idx.add(1, "apple banana");
  idx.add(2, "banana cherry");
  idx.add(3, "apple cherry");
  idx.add(4, "date");

  std::string err;
  WKV_CHECK(idx.writeSegment(kSegPath, &err));
  SegmentSearcher searcher(readFile(kSegPath));

  std::vector<uint64_t> out;
  WKV_CHECK(searcher.search("apple AND banana", &out, &err));
  WKV_CHECK((out == std::vector<uint64_t>{1}));

  WKV_CHECK(searcher.search("apple OR date", &out, &err));
  WKV_CHECK((out == std::vector<uint64_t>{1, 3, 4}));

  WKV_CHECK(searcher.search("cherry NOT apple", &out, &err));
  WKV_CHECK((out == std::vector<uint64_t>{2}));

  std::remove(kSegPath);
}
