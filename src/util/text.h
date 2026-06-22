#pragma once

#include <string>
#include <string_view>

namespace blogalone::util {

[[nodiscard]] std::string trim_ascii_whitespace(std::string_view value);

}
