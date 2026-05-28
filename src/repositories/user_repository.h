#pragma once

#include "models/user.h"

#include <drogon/orm/DbClient.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace blogalone::repositories {

class UserRepository {
  public:
    explicit UserRepository(std::string db_client_name = "default");
    explicit UserRepository(drogon::orm::DbClientPtr db_client);

    [[nodiscard]] std::optional<models::User> find_by_id(std::int64_t user_id) const;
    [[nodiscard]] std::optional<models::User> find_by_username(std::string_view username) const;
    [[nodiscard]] std::optional<models::User> find_by_email(std::string_view email) const;
    [[nodiscard]] std::int64_t create(
        std::string_view username,
        const std::optional<std::string>& email,
        std::string_view pwd_hash,
        std::int64_t created_at
    ) const;
    void update_profile(
        std::int64_t user_id,
        const std::optional<std::string>& email,
        const std::optional<std::string>& avatar_url,
        std::int64_t updated_at
    ) const;
    void update_password(std::int64_t user_id, std::string_view pwd_hash, std::int64_t updated_at) const;

  private:
    [[nodiscard]] drogon::orm::DbClientPtr client() const;

    std::string db_client_name_;
    drogon::orm::DbClientPtr db_client_;
};

}
