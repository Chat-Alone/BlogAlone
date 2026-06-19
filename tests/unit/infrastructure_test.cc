#include "config/app_config.h"
#include "db/transaction.h"
#include "http/api_error.h"
#include "http/client_ip.h"
#include "http/request_context.h"
#include "http/static_files.h"
#include "security/rate_limiter.h"
#include "util/crypto.h"
#include "util/time.h"

#include <drogon/drogon.h>
#include <gtest/gtest.h>
#include <json/reader.h>
#include <json/value.h>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path)
{
    std::ifstream file{path, std::ios::binary};
    if(!file) {
        throw std::runtime_error{"unable to read test file"};
    }
    return {
        std::istreambuf_iterator<char>{file},
        std::istreambuf_iterator<char>{}
    };
}

[[nodiscard]] Json::Value read_json_file(const std::filesystem::path& path)
{
    std::ifstream file{path, std::ios::binary};
    if(!file) {
        throw std::runtime_error{"unable to read json file"};
    }

    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if(!Json::parseFromStream(builder, file, &root, &errors)) {
        throw std::runtime_error{"unable to parse json file: " + errors};
    }
    return root;
}

}

TEST(InfrastructureTest, ParsesApplicationCustomConfig)
{
    Json::Value custom_config;
    custom_config["trusted_proxies"].append("127.0.0.1");
    custom_config["trusted_proxies"].append("2001:0db8::1");
    custom_config["uploads_root"] = "var/uploads";
    custom_config["web_root"] = "public";
    custom_config["session_ttl_seconds"] = 60;
    custom_config["upload_max_file_size"] = 1'048'576;
    custom_config["upload_max_daily_uploads"] = 7;
    custom_config["upload_max_dimension"] = 1024;
    custom_config["rate_limit_registration_max_requests"] = 3;
    custom_config["rate_limit_registration_window_seconds"] = 600;
    custom_config["rate_limit_login_max_requests"] = 4;
    custom_config["rate_limit_login_window_seconds"] = 120;
    custom_config["rate_limit_upload_max_requests"] = 8;
    custom_config["rate_limit_upload_window_seconds"] = 30;
    custom_config["rate_limit_post_max_requests"] = 9;
    custom_config["rate_limit_post_window_seconds"] = 45;
    custom_config["orphan_upload_retention_seconds"] = 7200;
    custom_config["upload_cleanup_interval_seconds"] = 300;
    custom_config["password_opslimit"] = 2;
    custom_config["password_memlimit"] = 67'108'864;

    const auto parsed = blogalone::config::app_config_from_json(custom_config);

    ASSERT_EQ(parsed.trusted_proxies.size(), 2);
    EXPECT_EQ(parsed.trusted_proxies.at(0), "127.0.0.1");
    EXPECT_EQ(parsed.trusted_proxies.at(1), "2001:db8::1");
    EXPECT_EQ(parsed.uploads_root, std::filesystem::path{"var/uploads"});
    EXPECT_EQ(parsed.web_root, std::filesystem::path{"public"});
    EXPECT_EQ(parsed.session_ttl_seconds, 60);
    EXPECT_EQ(parsed.upload.max_file_size, 1'048'576);
    EXPECT_EQ(parsed.upload.max_daily_uploads, 7);
    EXPECT_EQ(parsed.upload.max_dimension, 1024);
    EXPECT_EQ(parsed.rate_limits.registration.max_requests, 3);
    EXPECT_EQ(parsed.rate_limits.registration.window, std::chrono::seconds{600});
    EXPECT_EQ(parsed.rate_limits.login.max_requests, 4);
    EXPECT_EQ(parsed.rate_limits.login.window, std::chrono::seconds{120});
    EXPECT_EQ(parsed.rate_limits.upload.max_requests, 8);
    EXPECT_EQ(parsed.rate_limits.upload.window, std::chrono::seconds{30});
    EXPECT_EQ(parsed.rate_limits.post.max_requests, 9);
    EXPECT_EQ(parsed.rate_limits.post.window, std::chrono::seconds{45});
    EXPECT_EQ(parsed.upload_cleanup.retention_seconds, 7200);
    EXPECT_EQ(parsed.upload_cleanup.interval_seconds, 300);
    EXPECT_EQ(parsed.password_hash_options.opslimit, 2);
    EXPECT_EQ(parsed.password_hash_options.memlimit, 67'108'864);
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

TEST(InfrastructureTest, RejectsInvalidTrustedProxy)
{
    Json::Value custom_config;
    custom_config["trusted_proxies"].append("not-an-ip");

    EXPECT_THROW(
        static_cast<void>(blogalone::config::app_config_from_json(custom_config)),
        std::invalid_argument
    );
}

TEST(InfrastructureTest, RejectsUploadDimensionAboveDecodedLimit)
{
    Json::Value custom_config;
    custom_config["upload_max_dimension"] = static_cast<Json::Int64>(
        blogalone::util::kMaxDecodedImageDimension + 1
    );

    EXPECT_THROW(
        static_cast<void>(blogalone::config::app_config_from_json(custom_config)),
        std::invalid_argument
    );
}

TEST(InfrastructureTest, KeepsDrogonSqliteClientSingleConnection)
{
    const auto config_dir = std::filesystem::path{BLOGALONE_SOURCE_DIR} / "config";
    for(const auto filename : {"config.windows.json", "config.linux.json"}) {
        const auto config = read_json_file(config_dir / filename);
        const auto& clients = config["db_clients"];
        ASSERT_TRUE(clients.isArray());

        bool checked_sqlite_client = false;
        for(const auto& client : clients) {
            if(client["rdbms"].asString() != "sqlite3") {
                continue;
            }
            checked_sqlite_client = true;
            ASSERT_TRUE(client["number_of_connections"].isInt());
            EXPECT_EQ(client["number_of_connections"].asInt(), 1) << filename;
        }
        EXPECT_TRUE(checked_sqlite_client) << filename;
    }
}

TEST(InfrastructureTest, CommitsAndRollsBackDrogonTransactions)
{
    TempWorkspace workspace;
    const auto client = drogon::orm::DbClient::newSqlite3Client(
        "filename=" + (workspace.path() / "transactions.db").generic_string(),
        1
    );
    client->execSqlSync("CREATE TABLE values_log (value INTEGER NOT NULL)");

    {
        blogalone::db::Transaction transaction{
            client,
            drogon::orm::TransactionType::Immediate
        };
        transaction.client()->execSqlSync("INSERT INTO values_log (value) VALUES (1)");
        transaction.commit();
    }
    {
        blogalone::db::Transaction transaction{
            client,
            drogon::orm::TransactionType::Immediate
        };
        transaction.client()->execSqlSync("INSERT INTO values_log (value) VALUES (2)");
    }

    const auto rows = client->execSqlSync("SELECT COUNT(*) AS total FROM values_log");
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows.at(0)["total"].as<std::int64_t>(), 1);
}

