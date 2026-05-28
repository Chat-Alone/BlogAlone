#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace blogalone::models {

enum class UserRole {
    user,
    admin
};

struct User {
    std::int64_t id{};
    std::string username;
    std::optional<std::string> email;
    std::string pwd_hash;
    UserRole role{UserRole::user};
    std::optional<std::int64_t> banned_until;
    std::int64_t created_at{};
    std::int64_t updated_at{};
    std::optional<std::string> avatar_url;
};

[[nodiscard]] std::string_view to_string(UserRole role);
[[nodiscard]] UserRole user_role_from_string(std::string_view value);

}
