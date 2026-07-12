#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace blogalone::util {

[[nodiscard]] std::string trim_ascii_whitespace(std::string_view value);

// Truncates to at most max_bytes bytes without splitting a multi-byte UTF-8
// sequence, appending an ellipsis marker when truncation occurred.
[[nodiscard]] std::string truncate_utf8_excerpt(std::string_view value, std::size_t max_bytes);

}
