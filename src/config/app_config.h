#pragma once

#include "security/rate_limiter.h"
#include "util/image.h"
#include "util/password.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace Json {
class Value;
}

namespace blogalone::config {

struct UploadConfig {
    std::int64_t max_file_size{5 * 1024 * 1024};
    std::int64_t max_daily_uploads{100};
    std::int64_t max_dimension{util::kMaxDecodedImageDimension};
};

struct RateLimitConfig {
    security::RateLimitPolicy registration{
        .max_requests = 5,
        .window = std::chrono::hours{1}
    };
    security::RateLimitPolicy login{
        .max_requests = 5,
        .window = std::chrono::minutes{5}
    };
    security::RateLimitPolicy upload{
        .max_requests = 20,
        .window = std::chrono::minutes{1}
    };
    security::RateLimitPolicy post{
        .max_requests = 30,
        .window = std::chrono::minutes{1}
    };
};

struct UploadCleanupConfig {
    int retention_seconds{86'400};
    int interval_seconds{3'600};
};

struct SessionCleanupConfig {
    int interval_seconds{3'600};
};

struct AppConfig {
    std::vector<std::string> trusted_proxies;
    std::filesystem::path uploads_root{"uploads"};
    std::filesystem::path web_root{"web"};
    int session_ttl_seconds{1'209'600};
    UploadConfig upload;
    RateLimitConfig rate_limits;
    UploadCleanupConfig upload_cleanup;
    SessionCleanupConfig session_cleanup;
    util::PasswordHashOptions password_hash_options{util::default_password_hash_options()};
};

[[nodiscard]] AppConfig app_config_from_json(const Json::Value& custom_config);
[[nodiscard]] const AppConfig& app_config_from_drogon();

}
