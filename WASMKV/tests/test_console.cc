#include <cstdio>

#include "test_framework.h"
#include "wkv/console.h"

using namespace wkv;

namespace {
const char* kDb = "test_console.db";
const char* kWal = "test_console.wal";

void cleanup() {
  std::remove(kDb);
  std::remove(kWal);
  std::remove((std::string(kDb) + ".idx").c_str());
}
}  // namespace

WKV_TEST(ConsoleDrivesTheFullEngineEndToEnd) {
  cleanup();
  Console console(kDb, kWal, 16);
  std::string result, err;

  WKV_CHECK(console.execute("SET 0 name hello world", &result, &err));
  WKV_CHECK_EQ(result, std::string("OK"));

  WKV_CHECK(console.execute("GET 0 name", &result, &err));
  WKV_CHECK_EQ(result, std::string("hello world"));

  WKV_CHECK(console.execute("SET 0 other value", &result, &err));
  WKV_CHECK(console.execute("SCAN 0 na", &result, &err));
  WKV_CHECK(result.find("name=hello world") != std::string::npos);

  WKV_CHECK(console.execute("DEL 0 name", &result, &err));
  WKV_CHECK_EQ(result, std::string("OK"));
  WKV_CHECK(console.execute("GET 0 name", &result, &err));
  WKV_CHECK_EQ(result, std::string("(nil)"));
  WKV_CHECK(console.execute("SET 0 name hello world", &result, &err));

  WKV_CHECK(console.execute("INDEX 1 the quick brown fox", &result, &err));
  WKV_CHECK(console.execute("INDEX 2 the lazy dog", &result, &err));
  WKV_CHECK(console.execute("SEARCH quick", &result, &err));
  WKV_CHECK_EQ(result, std::string("1"));

  WKV_CHECK(console.execute("HSET 0 h f1 v1", &result, &err));
  WKV_CHECK(console.execute("GET 0 H:h:f1", &result, &err));
  WKV_CHECK_EQ(result, std::string("v1"));

  WKV_CHECK(!console.execute("BOGUS", &result, &err));
  WKV_CHECK(!err.empty());

  WKV_CHECK(console.execute("CLOSE", &result, &err));

  cleanup();
}
