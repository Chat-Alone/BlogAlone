#include "repositories/user_repository.h"

#include <drogon/drogon.h>

#include <utility>

namespace blogalone::repositories {
namespace {

[[nodiscard]] models::User row_to_user(const drogon::orm::Row& row)
{
    return models::User{
        .id = row["id"].as<std::int64_t>(),
        .username = row["username"].as<std::string>(),
        .email = row["email"].isNull() ? std::nullopt : std::optional{row["email"].as<std::string>()},
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

constexpr const char* kSelectColumns =
    "SELECT id, username, email, pwd_hash, role, banned_until, created_at, updated_at, avatar_url "
    "FROM users";

}

UserRepository::UserRepository(std::string db_client_name)
    : db_client_name_{std::move(db_client_name)}
{
}

UserRepository::UserRepository(drogon::orm::DbClientPtr db_client)
    : db_client_{std::move(db_client)}
{
}

drogon::orm::DbClientPtr UserRepository::client() const
{
    if(db_client_) {
        return db_client_;
    }
    return drogon::app().getDbClient(db_client_name_);
}

std::optional<models::User> UserRepository::find_by_id(std::int64_t user_id) const
{
    const auto db = client();
    const auto rows = db->execSqlSync(
        std::string{kSelectColumns} + " WHERE id = ?",
        user_id
    );
    if(rows.empty()) {
        return std::nullopt;
    }
    return row_to_user(rows.at(0));
}

std::optional<models::User> UserRepository::find_by_username(std::string_view username) const
{
    const auto db = client();
    const auto rows = db->execSqlSync(
        std::string{kSelectColumns} + " WHERE username = ? COLLATE NOCASE",
        std::string{username}
    );
    if(rows.empty()) {
        return std::nullopt;
    }
    return row_to_user(rows.at(0));
}

std::optional<models::User> UserRepository::find_by_email(std::string_view email) const
{
    const auto db = client();
    const auto rows = db->execSqlSync(
        std::string{kSelectColumns} + " WHERE email = ? COLLATE NOCASE",
        std::string{email}
    );
    if(rows.empty()) {
        return std::nullopt;
    }
    return row_to_user(rows.at(0));
}

std::int64_t UserRepository::create(
    std::string_view username,
    const std::optional<std::string>& email,
    std::string_view pwd_hash,
    std::int64_t created_at
) const
{
    const auto db = client();
    if(email.has_value()) {
        const auto rows = db->execSqlSync(
            "INSERT INTO users (username, email, pwd_hash, created_at, updated_at) "
            "VALUES (?, ?, ?, ?, ?) RETURNING id",
            std::string{username},
            *email,
            std::string{pwd_hash},
            created_at,
            created_at
        );
        return rows.at(0)["id"].as<std::int64_t>();
    }

    const auto rows = db->execSqlSync(
        "INSERT INTO users (username, email, pwd_hash, created_at, updated_at) "
        "VALUES (?, NULL, ?, ?, ?) RETURNING id",
        std::string{username},
        std::string{pwd_hash},
        created_at,
        created_at
    );
    return rows.at(0)["id"].as<std::int64_t>();
}

void UserRepository::update_profile(
    std::int64_t user_id,
    const std::optional<std::string>& email,
    const std::optional<std::string>& avatar_url,
    std::int64_t updated_at
) const
{
    const auto db = client();
    if(email.has_value() && avatar_url.has_value()) {
        db->execSqlSync(
            "UPDATE users SET email = ?, avatar_url = ?, updated_at = ? WHERE id = ?",
            *email, *avatar_url, updated_at, user_id
        );
    } else if(email.has_value()) {
        db->execSqlSync(
            "UPDATE users SET email = ?, updated_at = ? WHERE id = ?",
            *email, updated_at, user_id
        );
    } else if(avatar_url.has_value()) {
        db->execSqlSync(
            "UPDATE users SET avatar_url = ?, updated_at = ? WHERE id = ?",
            *avatar_url, updated_at, user_id
        );
    }
}

void UserRepository::update_password(
    std::int64_t user_id,
    std::string_view pwd_hash,
    std::int64_t updated_at
) const
{
    const auto db = client();
    db->execSqlSync(
        "UPDATE users SET pwd_hash = ?, updated_at = ? WHERE id = ?",
        std::string{pwd_hash},
        updated_at,
        user_id
    );
}

}