TEST(InfrastructureTest, LocksSqlMigrationLineEndings)
{
    const auto attributes = read_text_file(
        std::filesystem::path{BLOGALONE_SOURCE_DIR} / ".gitattributes"
    );

    EXPECT_NE(attributes.find("*.sql text eol=lf"), std::string::npos);
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

TEST(InfrastructureTest, TrustsForwardedForOnlyFromConfiguredProxy)
{
    const std::vector<std::string> trusted_proxies{"127.0.0.1", "10.0.0.2"};

    EXPECT_EQ(
        blogalone::http::resolve_client_ip(
            "127.0.0.1",
            "198.51.100.9, 10.0.0.2",
            trusted_proxies
        ),
        "198.51.100.9"
    );
    EXPECT_EQ(
        blogalone::http::resolve_client_ip(
            "203.0.113.7",
            "198.51.100.9",
            trusted_proxies
        ),
        "203.0.113.7"
    );
}

TEST(InfrastructureTest, RejectsMalformedForwardedForChain)
{
    const std::vector<std::string> trusted_proxies{"127.0.0.1"};

    EXPECT_EQ(
        blogalone::http::resolve_client_ip(
            "127.0.0.1",
            "198.51.100.9, not-an-ip",
            trusted_proxies
        ),
        "127.0.0.1"
    );
}

TEST(InfrastructureTest, EnforcesSlidingWindowAcrossIpAndUserKeys)
{
    blogalone::security::RequestRateLimiter limiter;
    const blogalone::security::RateLimitPolicy policy{
        .max_requests = 2,
        .window = std::chrono::seconds{60}
    };
    const auto started_at = blogalone::security::RequestRateLimiter::TimePoint{};

    EXPECT_TRUE(limiter.consume(
        blogalone::security::RateLimitScope::post,
        "198.51.100.1",
        42,
        policy,
        started_at
    ));
    EXPECT_TRUE(limiter.consume(
        blogalone::security::RateLimitScope::post,
        "198.51.100.1",
        42,
        policy,
        started_at + std::chrono::seconds{1}
    ));
    EXPECT_FALSE(limiter.consume(
        blogalone::security::RateLimitScope::post,
        "198.51.100.2",
        42,
        policy,
        started_at + std::chrono::seconds{2}
    ));
    EXPECT_FALSE(limiter.consume(
        blogalone::security::RateLimitScope::post,
        "198.51.100.1",
        43,
        policy,
        started_at + std::chrono::seconds{2}
    ));
    EXPECT_TRUE(limiter.consume(
        blogalone::security::RateLimitScope::post,
        "198.51.100.1",
        42,
        policy,
        started_at + std::chrono::seconds{60}
    ));
}

TEST(InfrastructureTest, ResetsFailedLoginWindow)
{
    blogalone::security::RequestRateLimiter limiter;
    const blogalone::security::RateLimitPolicy policy{
        .max_requests = 1,
        .window = std::chrono::minutes{5}
    };

    auto failed_attempt = limiter.reserve(
        blogalone::security::RateLimitScope::login,
        "198.51.100.1",
        std::nullopt,
        policy
    );
    ASSERT_TRUE(failed_attempt.has_value());
    failed_attempt->commit();
    EXPECT_FALSE(limiter.reserve(
        blogalone::security::RateLimitScope::login,
        "198.51.100.1",
        std::nullopt,
        policy
    ).has_value());

    limiter.reset(
        blogalone::security::RateLimitScope::login,
        "198.51.100.1",
        std::nullopt
    );
    EXPECT_TRUE(limiter.reserve(
        blogalone::security::RateLimitScope::login,
        "198.51.100.1",
        std::nullopt,
        policy
    ).has_value());
}

TEST(InfrastructureTest, KeepsConcurrentLoginReservationsAcrossReset)
{
    blogalone::security::RequestRateLimiter limiter;
    const blogalone::security::RateLimitPolicy policy{
        .max_requests = 2,
        .window = std::chrono::minutes{5}
    };
    const auto started_at = blogalone::security::RequestRateLimiter::TimePoint{};

    auto successful = limiter.reserve(
        blogalone::security::RateLimitScope::login,
        "198.51.100.1",
        std::nullopt,
        policy,
        started_at
    );
    auto concurrent_failure = limiter.reserve(
        blogalone::security::RateLimitScope::login,
        "198.51.100.1",
        std::nullopt,
        policy,
        started_at
    );
    ASSERT_TRUE(successful.has_value());
    ASSERT_TRUE(concurrent_failure.has_value());
    EXPECT_FALSE(limiter.reserve(
        blogalone::security::RateLimitScope::login,
        "198.51.100.1",
        std::nullopt,
        policy,
        started_at
    ).has_value());

    successful->cancel();
    limiter.reset(
        blogalone::security::RateLimitScope::login,
        "198.51.100.1",
        std::nullopt
    );
    concurrent_failure->commit(started_at + std::chrono::seconds{1});

    auto next_attempt = limiter.reserve(
        blogalone::security::RateLimitScope::login,
        "198.51.100.1",
        std::nullopt,
        policy,
        started_at + std::chrono::seconds{2}
    );
    ASSERT_TRUE(next_attempt.has_value());
    EXPECT_FALSE(limiter.reserve(
        blogalone::security::RateLimitScope::login,
        "198.51.100.1",
        std::nullopt,
        policy,
        started_at + std::chrono::seconds{2}
    ).has_value());
}

TEST(InfrastructureTest, CancelsAbandonedLoginReservation)
{
    blogalone::security::RequestRateLimiter limiter;
    const blogalone::security::RateLimitPolicy policy{
        .max_requests = 1,
        .window = std::chrono::minutes{5}
    };

    {
        auto abandoned = limiter.reserve(
            blogalone::security::RateLimitScope::login,
            "198.51.100.1",
            std::nullopt,
            policy
        );
        ASSERT_TRUE(abandoned.has_value());
        EXPECT_FALSE(limiter.reserve(
            blogalone::security::RateLimitScope::login,
            "198.51.100.1",
            std::nullopt,
            policy
        ).has_value());
    }

    EXPECT_TRUE(limiter.reserve(
        blogalone::security::RateLimitScope::login,
        "198.51.100.1",
        std::nullopt,
        policy
    ).has_value());
}

TEST(InfrastructureTest, GeneratesHexTokensAndHashes)
{
    const auto token = blogalone::util::random_token_hex(16);

    EXPECT_EQ(token.size(), 32);
    EXPECT_TRUE(blogalone::util::is_lower_hex(token));
    EXPECT_FALSE(blogalone::util::is_lower_hex(""));
    EXPECT_FALSE(blogalone::util::is_lower_hex("ABCDEF"));
    EXPECT_FALSE(blogalone::util::is_lower_hex("abcxyz"));
    EXPECT_EQ(
        blogalone::util::sha256_hex("abc"),
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"
    );
}

TEST(InfrastructureTest, ComparesSensitiveStringsByValue)
{
    EXPECT_TRUE(blogalone::util::constant_time_equal("csrf-hash", "csrf-hash"));
    EXPECT_FALSE(blogalone::util::constant_time_equal("csrf-hash", "csrf-Hash"));
    EXPECT_FALSE(blogalone::util::constant_time_equal("csrf-hash", "csrf-hash-extra"));
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
