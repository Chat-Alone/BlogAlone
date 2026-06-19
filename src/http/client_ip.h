#pragma once

#include <drogon/HttpRequest.h>

#include <span>
#include <string>
#include <string_view>

namespace blogalone::http {

inline constexpr std::string_view client_ip_attribute{"blogalone.client_ip"};

[[nodiscard]] std::string resolve_client_ip(
    std::string_view peer_ip,
    std::string_view forwarded_for,
    std::span<const std::string> trusted_proxies
);
[[nodiscard]] std::string client_ip_from(const drogon::HttpRequestPtr& request);
void install_client_ip_advice();

}
