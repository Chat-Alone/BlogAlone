#include "config/app_config.h"

#include "util/ip_address.h"

#include <drogon/drogon.h>
#include <json/value.h>

#include <chrono>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string_view>

namespace blogalone::config {
namespace {

[[nodiscard]] std::filesystem::path path_or_default(
    const Json::Value& config,
    std::string_view key,
    std::filesystem::path fallback
)
{
    const std::string key_name{key};
    if(!config.isMember(key_name)) {
        return fallback;
    }
    if(!config[key_name].isString() || config[key_name].asString().empty()) {
        throw std::invalid_argument{"custom_config." + key_name + " must be a non-empty string"};
    }
    return std::filesystem::path{config[key_name].asString()};
}

[[nodiscard]] int positive_int_or_default(
    const Json::Value& config,
    std::string_view key,
    int fallback
)
{
    const std::string key_name{key};
    if(!config.isMember(key_name)) {
        return fallback;
    }
    if(!config[key_name].isInt() || config[key_name].asInt() <= 0) {
        throw std::invalid_argument{"custom_config." + key_name + " must be a positive integer"};
    }
    return config[key_name].asInt();
}

[[nodiscard]] std::int64_t positive_int64_or_default(
    const Json::Value& config,
    std::string_view key,
    std::int64_t fallback
)
{
    const std::string key_name{key};
    if(!config.isMember(key_name)) {
        return fallback;
    }
    if(config[key_name].isInt64() && config[key_name].asInt64() > 0) {
        return config[key_name].asInt64();
    }
    const auto max_int64 = static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)());
    if(config[key_name].isUInt64()
        && config[key_name].asUInt64() <= max_int64) {
        return static_cast<std::int64_t>(config[key_name].asUInt64());
    }
    throw std::invalid_argument{"custom_config." + key_name + " must be a positive integer"};
}

[[nodiscard]] std::uint64_t positive_uint64_or_default(
    const Json::Value& config,
    std::string_view key,
    std::uint64_t fallback
)
{
    const std::string key_name{key};
    if(!config.isMember(key_name)) {
        return fallback;
    }
    if(config[key_name].isUInt64() && config[key_name].asUInt64() > 0) {
        return config[key_name].asUInt64();
    }
    if(config[key_name].isInt64() && config[key_name].asInt64() > 0) {
        return static_cast<std::uint64_t>(config[key_name].asInt64());
    }
    throw std::invalid_argument{"custom_config." + key_name + " must be a positive integer"};
}

[[nodiscard]] util::PasswordHashOptions password_hash_options_from(const Json::Value& custom_config)
{
    const auto defaults = util::default_password_hash_options();
    const auto memlimit = positive_uint64_or_default(
        custom_config,
        "password_memlimit",
        defaults.memlimit
    );
    if(memlimit > (std::numeric_limits<std::size_t>::max)()) {
        throw std::invalid_argument{"custom_config.password_memlimit is too large"};
    }

    return util::PasswordHashOptions{
        .opslimit = positive_uint64_or_default(
            custom_config,
            "password_opslimit",
            defaults.opslimit
        ),
        .memlimit = static_cast<std::size_t>(memlimit)
    };
}

[[nodiscard]] UploadConfig upload_config_from(const Json::Value& custom_config)
{
    const UploadConfig defaults;
    UploadConfig upload{
        .max_file_size = positive_int64_or_default(
            custom_config,
            "upload_max_file_size",
            defaults.max_file_size
        ),
        .max_daily_uploads = positive_int64_or_default(
            custom_config,
            "upload_max_daily_uploads",
            defaults.max_daily_uploads
        ),
        .max_dimension = positive_int64_or_default(
            custom_config,
            "upload_max_dimension",
            defaults.max_dimension
        )
    };
    if(upload.max_dimension > util::kMaxDecodedImageDimension) {
        throw std::invalid_argument{"custom_config.upload_max_dimension exceeds decoded image limit"};
    }
    return upload;
}

