#include "config/app_config.h"
#include "http/api_error.h"
#include "http/request_context.h"
#include "http/static_files.h"
#include "util/crypto.h"
#include "util/time.h"

#include <gtest/gtest.h>
#include <json/value.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace {

class TempWorkspace {
  public:
    TempWorkspace()
    {
        const auto base = std::filesystem::temp_directory_path();
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();

        for(int attempt = 0; attempt < 100; ++attempt) {
            auto candidate = base
                / ("blogalone-infra-test-" + std::to_string(ticks) + "-" + std::to_string(attempt));
            if(std::filesystem::create_directory(candidate)) {
                path_ = std::move(candidate);
                return;
            }
        }

        throw std::runtime_error{"unable to create temporary test directory"};
    }

    TempWorkspace(const TempWorkspace&) = delete;
    TempWorkspace& operator=(const TempWorkspace&) = delete;

    ~TempWorkspace()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const
    {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

void write_file(const std::filesystem::path& path, std::string_view content)
{
    std::ofstream file{path, std::ios::binary};
    if(!file) {
        throw std::runtime_error{"unable to write test file"};
    }
    file << content;
}

}

TEST(InfrastructureTest, ParsesApplicationCustomConfig)
{
    Json::Value custom_config;
    custom_config["trusted_proxies"].append("127.0.0.1");
    custom_config["trusted_proxies"].append("10.0.0.1");
    custom_config["uploads_root"] = "var/uploads";
    custom_config["web_root"] = "public";
    custom_config["session_ttl_seconds"] = 60;

    const auto parsed = blogalone::config::app_config_from_json(custom_config);

    ASSERT_EQ(parsed.trusted_proxies.size(), 2);
    EXPECT_EQ(parsed.trusted_proxies.at(0), "127.0.0.1");
    EXPECT_EQ(parsed.uploads_root, std::filesystem::path{"var/uploads"});
    EXPECT_EQ(parsed.web_root, std::filesystem::path{"public"});
    EXPECT_EQ(parsed.session_ttl_seconds, 60);
}

TEST(InfrastructureTest, RejectsInvalidCustomConfig)
{
    Json::Value custom_config;
    custom_config["session_ttl_seconds"] = 0;

    EXPECT_THROW(
        static_cast<void>(blogalone::config::app_config_from_json(custom_config)),
        std::invalid_argument
    );
}

TEST(InfrastructureTest, MapsApiErrorsToWireCodes)
{
    const auto error = blogalone::http::make_api_error(
        blogalone::http::ErrorCode::conflict,
        "duplicate floor number"
    );

    EXPECT_EQ(blogalone::http::to_string(error.code), "conflict");
    EXPECT_EQ(error.status, drogon::k409Conflict);
    EXPECT_EQ(error.message, "duplicate floor number");
}

TEST(InfrastructureTest, ValidatesRequestIds)
{
    EXPECT_TRUE(blogalone::http::is_valid_request_id("req_0123456789abcdef"));
    EXPECT_FALSE(blogalone::http::is_valid_request_id("short"));
    EXPECT_FALSE(blogalone::http::is_valid_request_id("req_has space"));
    EXPECT_FALSE(blogalone::http::is_valid_request_id("req_has/slash"));
}

TEST(InfrastructureTest, GeneratesHexTokensAndHashes)
{
    const auto token = blogalone::util::random_token_hex(16);

    EXPECT_EQ(token.size(), 32);
    EXPECT_TRUE(blogalone::util::is_lower_hex(token));
    EXPECT_EQ(
        blogalone::util::sha256_hex("abc"),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    );
}

TEST(InfrastructureTest, ReturnsCurrentUtcSeconds)
{
    const auto before = std::chrono::system_clock::now();
    const auto value = blogalone::util::utc_unix_seconds();
    const auto after = std::chrono::system_clock::now();

    const auto before_seconds = std::chrono::duration_cast<std::chrono::seconds>(
        before.time_since_epoch()
    ).count();
    const auto after_seconds = std::chrono::duration_cast<std::chrono::seconds>(
        after.time_since_epoch()
    ).count();

    EXPECT_GE(value, before_seconds);
    EXPECT_LE(value, after_seconds);
}

TEST(InfrastructureTest, ResolvesOnlySafeExistingResources)
{
    TempWorkspace workspace;
    const auto static_dir = workspace.path() / "static";
    std::filesystem::create_directory(static_dir);
    write_file(static_dir / "app.js", "console.log('ok');");

    const auto resolved = blogalone::http::resolve_existing_resource(static_dir, "app.js");
    const auto traversal = blogalone::http::resolve_existing_resource(static_dir, "../secret.txt");
    const auto missing = blogalone::http::resolve_existing_resource(static_dir, "missing.css");

    ASSERT_TRUE(resolved.has_value());
    EXPECT_EQ(resolved->filename(), "app.js");
    EXPECT_FALSE(traversal.has_value());
    EXPECT_FALSE(missing.has_value());
}

TEST(InfrastructureTest, UsesStrictMimeTypesForStaticAndUploads)
{
    EXPECT_EQ(blogalone::http::mime_type_for_static_file("app.css"), "text/css; charset=utf-8");
    EXPECT_EQ(blogalone::http::mime_type_for_static_file("app.js"), "application/javascript; charset=utf-8");
    EXPECT_EQ(blogalone::http::mime_type_for_upload_file("photo.webp"), "image/webp");
    EXPECT_EQ(blogalone::http::mime_type_for_upload_file("payload.exe"), "application/octet-stream");
}
