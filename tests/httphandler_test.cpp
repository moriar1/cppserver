#include "httphandler.hpp"
#include <gtest/gtest.h>

TEST(HttpHandlerTest, PathTraversalUnsafe) {
  EXPECT_FALSE(http::is_path_safe("../../../../../etc/passwd"));
}

TEST(HttpHandlerTest, NormalPathSafe) {
  EXPECT_TRUE(http::is_path_safe("./index.html"));
}
