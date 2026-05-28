#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace Json {
class Value;
}

namespace blogalone::config {

struct AppConfig {
    std::vector<std::string> trusted_proxies;
    std::filesystem::path uploads_root{"uploads"};
    std::filesystem::path web_root{"web"};
    int session_ttl_seconds{1'209'600};
};

[[nodiscard]] AppConfig app_config_from_json(const Json::Value& custom_config);
[[nodiscard]] AppConfig app_config_from_drogon();

}
