#pragma once

#include "models/admin.h"
#include "models/forum.h"
#include "models/user.h"
#include "repositories/admin_repository.h"
#include "repositories/user_repository.h"
#include "util/pagination.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace blogalone::services {

struct AdminPaginationRequest {
    std::int64_t page{1};
    std::int64_t page_size{util::kDefaultPageSize};
};

template <typename T>
struct AdminPage {
    std::vector<T> items;
    std::int64_t page{};
    std::int64_t page_size{};
    std::int64_t total{};
};

struct CreateForumRequest {
    std::string slug;
    std::string name;
    std::string description;
    std::int64_t sort_order{};
};

struct UpdateForumRequest {
    std::optional<std::string> slug;
    std::optional<std::string> name;
    std::optional<std::string> description;
    std::optional<std::int64_t> sort_order;
};

struct AdminState {
    std::int64_t id{};
    bool value{};
};

struct ReauthResult {
    std::int64_t confirmed_at{};
};

struct AdminDeleteResult {
};

enum class AdminError {
    invalid_input,
    not_found,
    forbidden,
    conflict,
    reauth_required
};

[[nodiscard]] std::string_view to_string(AdminError error);

template <typename T>
class AdminResult {
  public:
    AdminResult(T value) : storage_{std::move(value)} {}
    AdminResult(AdminError error) : storage_{error} {}

    [[nodiscard]] bool has_value() const { return storage_.index() == 0; }
    [[nodiscard]] explicit operator bool() const { return has_value(); }
    [[nodiscard]] const T& value() const { return std::get<0>(storage_); }
    [[nodiscard]] T& value() { return std::get<0>(storage_); }
    [[nodiscard]] AdminError error() const { return std::get<1>(storage_); }
    [[nodiscard]] const T& operator*() const { return value(); }
    [[nodiscard]] T& operator*() { return value(); }
    [[nodiscard]] const T* operator->() const { return &value(); }
    [[nodiscard]] T* operator->() { return &value(); }

  private:
    std::variant<T, AdminError> storage_;
};

class AdminService {
  public:
    AdminService(
        repositories::AdminRepository admin_repository = repositories::AdminRepository{},
        repositories::UserRepository user_repository = repositories::UserRepository{}
    );

    [[nodiscard]] AdminResult<models::Forum> create_forum(
        std::int64_t admin_id,
        const CreateForumRequest& request,
        std::int64_t now
    ) const;
    [[nodiscard]] AdminResult<models::Forum> update_forum(
        std::int64_t admin_id,
        std::int64_t forum_id,
        const UpdateForumRequest& request,
        std::int64_t now
    ) const;
    [[nodiscard]] AdminResult<AdminDeleteResult> delete_forum(
        std::int64_t admin_id,
        std::int64_t forum_id,
        std::int64_t now
    ) const;

    [[nodiscard]] AdminResult<AdminPage<models::User>> list_users(
        std::int64_t admin_id,
        const std::optional<models::UserRole>& role,
        AdminPaginationRequest pagination
    ) const;
    [[nodiscard]] AdminResult<AdminPage<models::AuditLogEntry>> list_audit_logs(
        std::int64_t admin_id,
        AdminPaginationRequest pagination
    ) const;

    [[nodiscard]] AdminResult<ReauthResult> reauth(
        std::int64_t admin_id,
        std::string_view session_token_hash,
        std::string_view password,
        std::int64_t now
    ) const;

    [[nodiscard]] AdminResult<AdminState> set_thread_pinned(
        std::int64_t admin_id,
        std::int64_t thread_id,
        bool is_pinned,
        std::int64_t now
    ) const;
    [[nodiscard]] AdminResult<AdminState> set_thread_featured(
        std::int64_t admin_id,
        std::int64_t thread_id,
        bool is_featured,
        std::int64_t now
    ) const;
    [[nodiscard]] AdminResult<AdminState> set_thread_deleted(
        std::int64_t admin_id,
        std::int64_t thread_id,
        bool is_deleted,
        std::int64_t now
    ) const;
    [[nodiscard]] AdminResult<AdminState> set_post_deleted(
        std::int64_t admin_id,
        std::int64_t post_id,
        bool is_deleted,
        std::int64_t now
    ) const;
    [[nodiscard]] AdminResult<AdminState> set_sub_post_deleted(
        std::int64_t admin_id,
        std::int64_t sub_post_id,
        bool is_deleted,
        std::int64_t now
    ) const;

    [[nodiscard]] AdminResult<models::User> update_user_role(
        std::int64_t admin_id,
        const std::optional<std::int64_t>& admin_confirmed_at,
        std::int64_t user_id,
        models::UserRole role,
        std::int64_t now
    ) const;
    [[nodiscard]] AdminResult<models::User> update_user_ban(
        std::int64_t admin_id,
        const std::optional<std::int64_t>& admin_confirmed_at,
        std::int64_t user_id,
        const std::optional<std::int64_t>& banned_until,
        std::int64_t now
    ) const;
    [[nodiscard]] AdminResult<AdminDeleteResult> revoke_session(
        std::int64_t admin_id,
        const std::optional<std::int64_t>& admin_confirmed_at,
        std::string_view target_token_hash,
        std::int64_t now
    ) const;

  private:
    repositories::AdminRepository admin_repository_;
    repositories::UserRepository user_repository_;
};

}
