#pragma once

#include <string_view>

namespace blogalone::util {

[[nodiscard]] bool is_valid_username(std::string_view username);
[[nodiscard]] bool is_valid_password(std::string_view password);

}
