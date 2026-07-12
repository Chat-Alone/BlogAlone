#include "util/text.h"

namespace blogalone::util {

std::string trim_ascii_whitespace(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if(first == std::string_view::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string{value.substr(first, last - first + 1)};
}

std::string truncate_utf8_excerpt(std::string_view value, std::size_t max_bytes)
{
    if(value.size() <= max_bytes) {
        return std::string{value};
    }

    auto end = max_bytes;
    // Back off while the next byte is a UTF-8 continuation byte (10xxxxxx),
    // so the excerpt never splits a multi-byte code point.
    while(end > 0 && (static_cast<unsigned char>(value[end]) & 0xC0) == 0x80) {
        --end;
    }
    return std::string{value.substr(0, end)} + "...";
}

}
