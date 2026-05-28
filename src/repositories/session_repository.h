#pragma once

#include "models/session.h"

#include <drogon/orm/DbClient.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace blogalone::repositories {

class SessionRepository {
  public:
    explicit SessionRepository(std::string db_client_name = "default");
    explicit SessionRepository(drogon::orm::DbClientPtr db_client);

    [[nodiscard]] std::optional<models::Session> find_by_token_hash(std::string_view token_hash) const;
    void create(const models::Session& session) const;
    void revoke(std::string_view token_hash, std::int64_t revoked_at) const;
    void revoke_all_for_user(std::int64_t user_id, std::int64_t revoked_at) const;

  private:
    [[nodiscard]] drogon::orm::DbClientPtr client() const;

    std::string db_client_name_;
    drogon::orm::DbClientPtr db_client_;
};

}
