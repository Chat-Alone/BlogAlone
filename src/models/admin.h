#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace blogalone::models {

struct AuditLogEntry {
    std::int64_t id{};
    std::optional<std::int64_t> admin_id;
    std::string action;
    std::string target_type;
    std::int64_t target_id{};
    std::string detail;
    std::int64_t created_at{};
};

}
