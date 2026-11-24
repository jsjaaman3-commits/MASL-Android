#include "masl/version.hpp"

#ifndef MASL_VERSION_MAJOR
#define MASL_VERSION_MAJOR 0
#endif
#ifndef MASL_VERSION_MINOR
#define MASL_VERSION_MINOR 1
#endif
#ifndef MASL_VERSION_PATCH
#define MASL_VERSION_PATCH 0
#endif

#include <string>

namespace masl::core {
namespace {

constexpr Version kVersion{
    static_cast<unsigned int>(MASL_VERSION_MAJOR),
    static_cast<unsigned int>(MASL_VERSION_MINOR),
    static_cast<unsigned int>(MASL_VERSION_PATCH)};

const std::string kVersionString =
    std::to_string(kVersion.major) + "." + std::to_string(kVersion.minor) +
    "." + std::to_string(kVersion.patch);

}  // namespace

Version version() noexcept { return kVersion; }

std::string_view version_string() noexcept { return kVersionString; }

}  // namespace masl::core
