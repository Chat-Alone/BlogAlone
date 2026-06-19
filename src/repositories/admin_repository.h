#pragma once

#include "models/admin.h"
#include "models/forum.h"
#include "models/user.h"

#include <drogon/orm/DbClient.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace blogalone::repositories {

class AdminRepository {
  public:
    explicit AdminRepository(std::string db_client_name = "default");
    explicit AdminRepository(drogon::orm::DbClientPtr db_client);

    [[nodiscard]] drogon::orm::DbClientPtr client() const;
    [[nodiscard]] std::optional<models::Forum> find_forum_by_id(std::int64_t forum_id) const;
    [[nodiscard]] std::optional<models::Forum> find_forum_by_slug(std::string_view slug) const;
    [[nodiscard]] std::int64_t create_forum(
        std::string_view slug,
        std::string_view name,
        std::string_view description,
        std::int64_t sort_order,
        std::int64_t now
    ) const;
    [[nodiscard]] bool update_forum(
        std::int64_t forum_id,
        std::string_view slug,
        std::string_view name,
        std::string_view description,
        std::int64_t sort_order,
        std::int64_t now
    ) const;
    [[nodiscard]] bool delete_forum(std::int64_t forum_id) const;
    [[nodiscard]] std::int64_t count_forum_threads(std::int64_t forum_id) const;

    [[nodiscard]] bool set_thread_pinned(
        std::int64_t thread_id,
        bool is_pinned,
        std::int64_t now
    ) const;
    [[nodiscard]] bool set_thread_featured(
        std::int64_t thread_id,
        bool is_featured,
        std::int64_t now
    ) const;
    [[nodiscard]] bool set_thread_deleted(
        std::int64_t thread_id,
        bool is_deleted,
        std::int64_t admin_id,
        std::int64_t now
    ) const;
    [[nodiscard]] bool set_post_deleted(
        std::int64_t post_id,
        bool is_deleted,
        std::int64_t admin_id,
        std::int64_t now
    ) const;
    [[nodiscard]] bool set_sub_post_deleted(
        std::int64_t sub_post_id,
        bool is_deleted,
        std::int64_t admin_id,
        std::int64_t now
    ) const;
    [[nodiscard]] std::optional<std::int64_t> thread_id_for_post(std::int64_t post_id) const;
    [[nodiscard]] std::optional<std::int64_t> thread_id_for_sub_post(
        std::int64_t sub_post_id
    ) const;

    [[nodiscard]] std::int64_t count_users(const std::optional<models::UserRole>& role) const;
    [[nodiscard]] std::vector<models::User> list_users(
        const std::optional<models::UserRole>& role,
        std::int64_t limit,
        std::int64_t offset
    ) const;
    [[nodiscard]] std::int64_t count_admins() const;
    [[nodiscard]] bool update_user_role(
        std::int64_t user_id,
        models::UserRole role,
        std::int64_t now
    ) const;
    [[nodiscard]] bool update_user_ban(
        std::int64_t user_id,
        const std::optional<std::int64_t>& banned_until,
        std::int64_t now
    ) const;

    void add_audit_log(
        std::int64_t admin_id,
        std::string_view action,
        std::string_view target_type,
        std::int64_t target_id,
        std::string_view detail,
        std::int64_t now
    ) const;
    [[nodiscard]] std::int64_t count_audit_logs() const;
    [[nodiscard]] std::vector<models::AuditLogEntry> list_audit_logs(
        std::int64_t limit,
        std::int64_t offset
    ) const;

  private:
    std::string db_client_name_;
    drogon::orm::DbClientPtr db_client_;
};

}
