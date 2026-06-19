#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace blogalone::util {

[[nodiscard]] std::optional<std::string> normalize_ip_address(std::string_view value);

}
