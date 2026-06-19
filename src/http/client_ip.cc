#include "http/client_ip.h"

#include "config/app_config.h"
#include "util/ip_address.h"

#include <drogon/drogon.h>

#include <algorithm>

namespace blogalone::http {
namespace {

[[nodiscard]] bool is_trusted(
    std::string_view normalized_ip,
    std::span<const std::string> trusted_proxies
)
{
    return std::ranges::find(trusted_proxies, normalized_ip) != trusted_proxies.end();
}

}

std::string resolve_client_ip(
    std::string_view peer_ip,
    std::string_view forwarded_for,
    std::span<const std::string> trusted_proxies
)
{
    const auto normalized_peer = util::normalize_ip_address(peer_ip);
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
        const auto candidate = util::normalize_ip_address(forwarded_for.substr(start, end - start));
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
        const auto& app_config = config::app_config_from_drogon();
        const auto client_ip = resolve_client_ip(
            request->peerAddr().toIp(),
            request->getHeader("x-forwarded-for"),
            app_config.trusted_proxies
        );
        request->attributes()->insert(std::string{client_ip_attribute}, client_ip);
    });
}

}
