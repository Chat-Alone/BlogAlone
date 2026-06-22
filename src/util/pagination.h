#pragma once

#include <cstdint>
#include <limits>
#include <optional>

namespace blogalone::util {

inline constexpr std::int64_t kDefaultPageSize = 20;
inline constexpr std::int64_t kMaxPage = 1'000'000;
inline constexpr std::int64_t kMaxPageSize = 50;

[[nodiscard]] constexpr std::optional<std::int64_t> pagination_offset(
    std::int64_t page,
    std::int64_t page_size
)
{
    if(page < 1 || page > kMaxPage || page_size < 1 || page_size > kMaxPageSize) {
        return std::nullopt;
    }

    const auto pages_before = page - 1;
    if(pages_before > (std::numeric_limits<std::int64_t>::max)() / page_size) {
        return std::nullopt;
    }
    return pages_before * page_size;
}

}
