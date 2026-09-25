#pragma once
#include <functional>
#include <iostream>
#include <string>
#include <vector>

namespace wkvtest {

struct Registry {
  static std::vector<std::pair<std::string, std::function<void()>>>& tests() {
    static std::vector<std::pair<std::string, std::function<void()>>> t;
    return t;
  }
  static int& failures() {
    static int f = 0;
    return f;
  }
};

struct Registrar {
  Registrar(const std::string& name, std::function<void()> fn) {
    Registry::tests().emplace_back(name, std::move(fn));
  }
};

}  // namespace wkvtest

#define WKV_TEST(name)                                                     \
  static void wkvtest_##name();                                           \
  static wkvtest::Registrar wkvtest_reg_##name(#name, wkvtest_##name);     \
  static void wkvtest_##name()

#define WKV_CHECK(cond)                                                                    \
  do {                                                                                     \
    if (!(cond)) {                                                                         \
      std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #cond "\n";              \
      ++wkvtest::Registry::failures();                                                     \
    }                                                                                       \
  } while (0)

#define WKV_CHECK_EQ(a, b)                                                                  \
  do {                                                                                      \
    auto _a = (a);                                                                          \
    auto _b = (b);                                                                          \
    if (!(_a == _b)) {                                                                      \
      std::cerr << "FAIL " << __FILE__ << ":" << __LINE__ << ": " #a " == " #b              \
                 << "\n  got:      " << _a << "\n  expected: " << _b << "\n";                \
      ++wkvtest::Registry::failures();                                                      \
    }                                                                                        \
  } while (0)
