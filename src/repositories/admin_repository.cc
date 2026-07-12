#include "repositories/admin_repository.h"

#include "util/text.h"

#include <drogon/drogon.h>

#include <utility>

namespace blogalone::repositories {
namespace {

constexpr std::string_view kUserColumns{
    "id, username, email, pwd_hash, role, banned_until, created_at, updated_at, avatar_url"
};

constexpr std::size_t kDeletedContentExcerptBytes = 200;

[[nodiscard]] models::Forum row_to_forum(const drogon::orm::Row& row)
{
    return models::Forum{
        .id = row["id"].as<std::int64_t>(),
        .slug = row["slug"].as<std::string>(),
        .name = row["name"].as<std::string>(),
        .description = row["description"].as<std::string>(),
        .sort_order = row["sort_order"].as<std::int64_t>(),
        .created_at = row["created_at"].as<std::int64_t>(),
        .updated_at = row["updated_at"].as<std::int64_t>()
    };
}

[[nodiscard]] models::User row_to_user(const drogon::orm::Row& row)
{
    return models::User{
        .id = row["id"].as<std::int64_t>(),
        .username = row["username"].as<std::string>(),
        .email = row["email"].isNull()
            ? std::nullopt
            : std::optional{row["email"].as<std::string>()},
        .pwd_hash = row["pwd_hash"].as<std::string>(),
        .role = models::user_role_from_string(row["role"].as<std::string>()),
        .banned_until = row["banned_until"].isNull()
            ? std::nullopt
            : std::optional{row["banned_until"].as<std::int64_t>()},
        .created_at = row["created_at"].as<std::int64_t>(),
        .updated_at = row["updated_at"].as<std::int64_t>(),
        .avatar_url = row["avatar_url"].isNull()
            ? std::nullopt
            : std::optional{row["avatar_url"].as<std::string>()}
    };
}

[[nodiscard]] models::AuditLogEntry row_to_audit_log(const drogon::orm::Row& row)
{
    return models::AuditLogEntry{
        .id = row["id"].as<std::int64_t>(),
        .admin_id = row["admin_id"].isNull()
            ? std::nullopt
            : std::optional{row["admin_id"].as<std::int64_t>()},
        .action = row["action"].as<std::string>(),
        .target_type = row["target_type"].as<std::string>(),
        .target_id = row["target_id"].as<std::int64_t>(),
        .detail = row["detail"].as<std::string>(),
        .created_at = row["created_at"].as<std::int64_t>()
    };
}

[[nodiscard]] models::AdminSessionSummary row_to_session_summary(const drogon::orm::Row& row)
{
    return models::AdminSessionSummary{
        .token_hash = row["token_hash"].as<std::string>(),
        .user_id = row["user_id"].as<std::int64_t>(),
        .username = row["username"].as<std::string>(),
        .created_at = row["created_at"].as<std::int64_t>(),
        .expires_at = row["expires_at"].as<std::int64_t>(),
        .revoked_at = row["revoked_at"].isNull()
            ? std::nullopt
            : std::optional{row["revoked_at"].as<std::int64_t>()},
        .admin_confirmed_at = row["admin_confirmed_at"].isNull()
            ? std::nullopt
            : std::optional{row["admin_confirmed_at"].as<std::int64_t>()},
        .ip = row["ip"].as<std::string>(),
        .user_agent = row["user_agent"].as<std::string>()
    };
}

[[nodiscard]] std::optional<std::int64_t> row_deleted_by(const drogon::orm::Row& row)
{
    return row["deleted_by"].isNull()
        ? std::nullopt
        : std::optional{row["deleted_by"].as<std::int64_t>()};
}

[[nodiscard]] std::optional<std::string> row_deleted_by_username(const drogon::orm::Row& row)
{
    return row["deleted_by_username"].isNull()
        ? std::nullopt
        : std::optional{row["deleted_by_username"].as<std::string>()};
}

[[nodiscard]] models::DeletedThreadSummary row_to_deleted_thread(const drogon::orm::Row& row)
{
    return models::DeletedThreadSummary{
        .id = row["id"].as<std::int64_t>(),
        .forum_id = row["forum_id"].as<std::int64_t>(),
        .forum_slug = row["forum_slug"].as<std::string>(),
        .forum_name = row["forum_name"].as<std::string>(),
        .author_id = row["author_id"].as<std::int64_t>(),
        .author_username = row["author_username"].as<std::string>(),
        .title = row["title"].as<std::string>(),
        .body_excerpt = util::truncate_utf8_excerpt(
            row["body_md"].as<std::string>(),
            kDeletedContentExcerptBytes
        ),
        .deleted_by = row_deleted_by(row),
        .deleted_by_username = row_deleted_by_username(row),
        .deleted_at = row["deleted_at"].as<std::int64_t>()
    };
}

[[nodiscard]] models::DeletedPostSummary row_to_deleted_post(const drogon::orm::Row& row)
{
    return models::DeletedPostSummary{
        .id = row["id"].as<std::int64_t>(),
        .thread_id = row["thread_id"].as<std::int64_t>(),
        .thread_title = row["thread_title"].as<std::string>(),
        .author_id = row["author_id"].as<std::int64_t>(),
        .author_username = row["author_username"].as<std::string>(),
        .floor_no = row["floor_no"].as<std::int64_t>(),
        .body_excerpt = util::truncate_utf8_excerpt(
            row["body_md"].as<std::string>(),
            kDeletedContentExcerptBytes
        ),
        .deleted_by = row_deleted_by(row),
        .deleted_by_username = row_deleted_by_username(row),
        .deleted_at = row["deleted_at"].as<std::int64_t>()
    };
}

[[nodiscard]] models::DeletedSubPostSummary row_to_deleted_sub_post(const drogon::orm::Row& row)
{
    return models::DeletedSubPostSummary{
        .id = row["id"].as<std::int64_t>(),
        .post_id = row["post_id"].as<std::int64_t>(),
        .thread_id = row["thread_id"].as<std::int64_t>(),
        .thread_title = row["thread_title"].as<std::string>(),
        .author_id = row["author_id"].as<std::int64_t>(),
        .author_username = row["author_username"].as<std::string>(),
        .body_excerpt = util::truncate_utf8_excerpt(
            row["body_md"].as<std::string>(),
            kDeletedContentExcerptBytes
        ),
        .deleted_by = row_deleted_by(row),
        .deleted_by_username = row_deleted_by_username(row),
        .deleted_at = row["deleted_at"].as<std::int64_t>()
    };
}

}

AdminRepository::AdminRepository(std::string db_client_name)
    : db_client_name_{std::move(db_client_name)}
{
}

AdminRepository::AdminRepository(drogon::orm::DbClientPtr db_client)
    : db_client_{std::move(db_client)}
{
}

drogon::orm::DbClientPtr AdminRepository::client() const
{
    if(db_client_) {
        return db_client_;
    }
    return drogon::app().getDbClient(db_client_name_);
}

std::optional<models::Forum> AdminRepository::find_forum_by_id(std::int64_t forum_id) const
{
    const auto rows = client()->execSqlSync(
        "SELECT id, slug, name, description, sort_order, created_at, updated_at "
        "FROM forums WHERE id = ?",
        forum_id
    );
    if(rows.empty()) {
        return std::nullopt;
    }
    return row_to_forum(rows.at(0));
}

std::optional<models::Forum> AdminRepository::find_forum_by_slug(std::string_view slug) const
{
    const auto rows = client()->execSqlSync(
        "SELECT id, slug, name, description, sort_order, created_at, updated_at "
        "FROM forums WHERE slug = ? COLLATE NOCASE",
        std::string{slug}
    );
    if(rows.empty()) {
        return std::nullopt;
    }
    return row_to_forum(rows.at(0));
}

std::int64_t AdminRepository::create_forum(
    std::string_view slug,
    std::string_view name,
    std::string_view description,
    std::int64_t sort_order,
    std::int64_t now
) const
{
    const auto rows = client()->execSqlSync(
        "INSERT INTO forums (slug, name, description, sort_order, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?) RETURNING id",
        std::string{slug},
        std::string{name},
        std::string{description},
        sort_order,
        now,
        now
    );
    return rows.at(0)["id"].as<std::int64_t>();
}

bool AdminRepository::update_forum(
    std::int64_t forum_id,
    std::string_view slug,
    std::string_view name,
    std::string_view description,
    std::int64_t sort_order,
    std::int64_t now
) const
{
    const auto result = client()->execSqlSync(
        "UPDATE forums SET slug = ?, name = ?, description = ?, sort_order = ?, updated_at = ? "
        "WHERE id = ?",
        std::string{slug},
        std::string{name},
        std::string{description},
        sort_order,
        now,
        forum_id
    );
    return result.affectedRows() == 1;
}

bool AdminRepository::delete_forum(std::int64_t forum_id) const
{
    return client()->execSqlSync("DELETE FROM forums WHERE id = ?", forum_id).affectedRows() == 1;
}

std::int64_t AdminRepository::count_forum_threads(std::int64_t forum_id) const
{
    const auto rows = client()->execSqlSync(
        "SELECT COUNT(*) AS count FROM threads WHERE forum_id = ?",
        forum_id
    );
    return rows.at(0)["count"].as<std::int64_t>();
}

bool AdminRepository::set_thread_pinned(
    std::int64_t thread_id,
    bool is_pinned,
    std::int64_t now
) const
{
    const auto result = client()->execSqlSync(
        "UPDATE threads SET is_pinned = ?, updated_at = ? WHERE id = ?",
        is_pinned ? 1 : 0,
        now,
        thread_id
    );
    return result.affectedRows() == 1;
}

bool AdminRepository::set_thread_featured(
    std::int64_t thread_id,
    bool is_featured,
    std::int64_t now
) const
{
    const auto result = client()->execSqlSync(
        "UPDATE threads SET is_featured = ?, updated_at = ? WHERE id = ?",
        is_featured ? 1 : 0,
        now,
        thread_id
    );
    return result.affectedRows() == 1;
}

bool AdminRepository::set_thread_deleted(
    std::int64_t thread_id,
    bool is_deleted,
    std::int64_t admin_id,
    std::int64_t now
) const
{
    const auto result = is_deleted
        ? client()->execSqlSync(
            "UPDATE threads SET is_deleted = 1, deleted_by = ?, deleted_at = ?, updated_at = ? "
            "WHERE id = ?",
            admin_id,
            now,
            now,
            thread_id
        )
        : client()->execSqlSync(
            "UPDATE threads SET is_deleted = 0, deleted_by = NULL, deleted_at = NULL, updated_at = ? "
            "WHERE id = ?",
            now,
            thread_id
        );
    return result.affectedRows() == 1;
}

bool AdminRepository::set_post_deleted(
    std::int64_t post_id,
    bool is_deleted,
    std::int64_t admin_id,
    std::int64_t now
) const
{
    const auto result = is_deleted
        ? client()->execSqlSync(
            "UPDATE posts SET is_deleted = 1, deleted_by = ?, deleted_at = ?, updated_at = ? "
            "WHERE id = ?",
            admin_id,
            now,
            now,
            post_id
        )
        : client()->execSqlSync(
            "UPDATE posts SET is_deleted = 0, deleted_by = NULL, deleted_at = NULL, updated_at = ? "
            "WHERE id = ?",
            now,
            post_id
        );
    return result.affectedRows() == 1;
}

bool AdminRepository::set_sub_post_deleted(
    std::int64_t sub_post_id,
    bool is_deleted,
    std::int64_t admin_id,
    std::int64_t now
) const
{
    const auto result = is_deleted
        ? client()->execSqlSync(
            "UPDATE sub_posts SET is_deleted = 1, deleted_by = ?, deleted_at = ?, updated_at = ? "
            "WHERE id = ?",
            admin_id,
            now,
            now,
            sub_post_id
        )
        : client()->execSqlSync(
            "UPDATE sub_posts SET is_deleted = 0, deleted_by = NULL, deleted_at = NULL, updated_at = ? "
            "WHERE id = ?",
            now,
            sub_post_id
        );
    return result.affectedRows() == 1;
}

std::optional<std::int64_t> AdminRepository::thread_id_for_post(std::int64_t post_id) const
{
    const auto rows = client()->execSqlSync("SELECT thread_id FROM posts WHERE id = ?", post_id);
    if(rows.empty()) {
        return std::nullopt;
    }
    return rows.at(0)["thread_id"].as<std::int64_t>();
}

std::optional<std::int64_t> AdminRepository::thread_id_for_sub_post(
    std::int64_t sub_post_id
) const
{
    const auto rows = client()->execSqlSync(
        "SELECT p.thread_id FROM sub_posts sp JOIN posts p ON p.id = sp.post_id WHERE sp.id = ?",
        sub_post_id
    );
    if(rows.empty()) {
        return std::nullopt;
    }
    return rows.at(0)["thread_id"].as<std::int64_t>();
}

std::int64_t AdminRepository::count_users(const std::optional<models::UserRole>& role) const
{
    const auto rows = role.has_value()
        ? client()->execSqlSync(
            "SELECT COUNT(*) AS count FROM users WHERE role = ?",
            std::string{models::to_string(*role)}
        )
        : client()->execSqlSync("SELECT COUNT(*) AS count FROM users");
    return rows.at(0)["count"].as<std::int64_t>();
}

std::vector<models::User> AdminRepository::list_users(
    const std::optional<models::UserRole>& role,
    std::int64_t limit,
    std::int64_t offset
) const
{
    const auto rows = role.has_value()
        ? client()->execSqlSync(
            "SELECT " + std::string{kUserColumns} + " FROM users WHERE role = ? "
            "ORDER BY id ASC LIMIT ? OFFSET ?",
            std::string{models::to_string(*role)},
            limit,
            offset
        )
        : client()->execSqlSync(
            "SELECT " + std::string{kUserColumns} + " FROM users ORDER BY id ASC LIMIT ? OFFSET ?",
            limit,
            offset
        );

    std::vector<models::User> users;
    users.reserve(rows.size());
    for(const auto& row : rows) {
        users.push_back(row_to_user(row));
    }
    return users;
}

std::int64_t AdminRepository::count_admins() const
{
    const auto rows = client()->execSqlSync(
        "SELECT COUNT(*) AS count FROM users WHERE role = 'admin'"
    );
    return rows.at(0)["count"].as<std::int64_t>();
}

bool AdminRepository::update_user_role(
    std::int64_t user_id,
    models::UserRole role,
    std::int64_t now
) const
{
    const auto result = client()->execSqlSync(
        "UPDATE users SET role = ?, updated_at = ? WHERE id = ?",
        std::string{models::to_string(role)},
        now,
        user_id
    );
    return result.affectedRows() == 1;
}

bool AdminRepository::update_user_ban(
    std::int64_t user_id,
    const std::optional<std::int64_t>& banned_until,
    std::int64_t now
) const
{
    const auto result = banned_until.has_value()
        ? client()->execSqlSync(
            "UPDATE users SET banned_until = ?, updated_at = ? WHERE id = ?",
            *banned_until,
            now,
            user_id
        )
        : client()->execSqlSync(
            "UPDATE users SET banned_until = NULL, updated_at = ? WHERE id = ?",
            now,
            user_id
        );
    return result.affectedRows() == 1;
}

void AdminRepository::add_audit_log(
    std::int64_t admin_id,
    std::string_view action,
    std::string_view target_type,
    std::int64_t target_id,
    std::string_view detail,
    std::int64_t now
) const
{
    client()->execSqlSync(
        "INSERT INTO audit_log (admin_id, action, target_type, target_id, detail, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?)",
        admin_id,
        std::string{action},
        std::string{target_type},
        target_id,
        std::string{detail},
        now
    );
}

std::int64_t AdminRepository::count_audit_logs() const
{
    const auto rows = client()->execSqlSync("SELECT COUNT(*) AS count FROM audit_log");
    return rows.at(0)["count"].as<std::int64_t>();
}

std::vector<models::AuditLogEntry> AdminRepository::list_audit_logs(
    std::int64_t limit,
    std::int64_t offset
) const
{
    const auto rows = client()->execSqlSync(
        "SELECT id, admin_id, action, target_type, target_id, detail, created_at "
        "FROM audit_log ORDER BY id DESC LIMIT ? OFFSET ?",
        limit,
        offset
    );
    std::vector<models::AuditLogEntry> logs;
    logs.reserve(rows.size());
    for(const auto& row : rows) {
        logs.push_back(row_to_audit_log(row));
    }
    return logs;
}

std::int64_t AdminRepository::count_sessions(
    const std::optional<std::int64_t>& user_id_filter
) const
{
    const auto rows = user_id_filter.has_value()
        ? client()->execSqlSync(
            "SELECT COUNT(*) AS count FROM sessions WHERE user_id = ?",
            *user_id_filter
        )
        : client()->execSqlSync("SELECT COUNT(*) AS count FROM sessions");
    return rows.at(0)["count"].as<std::int64_t>();
}

std::vector<models::AdminSessionSummary> AdminRepository::list_sessions(
    const std::optional<std::int64_t>& user_id_filter,
    std::int64_t limit,
    std::int64_t offset
) const
{
    constexpr std::string_view select{
        "SELECT s.token_hash, s.user_id, u.username, s.created_at, s.expires_at, "
        "s.revoked_at, s.admin_confirmed_at, s.ip, s.user_agent "
        "FROM sessions s JOIN users u ON u.id = s.user_id "
    };
    const auto rows = user_id_filter.has_value()
        ? client()->execSqlSync(
            std::string{select} + "WHERE s.user_id = ? ORDER BY s.created_at DESC LIMIT ? OFFSET ?",
            *user_id_filter,
            limit,
            offset
        )
        : client()->execSqlSync(
            std::string{select} + "ORDER BY s.created_at DESC LIMIT ? OFFSET ?",
            limit,
            offset
        );

    std::vector<models::AdminSessionSummary> sessions;
    sessions.reserve(rows.size());
    for(const auto& row : rows) {
        sessions.push_back(row_to_session_summary(row));
    }
    return sessions;
}

std::int64_t AdminRepository::count_deleted_threads() const
{
    const auto rows = client()->execSqlSync(
        "SELECT COUNT(*) AS count FROM threads WHERE is_deleted = 1"
    );
    return rows.at(0)["count"].as<std::int64_t>();
}

std::vector<models::DeletedThreadSummary> AdminRepository::list_deleted_threads(
    std::int64_t limit,
    std::int64_t offset
) const
{
    const auto rows = client()->execSqlSync(
        "SELECT t.id, t.forum_id, f.slug AS forum_slug, f.name AS forum_name, "
        "t.author_id, u.username AS author_username, t.title, t.body_md, "
        "t.deleted_by, du.username AS deleted_by_username, t.deleted_at "
        "FROM threads t "
        "JOIN forums f ON f.id = t.forum_id "
        "JOIN users u ON u.id = t.author_id "
        "LEFT JOIN users du ON du.id = t.deleted_by "
        "WHERE t.is_deleted = 1 "
        "ORDER BY t.deleted_at DESC LIMIT ? OFFSET ?",
        limit,
        offset
    );
    std::vector<models::DeletedThreadSummary> threads;
    threads.reserve(rows.size());
    for(const auto& row : rows) {
        threads.push_back(row_to_deleted_thread(row));
    }
    return threads;
}

std::int64_t AdminRepository::count_deleted_posts() const
{
    const auto rows = client()->execSqlSync(
        "SELECT COUNT(*) AS count FROM posts WHERE is_deleted = 1"
    );
    return rows.at(0)["count"].as<std::int64_t>();
}

std::vector<models::DeletedPostSummary> AdminRepository::list_deleted_posts(
    std::int64_t limit,
    std::int64_t offset
) const
{
    const auto rows = client()->execSqlSync(
        "SELECT p.id, p.thread_id, t.title AS thread_title, "
        "p.author_id, u.username AS author_username, p.floor_no, p.body_md, "
        "p.deleted_by, du.username AS deleted_by_username, p.deleted_at "
        "FROM posts p "
        "JOIN threads t ON t.id = p.thread_id "
        "JOIN users u ON u.id = p.author_id "
        "LEFT JOIN users du ON du.id = p.deleted_by "
        "WHERE p.is_deleted = 1 "
        "ORDER BY p.deleted_at DESC LIMIT ? OFFSET ?",
        limit,
        offset
    );
    std::vector<models::DeletedPostSummary> posts;
    posts.reserve(rows.size());
    for(const auto& row : rows) {
        posts.push_back(row_to_deleted_post(row));
    }
    return posts;
}

std::int64_t AdminRepository::count_deleted_sub_posts() const
{
    const auto rows = client()->execSqlSync(
        "SELECT COUNT(*) AS count FROM sub_posts WHERE is_deleted = 1"
    );
    return rows.at(0)["count"].as<std::int64_t>();
}

std::vector<models::DeletedSubPostSummary> AdminRepository::list_deleted_sub_posts(
    std::int64_t limit,
    std::int64_t offset
) const
{
    const auto rows = client()->execSqlSync(
        "SELECT sp.id, sp.post_id, p.thread_id, t.title AS thread_title, "
        "sp.author_id, u.username AS author_username, sp.body_md, "
        "sp.deleted_by, du.username AS deleted_by_username, sp.deleted_at "
        "FROM sub_posts sp "
        "JOIN posts p ON p.id = sp.post_id "
        "JOIN threads t ON t.id = p.thread_id "
        "JOIN users u ON u.id = sp.author_id "
        "LEFT JOIN users du ON du.id = sp.deleted_by "
        "WHERE sp.is_deleted = 1 "
        "ORDER BY sp.deleted_at DESC LIMIT ? OFFSET ?",
        limit,
        offset
    );
    std::vector<models::DeletedSubPostSummary> sub_posts;
    sub_posts.reserve(rows.size());
    for(const auto& row : rows) {
        sub_posts.push_back(row_to_deleted_sub_post(row));
    }
    return sub_posts;
}

}
