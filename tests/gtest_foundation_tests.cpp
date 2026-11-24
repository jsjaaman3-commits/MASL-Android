#include "masl/version.hpp"

#include <gtest/gtest.h>

TEST(MaslFoundation, VersionFieldsAreCorrect) {
  const auto current = masl::core::version();
  EXPECT_EQ(current.major, 0U);
  EXPECT_EQ(current.minor, 1U);
  EXPECT_EQ(current.patch, 0U);
}

TEST(MaslFoundation, VersionStringIsCanonical) {
  EXPECT_EQ(masl::core::version_string(), "0.3.0");
}
