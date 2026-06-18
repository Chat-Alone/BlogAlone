#include "repositories/forum_repository.h"

#include <drogon/drogon.h>

#include <utility>

namespace blogalone::repositories {
namespace {

constexpr const char* kThreadSelect =
    "SELECT t.id, t.forum_id, f.slug AS forum_slug, f.name AS forum_name, "
    "t.author_id, u.username AS author_username, t.title, t.body_md, t.body_html, "
    "t.is_pinned, t.is_featured, t.reply_count, t.last_reply_at, t.last_reply_user_id, "
    "t.created_at, t.updated_at "
    "FROM threads t "
    "JOIN forums f ON f.id = t.forum_id "
    "JOIN users u ON u.id = t.author_id ";

constexpr const char* kPostSelect =
    "SELECT p.id, p.thread_id, p.author_id, u.username AS author_username, p.floor_no, "
    "p.body_md, p.body_html, p.created_at, p.updated_at "
    "FROM posts p "
    "JOIN users u ON u.id = p.author_id "
    "JOIN threads t ON t.id = p.thread_id ";

constexpr const char* kSubPostSelect =
    "SELECT sp.id, sp.post_id, p.thread_id, sp.author_id, u.username AS author_username, "
    "sp.body_md, sp.body_html, sp.reply_to_user_id, reply_to.username AS reply_to_username, "
    "sp.created_at, sp.updated_at "
    "FROM sub_posts sp "
    "JOIN posts p ON p.id = sp.post_id "
    "JOIN threads t ON t.id = p.thread_id "
    "JOIN users u ON u.id = sp.author_id "
    "LEFT JOIN users reply_to ON reply_to.id = sp.reply_to_user_id ";

[[nodiscard]] std::optional<std::int64_t> optional_int64(const drogon::orm::Field& field)
{
    if(field.isNull()) {
        return std::nullopt;
    }
    return field.as<std::int64_t>();
}

[[nodiscard]] std::optional<std::string> optional_string(const drogon::orm::Field& field)
{
    if(field.isNull()) {
        return std::nullopt;
    }
    return field.as<std::string>();
}

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

[[nodiscard]] models::Thread row_to_thread(const drogon::orm::Row& row)
{
    return models::Thread{
        .id = row["id"].as<std::int64_t>(),
        .forum_id = row["forum_id"].as<std::int64_t>(),
        .forum_slug = row["forum_slug"].as<std::string>(),
        .forum_name = row["forum_name"].as<std::string>(),
        .author_id = row["author_id"].as<std::int64_t>(),
        .author_username = row["author_username"].as<std::string>(),
        .title = row["title"].as<std::string>(),
        .body_md = row["body_md"].as<std::string>(),
        .body_html = row["body_html"].as<std::string>(),
        .is_pinned = row["is_pinned"].as<int>() != 0,
        .is_featured = row["is_featured"].as<int>() != 0,
        .reply_count = row["reply_count"].as<std::int64_t>(),
        .last_reply_at = optional_int64(row["last_reply_at"]),
        .last_reply_user_id = optional_int64(row["last_reply_user_id"]),
        .created_at = row["created_at"].as<std::int64_t>(),
        .updated_at = row["updated_at"].as<std::int64_t>()
    };
}

[[nodiscard]] models::Post row_to_post(const drogon::orm::Row& row)
{
    return models::Post{
        .id = row["id"].as<std::int64_t>(),
        .thread_id = row["thread_id"].as<std::int64_t>(),
        .author_id = row["author_id"].as<std::int64_t>(),
        .author_username = row["author_username"].as<std::string>(),
        .floor_no = row["floor_no"].as<std::int64_t>(),
        .body_md = row["body_md"].as<std::string>(),
        .body_html = row["body_html"].as<std::string>(),
        .created_at = row["created_at"].as<std::int64_t>(),
        .updated_at = row["updated_at"].as<std::int64_t>()
    };
}

[[nodiscard]] models::SubPost row_to_sub_post(const drogon::orm::Row& row)
{
    return models::SubPost{
        .id = row["id"].as<std::int64_t>(),
        .post_id = row["post_id"].as<std::int64_t>(),
        .thread_id = row["thread_id"].as<std::int64_t>(),
        .author_id = row["author_id"].as<std::int64_t>(),
        .author_username = row["author_username"].as<std::string>(),
        .body_md = row["body_md"].as<std::string>(),
        .body_html = row["body_html"].as<std::string>(),
        .reply_to_user_id = optional_int64(row["reply_to_user_id"]),
        .reply_to_username = optional_string(row["reply_to_username"]),
        .created_at = row["created_at"].as<std::int64_t>(),
        .updated_at = row["updated_at"].as<std::int64_t>()
    };
}

}

ForumRepository::ForumRepository(std::string db_client_name)
    : db_client_name_{std::move(db_client_name)}
{
}

ForumRepository::ForumRepository(drogon::orm::DbClientPtr db_client)
    : db_client_{std::move(db_client)}
{
}

drogon::orm::DbClientPtr ForumRepository::client() const
{
    if(db_client_) {
        return db_client_;
    }
    return drogon::app().getDbClient(db_client_name_);
}

std::vector<models::Forum> ForumRepository::list_forums() const
{
    const auto rows = client()->execSqlSync(
        "SELECT id, slug, name, description, sort_order, created_at, updated_at "
        "FROM forums ORDER BY sort_order ASC, id ASC"
    );

    std::vector<models::Forum> forums;
    forums.reserve(rows.size());
    for(const auto& row : rows) {
        forums.push_back(row_to_forum(row));
    }
    return forums;
}

std::optional<models::Forum> ForumRepository::find_forum_by_slug(std::string_view slug) const
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

std::optional<models::Thread> ForumRepository::find_thread(std::int64_t thread_id) const
{
    const auto rows = client()->execSqlSync(
        std::string{kThreadSelect} + "WHERE t.id = ? AND t.is_deleted = 0",
        thread_id
    );
    if(rows.empty()) {
        return std::nullopt;
    }
    return row_to_thread(rows.at(0));
}

std::optional<models::Post> ForumRepository::find_post(std::int64_t post_id) const
{
    const auto rows = client()->execSqlSync(
        std::string{kPostSelect}
            + "WHERE p.id = ? AND p.is_deleted = 0 AND t.is_deleted = 0",
        post_id
    );
    if(rows.empty()) {
        return std::nullopt;
    }
    return row_to_post(rows.at(0));
}

std::optional<models::SubPost> ForumRepository::find_sub_post(std::int64_t sub_post_id) const
{
    const auto rows = client()->execSqlSync(
        std::string{kSubPostSelect}
            + "WHERE sp.id = ? AND sp.is_deleted = 0 AND p.is_deleted = 0 AND t.is_deleted = 0",
        sub_post_id
    );
    if(rows.empty()) {
        return std::nullopt;
    }
    return row_to_sub_post(rows.at(0));
}

bool ForumRepository::user_exists(std::int64_t user_id) const
{
    const auto rows = client()->execSqlSync(
        "SELECT 1 FROM users WHERE id = ?",
        user_id
    );
    return !rows.empty();
}

std::int64_t ForumRepository::count_threads(std::int64_t forum_id) const
{
    const auto rows = client()->execSqlSync(
        "SELECT COUNT(*) AS count FROM threads WHERE forum_id = ? AND is_deleted = 0",
        forum_id
    );
    return rows.at(0)["count"].as<std::int64_t>();
}

std::vector<models::Thread> ForumRepository::list_threads(
    std::int64_t forum_id,
    std::int64_t limit,
    std::int64_t offset
) const
{
    const auto rows = client()->execSqlSync(
        std::string{kThreadSelect}
            + "WHERE t.forum_id = ? AND t.is_deleted = 0 "
              "ORDER BY t.is_pinned DESC, COALESCE(t.last_reply_at, t.created_at) DESC, t.id DESC "
              "LIMIT ? OFFSET ?",
        forum_id,
        limit,
        offset
    );

    std::vector<models::Thread> threads;
    threads.reserve(rows.size());
    for(const auto& row : rows) {
        threads.push_back(row_to_thread(row));
    }
    return threads;
}

std::int64_t ForumRepository::count_posts(std::int64_t thread_id) const
{
    const auto rows = client()->execSqlSync(
        "SELECT COUNT(*) AS count FROM posts WHERE thread_id = ? AND is_deleted = 0",
        thread_id
    );
    return rows.at(0)["count"].as<std::int64_t>();
}

std::vector<models::Post> ForumRepository::list_posts(
    std::int64_t thread_id,
    std::int64_t limit,
    std::int64_t offset
) const
{
    const auto rows = client()->execSqlSync(
        std::string{kPostSelect}
            + "WHERE p.thread_id = ? AND p.is_deleted = 0 AND t.is_deleted = 0 "
              "ORDER BY p.floor_no ASC LIMIT ? OFFSET ?",
        thread_id,
        limit,
        offset
    );

    std::vector<models::Post> posts;
    posts.reserve(rows.size());
    for(const auto& row : rows) {
        posts.push_back(row_to_post(row));
    }
    return posts;
}

std::vector<models::SubPost> ForumRepository::list_sub_posts(std::int64_t post_id) const
{
    const auto rows = client()->execSqlSync(
        std::string{kSubPostSelect}
            + "WHERE sp.post_id = ? AND sp.is_deleted = 0 AND p.is_deleted = 0 AND t.is_deleted = 0 "
              "ORDER BY sp.created_at ASC, sp.id ASC",
        post_id
    );

    std::vector<models::SubPost> sub_posts;
    sub_posts.reserve(rows.size());
    for(const auto& row : rows) {
        sub_posts.push_back(row_to_sub_post(row));
    }
    return sub_posts;
}

std::int64_t ForumRepository::next_floor_no(std::int64_t thread_id) const
{
    const auto rows = client()->execSqlSync(
        "SELECT COALESCE(MAX(floor_no), 0) + 1 AS floor_no FROM posts WHERE thread_id = ?",
        thread_id
    );
    return rows.at(0)["floor_no"].as<std::int64_t>();
}

std::int64_t ForumRepository::create_thread(
    std::int64_t forum_id,
    std::int64_t author_id,
    std::string_view title,
    std::string_view body_md,
    std::string_view body_html,
    std::int64_t now
) const
{
    const auto rows = client()->execSqlSync(
        "INSERT INTO threads (forum_id, author_id, title, body_md, body_html, "
        "last_reply_at, last_reply_user_id, created_at, updated_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?) RETURNING id",
        forum_id,
        author_id,
        std::string{title},
        std::string{body_md},
        std::string{body_html},
        now,
        author_id,
        now,
        now
    );
    return rows.at(0)["id"].as<std::int64_t>();
}

std::optional<std::int64_t> ForumRepository::create_post(
    std::int64_t thread_id,
    std::int64_t author_id,
    std::int64_t floor_no,
    std::string_view body_md,
    std::string_view body_html,
    std::int64_t now
) const
{
    const auto rows = client()->execSqlSync(
        "INSERT INTO posts (thread_id, author_id, floor_no, body_md, body_html, created_at, updated_at) "
        "SELECT ?, ?, ?, ?, ?, ?, ? "
        "WHERE EXISTS (SELECT 1 FROM threads WHERE id = ? AND is_deleted = 0) "
        "RETURNING id",
        thread_id,
        author_id,
        floor_no,
        std::string{body_md},
        std::string{body_html},
        now,
        now,
        thread_id
    );
    if(rows.empty()) {
        return std::nullopt;
    }
    return rows.at(0)["id"].as<std::int64_t>();
}

std::optional<std::int64_t> ForumRepository::create_sub_post(
    std::int64_t post_id,
    std::int64_t author_id,
    std::string_view body_md,
    std::string_view body_html,
    const std::optional<std::int64_t>& reply_to_user_id,
    std::int64_t now
) const
{
    if(reply_to_user_id.has_value()) {
        const auto rows = client()->execSqlSync(
            "INSERT INTO sub_posts (post_id, author_id, body_md, body_html, reply_to_user_id, "
            "created_at, updated_at) "
            "SELECT ?, ?, ?, ?, ?, ?, ? "
            "WHERE EXISTS ("
            "SELECT 1 FROM posts p JOIN threads t ON t.id = p.thread_id "
            "WHERE p.id = ? AND p.is_deleted = 0 AND t.is_deleted = 0"
            ") RETURNING id",
            post_id,
            author_id,
            std::string{body_md},
            std::string{body_html},
            *reply_to_user_id,
            now,
            now,
            post_id
        );
        if(rows.empty()) {
            return std::nullopt;
        }
        return rows.at(0)["id"].as<std::int64_t>();
    }

    const auto rows = client()->execSqlSync(
        "INSERT INTO sub_posts (post_id, author_id, body_md, body_html, reply_to_user_id, "
        "created_at, updated_at) "
        "SELECT ?, ?, ?, ?, NULL, ?, ? "
        "WHERE EXISTS ("
        "SELECT 1 FROM posts p JOIN threads t ON t.id = p.thread_id "
        "WHERE p.id = ? AND p.is_deleted = 0 AND t.is_deleted = 0"
        ") RETURNING id",
        post_id,
        author_id,
        std::string{body_md},
        std::string{body_html},
        now,
        now,
        post_id
    );
    if(rows.empty()) {
        return std::nullopt;
    }
    return rows.at(0)["id"].as<std::int64_t>();
}

bool ForumRepository::increment_thread_reply_count(
    std::int64_t thread_id,
    std::int64_t last_reply_user_id,
    std::int64_t last_reply_at
) const
{
    const auto result = client()->execSqlSync(
        "UPDATE threads SET reply_count = reply_count + 1, last_reply_at = ?, "
        "last_reply_user_id = ? WHERE id = ? AND is_deleted = 0",
        last_reply_at,
        last_reply_user_id,
        thread_id
    );
    return result.affectedRows() == 1;
}

bool ForumRepository::update_thread_last_reply(
    std::int64_t thread_id,
    std::int64_t post_id,
    std::int64_t last_reply_user_id,
    std::int64_t last_reply_at
) const
{
    const auto result = client()->execSqlSync(
        "UPDATE threads SET last_reply_at = ?, last_reply_user_id = ? "
        "WHERE id = ? AND is_deleted = 0 "
        "AND EXISTS ("
        "SELECT 1 FROM posts WHERE id = ? AND thread_id = ? AND is_deleted = 0"
        ")",
        last_reply_at,
        last_reply_user_id,
        thread_id,
        post_id,
        thread_id
    );
    return result.affectedRows() == 1;
}

void ForumRepository::refresh_thread_reply_summary(std::int64_t thread_id) const
{
    const auto db = client();
    const auto count_rows = db->execSqlSync(
        "SELECT COUNT(*) AS count FROM posts WHERE thread_id = ? AND is_deleted = 0",
        thread_id
    );
    const auto activity_rows = db->execSqlSync(
        "SELECT activity_at, user_id FROM ("
        "SELECT t.created_at AS activity_at, t.author_id AS user_id, 0 AS source_order, t.id AS sequence "
        "FROM threads t WHERE t.id = ? AND t.is_deleted = 0 "
        "UNION ALL "
        "SELECT p.created_at AS activity_at, p.author_id AS user_id, 1 AS source_order, p.id AS sequence "
        "FROM posts p JOIN threads t ON t.id = p.thread_id "
        "WHERE p.thread_id = ? AND p.is_deleted = 0 AND t.is_deleted = 0 "
        "UNION ALL "
        "SELECT sp.created_at AS activity_at, sp.author_id AS user_id, 2 AS source_order, sp.id AS sequence "
        "FROM sub_posts sp "
        "JOIN posts p ON p.id = sp.post_id "
        "JOIN threads t ON t.id = p.thread_id "
        "WHERE p.thread_id = ? AND p.is_deleted = 0 AND sp.is_deleted = 0 AND t.is_deleted = 0"
        ") ORDER BY activity_at DESC, source_order DESC, sequence DESC LIMIT 1",
        thread_id,
        thread_id,
        thread_id
    );
    if(activity_rows.empty()) {
        return;
    }

    db->execSqlSync(
        "UPDATE threads SET reply_count = ?, last_reply_at = ?, last_reply_user_id = ? WHERE id = ?",
        count_rows.at(0)["count"].as<std::int64_t>(),
        activity_rows.at(0)["activity_at"].as<std::int64_t>(),
        activity_rows.at(0)["user_id"].as<std::int64_t>(),
        thread_id
    );
}

bool ForumRepository::update_thread_content(
    std::int64_t thread_id,
    std::string_view title,
    std::string_view body_md,
    std::string_view body_html,
    std::int64_t updated_at
) const
{
    const auto result = client()->execSqlSync(
        "UPDATE threads SET title = ?, body_md = ?, body_html = ?, updated_at = ? "
        "WHERE id = ? AND is_deleted = 0",
        std::string{title},
        std::string{body_md},
        std::string{body_html},
        updated_at,
        thread_id
    );
    return result.affectedRows() == 1;
}

bool ForumRepository::update_post_content(
    std::int64_t post_id,
    std::string_view body_md,
    std::string_view body_html,
    std::int64_t updated_at
) const
{
    const auto result = client()->execSqlSync(
        "UPDATE posts SET body_md = ?, body_html = ?, updated_at = ? "
        "WHERE id = ? AND is_deleted = 0 "
        "AND EXISTS (SELECT 1 FROM threads WHERE id = posts.thread_id AND is_deleted = 0)",
        std::string{body_md},
        std::string{body_html},
        updated_at,
        post_id
    );
    return result.affectedRows() == 1;
}

bool ForumRepository::update_sub_post_content(
    std::int64_t sub_post_id,
    std::string_view body_md,
    std::string_view body_html,
    std::int64_t updated_at
) const
{
    const auto result = client()->execSqlSync(
        "UPDATE sub_posts SET body_md = ?, body_html = ?, updated_at = ? "
        "WHERE id = ? AND is_deleted = 0 "
        "AND EXISTS ("
        "SELECT 1 FROM posts p JOIN threads t ON t.id = p.thread_id "
        "WHERE p.id = sub_posts.post_id AND p.is_deleted = 0 AND t.is_deleted = 0"
        ")",
        std::string{body_md},
        std::string{body_html},
        updated_at,
        sub_post_id
    );
    return result.affectedRows() == 1;
}

bool ForumRepository::soft_delete_thread(std::int64_t thread_id, std::int64_t deleted_at) const
{
    const auto result = client()->execSqlSync(
        "UPDATE threads SET is_deleted = 1, deleted_at = ?, updated_at = ? "
        "WHERE id = ? AND is_deleted = 0",
        deleted_at,
        deleted_at,
        thread_id
    );
    return result.affectedRows() == 1;
}

bool ForumRepository::soft_delete_post(std::int64_t post_id, std::int64_t deleted_at) const
{
    const auto result = client()->execSqlSync(
        "UPDATE posts SET is_deleted = 1, deleted_at = ?, updated_at = ? "
        "WHERE id = ? AND is_deleted = 0 "
        "AND EXISTS (SELECT 1 FROM threads WHERE id = posts.thread_id AND is_deleted = 0)",
        deleted_at,
        deleted_at,
        post_id
    );
    return result.affectedRows() == 1;
}

bool ForumRepository::soft_delete_sub_post(std::int64_t sub_post_id, std::int64_t deleted_at) const
{
    const auto result = client()->execSqlSync(
        "UPDATE sub_posts SET is_deleted = 1, deleted_at = ?, updated_at = ? "
        "WHERE id = ? AND is_deleted = 0 "
        "AND EXISTS ("
        "SELECT 1 FROM posts p JOIN threads t ON t.id = p.thread_id "
        "WHERE p.id = sub_posts.post_id AND p.is_deleted = 0 AND t.is_deleted = 0"
        ")",
        deleted_at,
        deleted_at,
        sub_post_id
    );
    return result.affectedRows() == 1;
}

}
