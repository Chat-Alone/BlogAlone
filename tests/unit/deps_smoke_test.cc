#include <gtest/gtest.h>
#include <smoke/dependency_checks.h>

TEST(DependencySmokeTest, CoreDependenciesAreCallable)
{
    EXPECT_TRUE(blogalone::smoke::sqlite_is_available());
    EXPECT_TRUE(blogalone::smoke::sodium_is_available());
    EXPECT_TRUE(blogalone::smoke::cmark_gfm_is_available());
    EXPECT_TRUE(blogalone::smoke::drogon_is_available());
    EXPECT_TRUE(blogalone::smoke::spdlog_is_available());
}
