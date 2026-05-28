#include "util/password.h"

#include <sodium.h>

#include <array>
#include <stdexcept>

namespace blogalone::util {
namespace {

void ensure_sodium_initialized()
{
    if(sodium_init() < 0) {
        throw std::runtime_error{"libsodium initialization failed"};
    }
}

}

PasswordHashOptions default_password_hash_options()
{
    return PasswordHashOptions{
        .opslimit = crypto_pwhash_OPSLIMIT_MODERATE,
        .memlimit = crypto_pwhash_MEMLIMIT_MODERATE
    };
}

std::string hash_password(std::string_view password)
{
    return hash_password(password, default_password_hash_options());
}

std::string hash_password(std::string_view password, const PasswordHashOptions& options)
{
    ensure_sodium_initialized();

    if(password.empty() || password.size() > 128) {
        throw std::invalid_argument{"password length out of range"};
    }
    if(options.opslimit == 0 || options.memlimit == 0) {
        throw std::invalid_argument{"password hash options out of range"};
    }

    std::array<char, crypto_pwhash_STRBYTES> output{};
    const auto status = crypto_pwhash_str(
        output.data(),
        password.data(),
        password.size(),
        options.opslimit,
        options.memlimit
    );
    if(status != 0) {
        throw std::runtime_error{"password hashing failed"};
    }
    return std::string{output.data()};
}

bool verify_password(std::string_view password, std::string_view hash)
{
    ensure_sodium_initialized();

    if(password.empty() || hash.empty()) {
        return false;
    }

    std::array<char, crypto_pwhash_STRBYTES> hash_buffer{};
    if(hash.size() >= hash_buffer.size()) {
        return false;
    }
    std::copy(hash.begin(), hash.end(), hash_buffer.begin());

    return crypto_pwhash_str_verify(
        hash_buffer.data(),
        password.data(),
        password.size()
    ) == 0;
}

}
