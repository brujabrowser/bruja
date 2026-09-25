#include <iostream>

#include "test_framework.h"

int main() {
  for (auto& [name, fn] : wkvtest::Registry::tests()) {
    fn();
  }
  int failures = wkvtest::Registry::failures();
  if (failures > 0) {
    std::cerr << failures << " check(s) failed\n";
    return 1;
  }
  std::cout << wkvtest::Registry::tests().size() << " test(s) passed\n";
  return 0;
}
