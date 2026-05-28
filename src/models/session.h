#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace blogalone::models {

struct Session {
    std::string token_hash;
    std::int64_t user_id{};
    std::string csrf_hash;
    std::int64_t created_at{};
    std::int64_t expires_at{};
    std::optional<std::int64_t> revoked_at;
    std::optional<std::int64_t> admin_confirmed_at;
    std::string ip;
    std::string user_agent;
};

}