[[nodiscard]] security::RateLimitPolicy rate_limit_policy_from(
    const Json::Value& custom_config,
    std::string_view max_requests_key,
    std::string_view window_seconds_key,
    security::RateLimitPolicy fallback
)
{
    const auto max_requests = positive_int_or_default(
        custom_config,
        max_requests_key,
        static_cast<int>(fallback.max_requests)
    );
    const auto window_seconds = positive_int_or_default(
        custom_config,
        window_seconds_key,
        static_cast<int>(fallback.window.count())
    );
    return security::RateLimitPolicy{
        .max_requests = static_cast<std::size_t>(max_requests),
        .window = std::chrono::seconds{window_seconds}
    };
}

[[nodiscard]] RateLimitConfig rate_limit_config_from(const Json::Value& custom_config)
{
    const RateLimitConfig defaults;
    return RateLimitConfig{
        .registration = rate_limit_policy_from(
            custom_config,
            "rate_limit_registration_max_requests",
            "rate_limit_registration_window_seconds",
            defaults.registration
        ),
        .login = rate_limit_policy_from(
            custom_config,
            "rate_limit_login_max_requests",
            "rate_limit_login_window_seconds",
            defaults.login
        ),
        .upload = rate_limit_policy_from(
            custom_config,
            "rate_limit_upload_max_requests",
            "rate_limit_upload_window_seconds",
            defaults.upload
        ),
        .post = rate_limit_policy_from(
            custom_config,
            "rate_limit_post_max_requests",
            "rate_limit_post_window_seconds",
            defaults.post
        )
    };
}

[[nodiscard]] UploadCleanupConfig upload_cleanup_config_from(const Json::Value& custom_config)
{
    const UploadCleanupConfig defaults;
    return UploadCleanupConfig{
        .retention_seconds = positive_int_or_default(
            custom_config,
            "orphan_upload_retention_seconds",
            defaults.retention_seconds
        ),
        .interval_seconds = positive_int_or_default(
            custom_config,
            "upload_cleanup_interval_seconds",
            defaults.interval_seconds
        )
    };
}

[[nodiscard]] SessionCleanupConfig session_cleanup_config_from(const Json::Value& custom_config)
{
    const SessionCleanupConfig defaults;
    return SessionCleanupConfig{
        .interval_seconds = positive_int_or_default(
            custom_config,
            "session_cleanup_interval_seconds",
            defaults.interval_seconds
        )
    };
}

[[nodiscard]] std::vector<std::string> normalized_ip_array_or_empty(
    const Json::Value& config,
    std::string_view key
)
{
    const std::string key_name{key};
    if(!config.isMember(key_name)) {
        return {};
    }
    if(!config[key_name].isArray()) {
        throw std::invalid_argument{"custom_config." + key_name + " must be an array"};
    }

    std::vector<std::string> values;
    for(const auto& item : config[key_name]) {
        if(!item.isString() || item.asString().empty()) {
            throw std::invalid_argument{"custom_config." + key_name + " must contain strings"};
        }
        const auto normalized = util::normalize_ip_address(item.asString());
        if(!normalized.has_value()) {
            throw std::invalid_argument{"custom_config." + key_name + " contains an invalid IP address"};
        }
        values.push_back(*normalized);
    }
    return values;
}

}

AppConfig app_config_from_json(const Json::Value& custom_config)
{
    return AppConfig{
        .trusted_proxies = normalized_ip_array_or_empty(custom_config, "trusted_proxies"),
        .uploads_root = path_or_default(custom_config, "uploads_root", "uploads"),
        .web_root = path_or_default(custom_config, "web_root", "web"),
        .session_ttl_seconds = positive_int_or_default(
            custom_config,
            "session_ttl_seconds",
            1'209'600
        ),
        .upload = upload_config_from(custom_config),
        .rate_limits = rate_limit_config_from(custom_config),
        .upload_cleanup = upload_cleanup_config_from(custom_config),
        .session_cleanup = session_cleanup_config_from(custom_config),
        .password_hash_options = password_hash_options_from(custom_config)
    };
}

const AppConfig& app_config_from_drogon()
{
    static const AppConfig app_config = app_config_from_json(drogon::app().getCustomConfig());
    return app_config;
}

}
