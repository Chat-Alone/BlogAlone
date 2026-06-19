#include "repositories/session_repository.h"

#include <drogon/drogon.h>

#include <utility>

namespace blogalone::repositories {

SessionRepository::SessionRepository(std::string db_client_name)
    : db_client_name_{std::move(db_client_name)}
{
}

SessionRepository::SessionRepository(drogon::orm::DbClientPtr db_client)
    : db_client_{std::move(db_client)}
{
}

drogon::orm::DbClientPtr SessionRepository::client() const
{
    if(db_client_) {
        return db_client_;
    }
    return drogon::app().getDbClient(db_client_name_);
}

std::optional<models::Session> SessionRepository::find_by_token_hash(std::string_view token_hash) const
{
    const auto db = client();
    const auto rows = db->execSqlSync(
        "SELECT token_hash, user_id, csrf_hash, created_at, expires_at, revoked_at, "
        "admin_confirmed_at, ip, user_agent FROM sessions WHERE token_hash = ?",
        std::string{token_hash}
    );
    if(rows.empty()) {
        return std::nullopt;
    }

    const auto& row = rows.at(0);
    return models::Session{
        .token_hash = row["token_hash"].as<std::string>(),
        .user_id = row["user_id"].as<std::int64_t>(),
        .csrf_hash = row["csrf_hash"].as<std::string>(),
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

void SessionRepository::create(const models::Session& session) const
{
    const auto db = client();
    db->execSqlSync(
        "INSERT INTO sessions (token_hash, user_id, csrf_hash, created_at, expires_at, ip, user_agent) "
        "VALUES (?, ?, ?, ?, ?, ?, ?)",
        session.token_hash,
        session.user_id,
        session.csrf_hash,
        session.created_at,
        session.expires_at,
        session.ip,
        session.user_agent
    );
}

void SessionRepository::revoke(std::string_view token_hash, std::int64_t revoked_at) const
{
    const auto db = client();
    db->execSqlSync(
        "UPDATE sessions SET revoked_at = ? WHERE token_hash = ? AND revoked_at IS NULL",
        revoked_at,
        std::string{token_hash}
    );
}

void SessionRepository::revoke_all_for_user(std::int64_t user_id, std::int64_t revoked_at) const
{
    const auto db = client();
    db->execSqlSync(
        "UPDATE sessions SET revoked_at = ? WHERE user_id = ? AND revoked_at IS NULL",
        revoked_at,
        user_id
    );
}

bool SessionRepository::confirm_admin(
    std::string_view token_hash,
    std::int64_t user_id,
    std::int64_t confirmed_at
) const
{
    const auto db = client();
    const auto result = db->execSqlSync(
        "UPDATE sessions SET admin_confirmed_at = ? "
        "WHERE token_hash = ? AND user_id = ? AND revoked_at IS NULL AND expires_at > ?",
        confirmed_at,
        std::string{token_hash},
        user_id,
        confirmed_at
    );
    return result.affectedRows() == 1;
}

}
