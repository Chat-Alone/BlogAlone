#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace blogalone::util {

struct PasswordHashOptions {
    unsigned long long opslimit{};
    std::size_t memlimit{};
};

[[nodiscard]] PasswordHashOptions default_password_hash_options();
[[nodiscard]] std::string hash_password(std::string_view password);
[[nodiscard]] std::string hash_password(
    std::string_view password,
    const PasswordHashOptions& options
);
[[nodiscard]] bool verify_password(std::string_view password, std::string_view hash);

}
