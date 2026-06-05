#include "util/crypto.h"

#include <sodium.h>

#include <array>
#include <limits>
#include <span>
#include <stdexcept>
#include <vector>

namespace blogalone::util {
namespace {

void ensure_sodium_initialized()
{
    if(sodium_init() < 0) {
        throw std::runtime_error{"libsodium initialization failed"};
    }
}

[[nodiscard]] std::string bytes_to_lower_hex(std::span<const unsigned char> bytes)
{
    constexpr std::array<char, 16> hex_digits{
        '0', '1', '2', '3', '4', '5', '6', '7',
        '8', '9', 'a', 'b', 'c', 'd', 'e', 'f'
    };

    std::string output;
    output.reserve(bytes.size() * 2);
    for(const auto byte : bytes) {
        output.push_back(hex_digits.at(static_cast<std::size_t>(byte >> 4)));
        output.push_back(hex_digits.at(static_cast<std::size_t>(byte & 0x0f)));
    }
    return output;
}

}

std::string random_token_hex(std::size_t byte_count)
{
    ensure_sodium_initialized();
    if(byte_count == 0 || byte_count > std::numeric_limits<unsigned int>::max()) {
        throw std::invalid_argument{"token byte count out of range"};
    }

    std::vector<unsigned char> bytes(byte_count);
    randombytes_buf(bytes.data(), bytes.size());
    return bytes_to_lower_hex(bytes);
}

std::string sha256_hex(std::string_view content)
{
    ensure_sodium_initialized();

    std::array<unsigned char, crypto_hash_sha256_BYTES> digest{};
    crypto_hash_sha256(
        digest.data(),
        reinterpret_cast<const unsigned char*>(content.data()),
        static_cast<unsigned long long>(content.size())
    );
    return bytes_to_lower_hex(digest);
}

bool is_lower_hex(std::string_view value)
{
    if(value.empty()) {
        return false;
    }

    for(const auto ch : value) {
        const auto is_digit = ch >= '0' && ch <= '9';
        const auto is_lower_alpha = ch >= 'a' && ch <= 'f';
        if(!is_digit && !is_lower_alpha) {
            return false;
        }
    }
    return true;
}

bool constant_time_equal(std::string_view left, std::string_view right)
{
    ensure_sodium_initialized();
    if(left.size() != right.size()) {
        return false;
    }
    if(left.empty()) {
        return true;
    }
    return sodium_memcmp(left.data(), right.data(), left.size()) == 0;
}

}
