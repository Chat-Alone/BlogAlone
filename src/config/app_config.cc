#include "config/app_config.h"

#include <drogon/drogon.h>
#include <json/value.h>

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace blogalone::config {
namespace {

[[nodiscard]] std::filesystem::path path_or_default(
    const Json::Value& config,
    const char* key,
    std::filesystem::path fallback
)
{
    if(!config.isMember(key)) {
        return fallback;
    }
    if(!config[key].isString() || config[key].asString().empty()) {
        throw std::invalid_argument{std::string{"custom_config."} + key + " must be a non-empty string"};
    }
    return std::filesystem::path{config[key].asString()};
}

[[nodiscard]] int positive_int_or_default(const Json::Value& config, const char* key, int fallback)
{
    if(!config.isMember(key)) {
        return fallback;
    }
    if(!config[key].isInt() || config[key].asInt() <= 0) {
        throw std::invalid_argument{std::string{"custom_config."} + key + " must be a positive integer"};
    }
    return config[key].asInt();
}

[[nodiscard]] std::uint64_t positive_uint64_or_default(
    const Json::Value& config,
    const char* key,
    std::uint64_t fallback
)
{
    if(!config.isMember(key)) {
        return fallback;
    }
    if(config[key].isUInt64() && config[key].asUInt64() > 0) {
        return config[key].asUInt64();
    }
    if(config[key].isInt64() && config[key].asInt64() > 0) {
        return static_cast<std::uint64_t>(config[key].asInt64());
    }
    throw std::invalid_argument{std::string{"custom_config."} + key + " must be a positive integer"};
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

[[nodiscard]] std::vector<std::string> string_array_or_empty(const Json::Value& config, const char* key)
{
    if(!config.isMember(key)) {
        return {};
    }
    if(!config[key].isArray()) {
        throw std::invalid_argument{std::string{"custom_config."} + key + " must be an array"};
    }

    std::vector<std::string> values;
    for(const auto& item : config[key]) {
        if(!item.isString() || item.asString().empty()) {
            throw std::invalid_argument{std::string{"custom_config."} + key + " must contain strings"};
        }
        values.push_back(item.asString());
    }
    return values;
}

}

AppConfig app_config_from_json(const Json::Value& custom_config)
{
    return AppConfig{
        .trusted_proxies = string_array_or_empty(custom_config, "trusted_proxies"),
        .uploads_root = path_or_default(custom_config, "uploads_root", "uploads"),
        .web_root = path_or_default(custom_config, "web_root", "web"),
        .session_ttl_seconds = positive_int_or_default(
            custom_config,
            "session_ttl_seconds",
            1'209'600
        ),
        .password_hash_options = password_hash_options_from(custom_config)
    };
}

AppConfig app_config_from_drogon()
{
    return app_config_from_json(drogon::app().getCustomConfig());
}

}
