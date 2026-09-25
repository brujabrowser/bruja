#include <cstdio>

#include "test_framework.h"
#include "wkv/index.h"
#include "wkv/query.h"

using namespace wkv;

WKV_TEST(ParseQueryReportsMissingParen) {
  std::string err;
  auto q = parseQuery("(a AND b", &err);
  WKV_CHECK(q == nullptr);
  WKV_CHECK(!err.empty());
}

WKV_TEST(ParseQueryHandlesParenthesesAndPrecedence) {
  MemIndex idx;
  idx.add(1, "a");
  idx.add(2, "b");
  idx.add(1, "c");  // second Add() call for doc 1 -> doc1 has both a and c
  idx.add(3, "d");

  std::string err;
  const char* path = "test_query.seg";
  std::remove(path);
  WKV_CHECK(idx.writeSegment(path, &err));

  FILE* f = std::fopen(path, "rb");
  std::fseek(f, 0, SEEK_END);
  long size = std::ftell(f);
  std::fseek(f, 0, SEEK_SET);
  std::vector<uint8_t> data(static_cast<size_t>(size));
  if (!data.empty()) std::fread(data.data(), 1, data.size(), f);
  std::fclose(f);

  SegmentSearcher searcher(std::move(data));
  std::vector<uint64_t> out;
  // (a OR b) AND c: a={1}, b={2} -> {1,2}; AND c={1} -> {1}
  WKV_CHECK(searcher.search("(a OR b) AND c", &out, &err));
  WKV_CHECK((out == std::vector<uint64_t>{1}));

  std::remove(path);
}
