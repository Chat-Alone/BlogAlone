#include "http/client_ip.h"

#include "config/app_config.h"

#include <drogon/drogon.h>
#include <trantor/net/InetAddress.h>

#include <algorithm>
#include <cctype>

namespace blogalone::http {
namespace {

[[nodiscard]] std::string_view trim_ascii(std::string_view value)
{
    while(!value.empty() && std::isspace(static_cast<unsigned char>(value.front())) != 0) {
        value.remove_prefix(1);
    }
    while(!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.remove_suffix(1);
    }
    return value;
}

[[nodiscard]] bool is_trusted(
    std::string_view normalized_ip,
    std::span<const std::string> trusted_proxies
)
{
    return std::ranges::any_of(trusted_proxies, [normalized_ip](const std::string& proxy) {
        const auto normalized_proxy = normalize_ip_address(proxy);
        return normalized_proxy.has_value() && *normalized_proxy == normalized_ip;
    });
}

}

std::optional<std::string> normalize_ip_address(std::string_view value)
{
    value = trim_ascii(value);
    if(value.empty()) {
        return std::nullopt;
    }

    const bool is_ipv6 = value.find(':') != std::string_view::npos;
    const trantor::InetAddress address{std::string{value}, 0, is_ipv6};
    if(address.isUnspecified()) {
        return std::nullopt;
    }
    return address.toIp();
}

std::string resolve_client_ip(
    std::string_view peer_ip,
    std::string_view forwarded_for,
    std::span<const std::string> trusted_proxies
)
{
    const auto normalized_peer = normalize_ip_address(peer_ip);
    const auto fallback = normalized_peer.value_or(std::string{peer_ip});
    if(!normalized_peer.has_value() || !is_trusted(*normalized_peer, trusted_proxies)) {
        return fallback;
    }
    if(forwarded_for.empty() || forwarded_for.size() > 4096) {
        return fallback;
    }

    std::size_t end = forwarded_for.size();
    while(end > 0) {
        const auto separator = forwarded_for.rfind(',', end - 1);
        const auto start = separator == std::string_view::npos ? 0 : separator + 1;
        const auto candidate = normalize_ip_address(forwarded_for.substr(start, end - start));
        if(!candidate.has_value()) {
            return fallback;
        }
        if(!is_trusted(*candidate, trusted_proxies)) {
            return *candidate;
        }
        if(separator == std::string_view::npos) {
            break;
        }
        end = separator;
    }
    return fallback;
}

std::string client_ip_from(const drogon::HttpRequestPtr& request)
{
    if(!request) {
        return {};
    }
    const auto attributes = request->attributes();
    if(attributes->find(std::string{client_ip_attribute})) {
        return attributes->get<std::string>(std::string{client_ip_attribute});
    }
    return request->peerAddr().toIp();
}

void install_client_ip_advice()
{
    drogon::app().registerPreRoutingAdvice([](const drogon::HttpRequestPtr& request) {
        const auto app_config = config::app_config_from_drogon();
        const auto client_ip = resolve_client_ip(
            request->peerAddr().toIp(),
            request->getHeader("x-forwarded-for"),
            app_config.trusted_proxies
        );
        request->attributes()->insert(std::string{client_ip_attribute}, client_ip);
    });
}

}
