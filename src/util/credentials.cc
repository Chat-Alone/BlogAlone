#include "util/credentials.h"

#include <cctype>
#include <cstddef>
#include <optional>

namespace blogalone::util {
namespace {

constexpr std::size_t kMinUsernameLength = 3;
constexpr std::size_t kMaxUsernameLength = 32;
constexpr std::size_t kMinPasswordLength = 8;
constexpr std::size_t kMaxPasswordLength = 128;

[[nodiscard]] std::optional<char32_t> read_utf8_codepoint(
    std::string_view value,
    std::size_t& index
)
{
    const auto first = static_cast<unsigned char>(value.at(index));
    if(first < 0x80) {
        ++index;
        return first;
    }

    std::size_t continuation_count = 0;
    char32_t codepoint = 0;
    char32_t minimum = 0;
    if(first >= 0xc2 && first <= 0xdf) {
        continuation_count = 1;
        codepoint = first & 0x1f;
        minimum = 0x80;
    } else if(first >= 0xe0 && first <= 0xef) {
        continuation_count = 2;
        codepoint = first & 0x0f;
        minimum = 0x800;
    } else if(first >= 0xf0 && first <= 0xf4) {
        continuation_count = 3;
        codepoint = first & 0x07;
        minimum = 0x10000;
    } else {
        return std::nullopt;
    }

    if(index + continuation_count >= value.size()) {
        return std::nullopt;
    }

    for(std::size_t offset = 1; offset <= continuation_count; ++offset) {
        const auto byte = static_cast<unsigned char>(value.at(index + offset));
        if((byte & 0xc0) != 0x80) {
            return std::nullopt;
        }
        codepoint = (codepoint << 6) | (byte & 0x3f);
    }

    index += continuation_count + 1;
    if(codepoint < minimum || (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
        return std::nullopt;
    }
    return codepoint;
}

[[nodiscard]] bool is_chinese_codepoint(char32_t codepoint)
{
    return (codepoint >= 0x3400 && codepoint <= 0x4dbf)
        || (codepoint >= 0x4e00 && codepoint <= 0x9fff)
        || (codepoint >= 0xf900 && codepoint <= 0xfaff)
        || (codepoint >= 0x20000 && codepoint <= 0x2ebef);
}

[[nodiscard]] bool is_username_codepoint(char32_t codepoint)
{
    if(codepoint < 0x80) {
        const auto ch = static_cast<unsigned char>(codepoint);
        return std::isalnum(ch) != 0 || ch == '_';
    }
    return is_chinese_codepoint(codepoint);
}

}

bool is_valid_username(std::string_view username)
{
    std::size_t length = 0;
    std::size_t index = 0;
    while(index < username.size()) {
        const auto codepoint = read_utf8_codepoint(username, index);
        if(!codepoint.has_value() || !is_username_codepoint(*codepoint)) {
            return false;
        }
        ++length;
        if(length > kMaxUsernameLength) {
            return false;
        }
    }
    return length >= kMinUsernameLength;
}

bool is_valid_password(std::string_view password)
{
    return password.size() >= kMinPasswordLength && password.size() <= kMaxPasswordLength;
}

}
