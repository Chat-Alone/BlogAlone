#include "config/app_config.h"

#include <drogon/drogon.h>
#include <json/value.h>

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
        )
    };
}

AppConfig app_config_from_drogon()
{
    return app_config_from_json(drogon::app().getCustomConfig());
}

}
