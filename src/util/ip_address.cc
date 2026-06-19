#include "util/ip_address.h"

#include <trantor/net/InetAddress.h>

#include <cctype>

namespace blogalone::util {
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

}
