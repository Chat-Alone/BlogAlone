#include "services/admin_service.h"

#include "db/transaction.h"
#include "repositories/forum_repository.h"
#include "repositories/session_repository.h"
#include "util/password.h"

#include <algorithm>
#include <cctype>
#include <utility>

namespace blogalone::services {
namespace {

constexpr std::int64_t kMaxPage = 1'000'000;
constexpr std::int64_t kMaxPageSize = 50;
constexpr std::int64_t kAdminReauthLifetimeSeconds = 600;
constexpr std::size_t kMinForumSlugLength = 2;
constexpr std::size_t kMaxForumSlugLength = 32;
constexpr std::size_t kMaxForumNameBytes = 240;
constexpr std::size_t kMaxForumDescriptionBytes = 4'000;
constexpr std::size_t kSessionHashLength = 64;

[[nodiscard]] std::string trim(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if(first == std::string_view::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string{value.substr(first, last - first + 1)};
}

[[nodiscard]] bool valid_slug(std::string_view slug)
{
    if(slug.size() < kMinForumSlugLength || slug.size() > kMaxForumSlugLength) {
        return false;
    }
    return std::ranges::all_of(slug, [](char ch) {
        const auto byte = static_cast<unsigned char>(ch);
        return std::islower(byte) != 0 || std::isdigit(byte) != 0 || ch == '-';
    });
}

[[nodiscard]] bool valid_forum_text(
    std::string_view name,
    std::string_view description
)
{
    return !name.empty()
        && name.size() <= kMaxForumNameBytes
        && description.size() <= kMaxForumDescriptionBytes;
}

[[nodiscard]] bool valid_pagination(AdminPaginationRequest pagination)
{
    return pagination.page >= 1
        && pagination.page <= kMaxPage
        && pagination.page_size >= 1
        && pagination.page_size <= kMaxPageSize;
}

[[nodiscard]] std::int64_t pagination_offset(AdminPaginationRequest pagination)
{
    return (pagination.page - 1) * pagination.page_size;
}

[[nodiscard]] bool is_admin(
    const repositories::UserRepository& users,
    std::int64_t admin_id
)
{
    const auto admin = users.find_by_id(admin_id);
    return admin.has_value() && admin->role == models::UserRole::admin;
}

[[nodiscard]] bool has_recent_reauth(
    const std::optional<std::int64_t>& confirmed_at,
    std::int64_t now
)
{
    return confirmed_at.has_value()
        && *confirmed_at <= now
        && now - *confirmed_at <= kAdminReauthLifetimeSeconds;
}

[[nodiscard]] bool valid_session_hash(std::string_view token_hash)
{
    return token_hash.size() == kSessionHashLength
        && std::ranges::all_of(token_hash, [](char ch) {
            const auto byte = static_cast<unsigned char>(ch);
            return std::isdigit(byte) != 0 || (ch >= 'a' && ch <= 'f');
        });
}

[[nodiscard]] std::string bool_detail(bool value)
{
    return value ? R"({"value":true})" : R"({"value":false})";
}

}

std::string_view to_string(AdminError error)
{
    switch(error) {
    case AdminError::invalid_input:
        return "invalid_input";
    case AdminError::not_found:
        return "not_found";
    case AdminError::forbidden:
        return "forbidden";
    case AdminError::conflict:
        return "conflict";
    case AdminError::reauth_required:
        return "admin_reauth_required";
    }
    return "invalid_input";
}

AdminService::AdminService(
    repositories::AdminRepository admin_repository,
    repositories::UserRepository user_repository
)
    : admin_repository_{std::move(admin_repository)}
    , user_repository_{std::move(user_repository)}
{
}

AdminResult<models::Forum> AdminService::create_forum(
    std::int64_t admin_id,
    const CreateForumRequest& request,
    std::int64_t now
) const
{
    const auto slug = trim(request.slug);
    const auto name = trim(request.name);
    const auto description = trim(request.description);
    if(!valid_slug(slug) || !valid_forum_text(name, description)) {
        return AdminError::invalid_input;
    }

    db::Transaction transaction{
        admin_repository_.client(),
        drogon::orm::TransactionType::Immediate
    };
    const auto db = transaction.client();
    const repositories::AdminRepository repository{db};
    const repositories::UserRepository users{db};
    if(!is_admin(users, admin_id)) {
        return AdminError::forbidden;
    }
    if(repository.find_forum_by_slug(slug).has_value()) {
        return AdminError::conflict;
    }

    const auto forum_id = repository.create_forum(
        slug,
        name,
        description,
        request.sort_order,
        now
    );
    repository.add_audit_log(admin_id, "forum.create", "forum", forum_id, "{}", now);
    const auto forum = repository.find_forum_by_id(forum_id);
    if(!forum.has_value()) {
        return AdminError::not_found;
    }
    transaction.commit();
    return *forum;
}

AdminResult<models::Forum> AdminService::update_forum(
    std::int64_t admin_id,
    std::int64_t forum_id,
    const UpdateForumRequest& request,
    std::int64_t now
) const
{
    if(!request.slug.has_value() && !request.name.has_value()
        && !request.description.has_value() && !request.sort_order.has_value()) {
        return AdminError::invalid_input;
    }

    db::Transaction transaction{
        admin_repository_.client(),
        drogon::orm::TransactionType::Immediate
    };
    const auto db = transaction.client();
    const repositories::AdminRepository repository{db};
    const repositories::UserRepository users{db};
    if(!is_admin(users, admin_id)) {
        return AdminError::forbidden;
    }
    const auto current = repository.find_forum_by_id(forum_id);
    if(!current.has_value()) {
        return AdminError::not_found;
    }

    const auto slug = request.slug.has_value() ? trim(*request.slug) : current->slug;
    const auto name = request.name.has_value() ? trim(*request.name) : current->name;
    const auto description = request.description.has_value()
        ? trim(*request.description)
        : current->description;
    const auto sort_order = request.sort_order.value_or(current->sort_order);
    if(!valid_slug(slug) || !valid_forum_text(name, description)) {
        return AdminError::invalid_input;
    }
    const auto duplicate = repository.find_forum_by_slug(slug);
    if(duplicate.has_value() && duplicate->id != forum_id) {
        return AdminError::conflict;
    }
    if(!repository.update_forum(forum_id, slug, name, description, sort_order, now)) {
        return AdminError::not_found;
    }
    repository.add_audit_log(admin_id, "forum.update", "forum", forum_id, "{}", now);
    const auto forum = repository.find_forum_by_id(forum_id);
    if(!forum.has_value()) {
        return AdminError::not_found;
    }
    transaction.commit();
    return *forum;
}

AdminResult<AdminDeleteResult> AdminService::delete_forum(
    std::int64_t admin_id,
    std::int64_t forum_id,
    std::int64_t now
) const
{
    db::Transaction transaction{
        admin_repository_.client(),
        drogon::orm::TransactionType::Immediate
    };
    const auto db = transaction.client();
    const repositories::AdminRepository repository{db};
    const repositories::UserRepository users{db};
    if(!is_admin(users, admin_id)) {
        return AdminError::forbidden;
    }
    if(!repository.find_forum_by_id(forum_id).has_value()) {
        return AdminError::not_found;
    }
    if(repository.count_forum_threads(forum_id) != 0) {
        return AdminError::conflict;
    }
    if(!repository.delete_forum(forum_id)) {
        return AdminError::not_found;
    }
    repository.add_audit_log(admin_id, "forum.delete", "forum", forum_id, "{}", now);
    transaction.commit();
    return AdminDeleteResult{};
}

AdminResult<AdminPage<models::User>> AdminService::list_users(
    std::int64_t admin_id,
    const std::optional<models::UserRole>& role,
    AdminPaginationRequest pagination
) const
{
    if(!valid_pagination(pagination)) {
        return AdminError::invalid_input;
    }
    if(!is_admin(user_repository_, admin_id)) {
        return AdminError::forbidden;
    }
    return AdminPage<models::User>{
        .items = admin_repository_.list_users(
            role,
            pagination.page_size,
            pagination_offset(pagination)
        ),
        .page = pagination.page,
        .page_size = pagination.page_size,
        .total = admin_repository_.count_users(role)
    };
}

AdminResult<AdminPage<models::AuditLogEntry>> AdminService::list_audit_logs(
    std::int64_t admin_id,
    AdminPaginationRequest pagination
) const
{
    if(!valid_pagination(pagination)) {
        return AdminError::invalid_input;
    }
    if(!is_admin(user_repository_, admin_id)) {
        return AdminError::forbidden;
    }
    return AdminPage<models::AuditLogEntry>{
        .items = admin_repository_.list_audit_logs(
            pagination.page_size,
            pagination_offset(pagination)
        ),
        .page = pagination.page,
        .page_size = pagination.page_size,
        .total = admin_repository_.count_audit_logs()
    };
}

AdminResult<ReauthResult> AdminService::reauth(
    std::int64_t admin_id,
    std::string_view session_token_hash,
    std::string_view password,
    std::int64_t now
) const
{
    if(password.empty() || !valid_session_hash(session_token_hash)) {
        return AdminError::invalid_input;
    }

    const auto db = admin_repository_.client();
    const repositories::UserRepository users{db};
    const auto admin = users.find_by_id(admin_id);
    if(!admin.has_value() || admin->role != models::UserRole::admin) {
        return AdminError::forbidden;
    }
    if(!util::verify_password(password, admin->pwd_hash)) {
        return AdminError::forbidden;
    }

    db::Transaction transaction{db, drogon::orm::TransactionType::Immediate};
    const auto transaction_client = transaction.client();
    const repositories::AdminRepository repository{transaction_client};
    const repositories::UserRepository transaction_users{transaction_client};
    const repositories::SessionRepository sessions{transaction_client};
    const auto current_admin = transaction_users.find_by_id(admin_id);
    if(!current_admin.has_value() || current_admin->role != models::UserRole::admin) {
        return AdminError::forbidden;
    }
    if(!sessions.confirm_admin(session_token_hash, admin_id, now)) {
        return AdminError::not_found;
    }
    repository.add_audit_log(admin_id, "admin.reauth", "user", admin_id, "{}", now);
    transaction.commit();
    return ReauthResult{.confirmed_at = now};
}

AdminResult<AdminState> AdminService::set_thread_pinned(
    std::int64_t admin_id,
    std::int64_t thread_id,
    bool is_pinned,
    std::int64_t now
) const
{
    db::Transaction transaction{
        admin_repository_.client(),
        drogon::orm::TransactionType::Immediate
    };
    const auto db = transaction.client();
    const repositories::AdminRepository repository{db};
    const repositories::UserRepository users{db};
    if(!is_admin(users, admin_id)) {
        return AdminError::forbidden;
    }
    if(!repository.set_thread_pinned(thread_id, is_pinned, now)) {
        return AdminError::not_found;
    }
    repository.add_audit_log(
        admin_id,
        "thread.pin",
        "thread",
        thread_id,
        bool_detail(is_pinned),
        now
    );
    transaction.commit();
    return AdminState{.id = thread_id, .value = is_pinned};
}

AdminResult<AdminState> AdminService::set_thread_featured(
    std::int64_t admin_id,
    std::int64_t thread_id,
    bool is_featured,
    std::int64_t now
) const
{
    db::Transaction transaction{
        admin_repository_.client(),
        drogon::orm::TransactionType::Immediate
    };
    const auto db = transaction.client();
    const repositories::AdminRepository repository{db};
    const repositories::UserRepository users{db};
    if(!is_admin(users, admin_id)) {
        return AdminError::forbidden;
    }
    if(!repository.set_thread_featured(thread_id, is_featured, now)) {
        return AdminError::not_found;
    }
    repository.add_audit_log(
        admin_id,
        "thread.feature",
        "thread",
        thread_id,
        bool_detail(is_featured),
        now
    );
    transaction.commit();
    return AdminState{.id = thread_id, .value = is_featured};
}

AdminResult<AdminState> AdminService::set_thread_deleted(
    std::int64_t admin_id,
    std::int64_t thread_id,
    bool is_deleted,
    std::int64_t now
) const
{
    db::Transaction transaction{
        admin_repository_.client(),
        drogon::orm::TransactionType::Immediate
    };
    const auto db = transaction.client();
    const repositories::AdminRepository repository{db};
    const repositories::UserRepository users{db};
    if(!is_admin(users, admin_id)) {
        return AdminError::forbidden;
    }
    if(!repository.set_thread_deleted(thread_id, is_deleted, admin_id, now)) {
        return AdminError::not_found;
    }
    repository.add_audit_log(
        admin_id,
        is_deleted ? "thread.delete" : "thread.restore",
        "thread",
        thread_id,
        bool_detail(is_deleted),
        now
    );
    transaction.commit();
    return AdminState{.id = thread_id, .value = is_deleted};
}

AdminResult<AdminState> AdminService::set_post_deleted(
    std::int64_t admin_id,
    std::int64_t post_id,
    bool is_deleted,
    std::int64_t now
) const
{
    db::Transaction transaction{
        admin_repository_.client(),
        drogon::orm::TransactionType::Immediate
    };
    const auto db = transaction.client();
    const repositories::AdminRepository repository{db};
    const repositories::UserRepository users{db};
    if(!is_admin(users, admin_id)) {
        return AdminError::forbidden;
    }
    const auto thread_id = repository.thread_id_for_post(post_id);
    if(!thread_id.has_value()) {
        return AdminError::not_found;
    }
    if(!repository.set_post_deleted(post_id, is_deleted, admin_id, now)) {
        return AdminError::not_found;
    }
    repositories::ForumRepository{db}.refresh_thread_reply_summary(*thread_id);
    repository.add_audit_log(
        admin_id,
        is_deleted ? "post.delete" : "post.restore",
        "post",
        post_id,
        bool_detail(is_deleted),
        now
    );
    transaction.commit();
    return AdminState{.id = post_id, .value = is_deleted};
}

AdminResult<AdminState> AdminService::set_sub_post_deleted(
    std::int64_t admin_id,
    std::int64_t sub_post_id,
    bool is_deleted,
    std::int64_t now
) const
{
    db::Transaction transaction{
        admin_repository_.client(),
        drogon::orm::TransactionType::Immediate
    };
    const auto db = transaction.client();
    const repositories::AdminRepository repository{db};
    const repositories::UserRepository users{db};
    if(!is_admin(users, admin_id)) {
        return AdminError::forbidden;
    }
    const auto thread_id = repository.thread_id_for_sub_post(sub_post_id);
    if(!thread_id.has_value()) {
        return AdminError::not_found;
    }
    if(!repository.set_sub_post_deleted(sub_post_id, is_deleted, admin_id, now)) {
        return AdminError::not_found;
    }
    repositories::ForumRepository{db}.refresh_thread_reply_summary(*thread_id);
    repository.add_audit_log(
        admin_id,
        is_deleted ? "sub_post.delete" : "sub_post.restore",
        "sub_post",
        sub_post_id,
        bool_detail(is_deleted),
        now
    );
    transaction.commit();
    return AdminState{.id = sub_post_id, .value = is_deleted};
}

AdminResult<models::User> AdminService::update_user_role(
    std::int64_t admin_id,
    const std::optional<std::int64_t>& admin_confirmed_at,
    std::int64_t user_id,
    models::UserRole role,
    std::int64_t now
) const
{
    if(!has_recent_reauth(admin_confirmed_at, now)) {
        return AdminError::reauth_required;
    }

    db::Transaction transaction{
        admin_repository_.client(),
        drogon::orm::TransactionType::Immediate
    };
    const auto db = transaction.client();
    const repositories::AdminRepository repository{db};
    const repositories::UserRepository users{db};
    const repositories::SessionRepository sessions{db};
    if(!is_admin(users, admin_id)) {
        return AdminError::forbidden;
    }
    const auto target = users.find_by_id(user_id);
    if(!target.has_value()) {
        return AdminError::not_found;
    }
    if(target->role == role) {
        transaction.commit();
        return *target;
    }
    if(user_id == admin_id && role != models::UserRole::admin) {
        return AdminError::forbidden;
    }
    if(target->role == models::UserRole::admin
        && role == models::UserRole::user
        && repository.count_admins() <= 1) {
        return AdminError::conflict;
    }
    if(!repository.update_user_role(user_id, role, now)) {
        return AdminError::not_found;
    }
    if(role == models::UserRole::user) {
        sessions.revoke_all_for_user(user_id, now);
    }
    repository.add_audit_log(
        admin_id,
        "user.role.change",
        "user",
        user_id,
        role == models::UserRole::admin
            ? R"({"role":"admin"})"
            : R"({"role":"user"})",
        now
    );
    const auto updated = users.find_by_id(user_id);
    if(!updated.has_value()) {
        return AdminError::not_found;
    }
    transaction.commit();
    return *updated;
}

AdminResult<models::User> AdminService::update_user_ban(
    std::int64_t admin_id,
    const std::optional<std::int64_t>& admin_confirmed_at,
    std::int64_t user_id,
    const std::optional<std::int64_t>& banned_until,
    std::int64_t now
) const
{
    if(!has_recent_reauth(admin_confirmed_at, now)) {
        return AdminError::reauth_required;
    }
    if(banned_until.has_value() && *banned_until <= now) {
        return AdminError::invalid_input;
    }
    if(user_id == admin_id) {
        return AdminError::forbidden;
    }

    db::Transaction transaction{
        admin_repository_.client(),
        drogon::orm::TransactionType::Immediate
    };
    const auto db = transaction.client();
    const repositories::AdminRepository repository{db};
    const repositories::UserRepository users{db};
    if(!is_admin(users, admin_id)) {
        return AdminError::forbidden;
    }
    if(!users.find_by_id(user_id).has_value()) {
        return AdminError::not_found;
    }
    if(!repository.update_user_ban(user_id, banned_until, now)) {
        return AdminError::not_found;
    }
    const auto detail = banned_until.has_value()
        ? std::string{R"({"banned_until":)"} + std::to_string(*banned_until) + "}"
        : std::string{R"({"banned_until":null})"};
    repository.add_audit_log(
        admin_id,
        banned_until.has_value() ? "user.ban" : "user.unban",
        "user",
        user_id,
        detail,
        now
    );
    const auto updated = users.find_by_id(user_id);
    if(!updated.has_value()) {
        return AdminError::not_found;
    }
    transaction.commit();
    return *updated;
}

AdminResult<AdminDeleteResult> AdminService::revoke_session(
    std::int64_t admin_id,
    const std::optional<std::int64_t>& admin_confirmed_at,
    std::string_view target_token_hash,
    std::int64_t now
) const
{
    if(!has_recent_reauth(admin_confirmed_at, now)) {
        return AdminError::reauth_required;
    }
    if(!valid_session_hash(target_token_hash)) {
        return AdminError::invalid_input;
    }

    db::Transaction transaction{
        admin_repository_.client(),
        drogon::orm::TransactionType::Immediate
    };
    const auto db = transaction.client();
    const repositories::AdminRepository repository{db};
    const repositories::UserRepository users{db};
    const repositories::SessionRepository sessions{db};
    if(!is_admin(users, admin_id)) {
        return AdminError::forbidden;
    }
    const auto target = sessions.find_by_token_hash(target_token_hash);
    if(!target.has_value()) {
        return AdminError::not_found;
    }
    sessions.revoke(target_token_hash, now);
    repository.add_audit_log(
        admin_id,
        "session.revoke",
        "user",
        target->user_id,
        "{}",
        now
    );
    transaction.commit();
    return AdminDeleteResult{};
}

}
