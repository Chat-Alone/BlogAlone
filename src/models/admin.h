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

struct AdminSessionSummary {
    std::string token_hash;
    std::int64_t user_id{};
    std::string username;
    std::int64_t created_at{};
    std::int64_t expires_at{};
    std::optional<std::int64_t> revoked_at;
    std::optional<std::int64_t> admin_confirmed_at;
    std::string ip;
    std::string user_agent;
};

struct DeletedThreadSummary {
    std::int64_t id{};
    std::int64_t forum_id{};
    std::string forum_slug;
    std::string forum_name;
    std::int64_t author_id{};
    std::string author_username;
    std::string title;
    std::string body_excerpt;
    std::optional<std::int64_t> deleted_by;
    std::optional<std::string> deleted_by_username;
    std::int64_t deleted_at{};
};

struct DeletedPostSummary {
    std::int64_t id{};
    std::int64_t thread_id{};
    std::string thread_title;
    std::int64_t author_id{};
    std::string author_username;
    std::int64_t floor_no{};
    std::string body_excerpt;
    std::optional<std::int64_t> deleted_by;
    std::optional<std::string> deleted_by_username;
    std::int64_t deleted_at{};
};

struct DeletedSubPostSummary {
    std::int64_t id{};
    std::int64_t post_id{};
    std::int64_t thread_id{};
    std::string thread_title;
    std::int64_t author_id{};
    std::string author_username;
    std::string body_excerpt;
    std::optional<std::int64_t> deleted_by;
    std::optional<std::string> deleted_by_username;
    std::int64_t deleted_at{};
};

}
