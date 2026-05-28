#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace blogalone::util {

[[nodiscard]] std::string random_token_hex(std::size_t byte_count = 32);
[[nodiscard]] std::string sha256_hex(std::string_view content);
[[nodiscard]] bool is_lower_hex(std::string_view value);

}
