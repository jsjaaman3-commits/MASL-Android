#include "masl/version.hpp"

#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

int failures = 0;

void check(bool condition, std::string_view name) {
  if (condition) {
    std::cout << "[PASS] " << name << '\n';
  } else {
    std::cerr << "[FAIL] " << name << '\n';
    ++failures;
  }
}

}  // namespace

int main() {
  const masl::core::Version current = masl::core::version();
  check(current.major == 0U, "version major is 0");
  check(current.minor == 10U, "version minor is 10");
  check(current.patch == 0U, "version patch is 0");
  check(masl::core::version_string() == "0.10.0", "version string is canonical");

  if (failures != 0) {
    std::cerr << failures << " foundation test(s) failed.\n";
    return EXIT_FAILURE;
  }
  std::cout << "All foundation tests passed.\n";
  return EXIT_SUCCESS;
}
