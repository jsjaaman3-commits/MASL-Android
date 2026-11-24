#pragma once

#include <string_view>

namespace masl::core {

struct Version final {
  unsigned int major;
  unsigned int minor;
  unsigned int patch;
};

[[nodiscard]] Version version() noexcept;
[[nodiscard]] std::string_view version_string() noexcept;

}  // namespace masl::core
