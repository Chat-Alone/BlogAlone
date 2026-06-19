#include "controllers/admin_controller.h"

#include "filters/session_filter.h"
#include "http/api_error.h"
#include "http/handler_guard.h"
#include "http/json_body.h"
#include "http/request_context.h"
#include "http/session_context.h"
#include "models/admin.h"
#include "models/forum.h"
#include "models/user.h"
#include "services/admin_service.h"
#include "util/time.h"

#include <drogon/drogon.h>
#include <json/value.h>

#include <charconv>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace blogalone::controllers {
namespace {

using HttpCallback = std::function<void(const drogon::HttpResponsePtr&)>;

[[nodiscard]] http::ErrorCode to_error_code(services::AdminError error)
{
    switch(error) {
    case services::AdminError::invalid_input:
        return http::ErrorCode::invalid_argument;
    case services::AdminError::not_found:
        return http::ErrorCode::not_found;
    case services::AdminError::forbidden:
        return http::ErrorCode::forbidden;
    case services::AdminError::conflict:
        return http::ErrorCode::conflict;
    case services::AdminError::reauth_required:
        return http::ErrorCode::admin_reauth_required;
    }
    return http::ErrorCode::internal_error;
}

[[nodiscard]] drogon::HttpResponsePtr error_response(
    const drogon::HttpRequestPtr& request,
    services::AdminError error
)
{
    return http::make_error_response(
        http::make_api_error(to_error_code(error), std::string{services::to_string(error)}),
        http::request_id_from(request)
    );
}

[[nodiscard]] drogon::HttpResponsePtr invalid_response(
    const drogon::HttpRequestPtr& request,
    std::string message
)
{
    return http::make_error_response(
        http::make_api_error(http::ErrorCode::invalid_argument, std::move(message)),
        http::request_id_from(request)
    );
}

[[nodiscard]] std::optional<std::int64_t> parse_int64(std::string_view value)
{
    if(value.empty()) {
        return std::nullopt;
    }
    std::int64_t parsed{};
    const auto* first = value.data();
    const auto* last = value.data() + value.size();
    const auto [ptr, error] = std::from_chars(first, last, parsed);
    if(error != std::errc{} || ptr != last) {
        return std::nullopt;
    }
    return parsed;
}

[[nodiscard]] services::AdminPaginationRequest pagination_from(
    const drogon::HttpRequestPtr& request
)
{
    const auto page_value = request->getParameter("page");
    const auto page_size_value = request->getParameter("page_size");
    return services::AdminPaginationRequest{
        .page = page_value.empty() ? 1 : parse_int64(page_value).value_or(0),
        .page_size = page_size_value.empty() ? 20 : parse_int64(page_size_value).value_or(0)
    };
}

[[nodiscard]] std::optional<http::SessionContext> admin_session(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback
)
{
    const auto session = http::session_context_of(request);
    if(!session.has_value() || session->role != models::UserRole::admin) {
        callback(http::make_error_response(
            http::make_api_error(http::ErrorCode::forbidden, "administrator access required"),
            http::request_id_from(request)
        ));
        return std::nullopt;
    }
    return session;
}

[[nodiscard]] std::optional<bool> required_bool(
    const Json::Value& body,
    std::string_view key,
    bool& type_error
)
{
    const auto* value = body.find(key.data(), key.data() + key.size());
    if(value == nullptr || !value->isBool()) {
        type_error = true;
        return std::nullopt;
    }
    return value->asBool();
}

[[nodiscard]] Json::Value forum_to_json(const models::Forum& forum)
{
    Json::Value json;
    json["id"] = static_cast<Json::Int64>(forum.id);
    json["slug"] = forum.slug;
    json["name"] = forum.name;
    json["description"] = forum.description;
    json["sort_order"] = static_cast<Json::Int64>(forum.sort_order);
    json["created_at"] = static_cast<Json::Int64>(forum.created_at);
    json["updated_at"] = static_cast<Json::Int64>(forum.updated_at);
    return json;
}

[[nodiscard]] Json::Value user_to_json(const models::User& user)
{
    Json::Value json;
    json["id"] = static_cast<Json::Int64>(user.id);
    json["username"] = user.username;
    json["role"] = std::string{models::to_string(user.role)};
    json["email"] = user.email.has_value() ? Json::Value{*user.email} : Json::Value{};
    json["banned_until"] = user.banned_until.has_value()
        ? Json::Value{static_cast<Json::Int64>(*user.banned_until)}
        : Json::Value{};
    json["avatar_url"] = user.avatar_url.has_value()
        ? Json::Value{*user.avatar_url}
        : Json::Value{};
    json["created_at"] = static_cast<Json::Int64>(user.created_at);
    json["updated_at"] = static_cast<Json::Int64>(user.updated_at);
    return json;
}

[[nodiscard]] Json::Value audit_log_to_json(const models::AuditLogEntry& entry)
{
    Json::Value json;
    json["id"] = static_cast<Json::Int64>(entry.id);
    json["admin_id"] = entry.admin_id.has_value()
        ? Json::Value{static_cast<Json::Int64>(*entry.admin_id)}
        : Json::Value{};
    json["action"] = entry.action;
    json["target_type"] = entry.target_type;
    json["target_id"] = static_cast<Json::Int64>(entry.target_id);
    json["detail"] = entry.detail;
    json["created_at"] = static_cast<Json::Int64>(entry.created_at);
    return json;
}

template <typename T, typename Converter>
[[nodiscard]] Json::Value page_to_json(
    const services::AdminPage<T>& page,
    Converter&& converter
)
{
    Json::Value json;
    json["items"] = Json::arrayValue;
    for(const auto& item : page.items) {
        json["items"].append(converter(item));
    }
    json["page"] = static_cast<Json::Int64>(page.page);
    json["page_size"] = static_cast<Json::Int64>(page.page_size);
    json["total"] = static_cast<Json::Int64>(page.total);
    return json;
}

void handle_create_forum(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback
)
{
    const auto session = admin_session(request, callback);
    if(!session.has_value()) {
        return;
    }
    const auto body = http::parse_json_body(request);
    if(!body || !body->isObject()) {
        callback(invalid_response(request, "invalid json body"));
        return;
    }
    bool type_error = false;
    services::CreateForumRequest creation{
        .slug = http::json_string(*body, "slug", type_error).value_or(""),
        .name = http::json_string(*body, "name", type_error).value_or(""),
        .description = http::json_string(*body, "description", type_error).value_or(""),
        .sort_order = http::json_int64(*body, "sort_order", type_error).value_or(0)
    };
    if(type_error) {
        callback(invalid_response(request, "invalid field type"));
        return;
    }
    const auto result = services::AdminService{}.create_forum(
        session->user_id,
        creation,
        util::utc_unix_seconds()
    );
    if(!result.has_value()) {
        callback(error_response(request, result.error()));
        return;
    }
    Json::Value response_body;
    response_body["forum"] = forum_to_json(*result);
    auto response = drogon::HttpResponse::newHttpJsonResponse(response_body);
    response->setStatusCode(drogon::k201Created);
    callback(response);
}

void handle_update_forum(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback,
    std::int64_t forum_id
)
{
    const auto session = admin_session(request, callback);
    if(!session.has_value()) {
        return;
    }
    const auto body = http::parse_json_body(request);
    if(!body || !body->isObject()) {
        callback(invalid_response(request, "invalid json body"));
        return;
    }
    bool type_error = false;
    services::UpdateForumRequest update{
        .slug = http::json_string(*body, "slug", type_error),
        .name = http::json_string(*body, "name", type_error),
        .description = http::json_string(*body, "description", type_error),
        .sort_order = http::json_int64(*body, "sort_order", type_error)
    };
    if(type_error) {
        callback(invalid_response(request, "invalid field type"));
        return;
    }
    const auto result = services::AdminService{}.update_forum(
        session->user_id,
        forum_id,
        update,
        util::utc_unix_seconds()
    );
    if(!result.has_value()) {
        callback(error_response(request, result.error()));
        return;
    }
    Json::Value response_body;
    response_body["forum"] = forum_to_json(*result);
    callback(drogon::HttpResponse::newHttpJsonResponse(response_body));
}

void handle_delete_forum(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback,
    std::int64_t forum_id
)
{
    const auto session = admin_session(request, callback);
    if(!session.has_value()) {
        return;
    }
    const auto result = services::AdminService{}.delete_forum(
        session->user_id,
        forum_id,
        util::utc_unix_seconds()
    );
    if(!result.has_value()) {
        callback(error_response(request, result.error()));
        return;
    }
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(drogon::k204NoContent);
    callback(response);
}

void handle_list_users(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback
)
{
    const auto session = admin_session(request, callback);
    if(!session.has_value()) {
        return;
    }
    std::optional<models::UserRole> role;
    const auto role_value = request->getParameter("role");
    if(!role_value.empty()) {
        if(role_value == "admin") {
            role = models::UserRole::admin;
        } else if(role_value == "user") {
            role = models::UserRole::user;
        } else {
            callback(invalid_response(request, "invalid role"));
            return;
        }
    }
    const auto result = services::AdminService{}.list_users(
        session->user_id,
        role,
        pagination_from(request)
    );
    if(!result.has_value()) {
        callback(error_response(request, result.error()));
        return;
    }
    callback(drogon::HttpResponse::newHttpJsonResponse(page_to_json(*result, user_to_json)));
}

void handle_reauth(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback
)
{
    const auto session = admin_session(request, callback);
    if(!session.has_value()) {
        return;
    }
    const auto body = http::parse_json_body(request);
    if(!body || !body->isObject()) {
        callback(invalid_response(request, "invalid json body"));
        return;
    }
    bool type_error = false;
    const auto password = http::json_string(*body, "password", type_error).value_or("");
    if(type_error) {
        callback(invalid_response(request, "invalid field type"));
        return;
    }
    const auto result = services::AdminService{}.reauth(
        session->user_id,
        session->token_hash,
        password,
        util::utc_unix_seconds()
    );
    if(!result.has_value()) {
        callback(error_response(request, result.error()));
        return;
    }
    Json::Value response_body;
    response_body["admin_confirmed_at"] = static_cast<Json::Int64>(result->confirmed_at);
    callback(drogon::HttpResponse::newHttpJsonResponse(response_body));
}

enum class ThreadFlag {
    pinned,
    featured,
    deleted
};

void handle_thread_flag(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback,
    std::int64_t thread_id,
    ThreadFlag flag
)
{
    const auto session = admin_session(request, callback);
    if(!session.has_value()) {
        return;
    }
    const auto body = http::parse_json_body(request);
    if(!body || !body->isObject()) {
        callback(invalid_response(request, "invalid json body"));
        return;
    }
    const auto field = flag == ThreadFlag::pinned
        ? std::string_view{"is_pinned"}
        : flag == ThreadFlag::featured
            ? std::string_view{"is_featured"}
            : std::string_view{"is_deleted"};
    bool type_error = false;
    const auto value = required_bool(*body, field, type_error);
    if(type_error || !value.has_value()) {
        callback(invalid_response(request, "invalid field type"));
        return;
    }
    const auto now = util::utc_unix_seconds();
    const auto service = services::AdminService{};
    const auto result = flag == ThreadFlag::pinned
        ? service.set_thread_pinned(session->user_id, thread_id, *value, now)
        : flag == ThreadFlag::featured
            ? service.set_thread_featured(session->user_id, thread_id, *value, now)
            : service.set_thread_deleted(session->user_id, thread_id, *value, now);
    if(!result.has_value()) {
        callback(error_response(request, result.error()));
        return;
    }
    Json::Value response_body;
    response_body["id"] = static_cast<Json::Int64>(result->id);
    response_body[std::string{field}] = result->value;
    callback(drogon::HttpResponse::newHttpJsonResponse(response_body));
}

void handle_content_deleted(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback,
    std::int64_t content_id,
    bool is_sub_post
)
{
    const auto session = admin_session(request, callback);
    if(!session.has_value()) {
        return;
    }
    const auto body = http::parse_json_body(request);
    if(!body || !body->isObject()) {
        callback(invalid_response(request, "invalid json body"));
        return;
    }
    bool type_error = false;
    const auto is_deleted = required_bool(*body, "is_deleted", type_error);
    if(type_error || !is_deleted.has_value()) {
        callback(invalid_response(request, "invalid field type"));
        return;
    }
    const auto service = services::AdminService{};
    const auto result = is_sub_post
        ? service.set_sub_post_deleted(
            session->user_id,
            content_id,
            *is_deleted,
            util::utc_unix_seconds()
        )
        : service.set_post_deleted(
            session->user_id,
            content_id,
            *is_deleted,
            util::utc_unix_seconds()
        );
    if(!result.has_value()) {
        callback(error_response(request, result.error()));
        return;
    }
    Json::Value response_body;
    response_body["id"] = static_cast<Json::Int64>(result->id);
    response_body["is_deleted"] = result->value;
    callback(drogon::HttpResponse::newHttpJsonResponse(response_body));
}

void handle_user_role(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback,
    std::int64_t user_id
)
{
    const auto session = admin_session(request, callback);
    if(!session.has_value()) {
        return;
    }
    const auto body = http::parse_json_body(request);
    if(!body || !body->isObject()) {
        callback(invalid_response(request, "invalid json body"));
        return;
    }
    bool type_error = false;
    const auto role_value = http::json_string(*body, "role", type_error).value_or("");
    if(type_error || (role_value != "admin" && role_value != "user")) {
        callback(invalid_response(request, "invalid role"));
        return;
    }
    const auto role = role_value == "admin"
        ? models::UserRole::admin
        : models::UserRole::user;
    const auto result = services::AdminService{}.update_user_role(
        session->user_id,
        session->admin_confirmed_at,
        user_id,
        role,
        util::utc_unix_seconds()
    );
    if(!result.has_value()) {
        callback(error_response(request, result.error()));
        return;
    }
    Json::Value response_body;
    response_body["user"] = user_to_json(*result);
    callback(drogon::HttpResponse::newHttpJsonResponse(response_body));
}

void handle_user_ban(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback,
    std::int64_t user_id
)
{
    const auto session = admin_session(request, callback);
    if(!session.has_value()) {
        return;
    }
    const auto body = http::parse_json_body(request);
    if(!body || !body->isObject() || !body->isMember("banned_until")) {
        callback(invalid_response(request, "banned_until is required"));
        return;
    }
    bool type_error = false;
    std::optional<std::int64_t> banned_until;
    if(!(*body)["banned_until"].isNull()) {
        banned_until = http::json_int64(*body, "banned_until", type_error);
    }
    if(type_error || (!(*body)["banned_until"].isNull() && !banned_until.has_value())) {
        callback(invalid_response(request, "invalid field type"));
        return;
    }
    const auto result = services::AdminService{}.update_user_ban(
        session->user_id,
        session->admin_confirmed_at,
        user_id,
        banned_until,
        util::utc_unix_seconds()
    );
    if(!result.has_value()) {
        callback(error_response(request, result.error()));
        return;
    }
    Json::Value response_body;
    response_body["user"] = user_to_json(*result);
    callback(drogon::HttpResponse::newHttpJsonResponse(response_body));
}

void handle_revoke_session(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback,
    std::string token_hash
)
{
    const auto session = admin_session(request, callback);
    if(!session.has_value()) {
        return;
    }
    const auto result = services::AdminService{}.revoke_session(
        session->user_id,
        session->admin_confirmed_at,
        token_hash,
        util::utc_unix_seconds()
    );
    if(!result.has_value()) {
        callback(error_response(request, result.error()));
        return;
    }
    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(drogon::k204NoContent);
    callback(response);
}

void handle_list_audit_logs(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback
)
{
    const auto session = admin_session(request, callback);
    if(!session.has_value()) {
        return;
    }
    const auto result = services::AdminService{}.list_audit_logs(
        session->user_id,
        pagination_from(request)
    );
    if(!result.has_value()) {
        callback(error_response(request, result.error()));
        return;
    }
    callback(drogon::HttpResponse::newHttpJsonResponse(page_to_json(*result, audit_log_to_json)));
}

[[nodiscard]] std::vector<drogon::internal::HttpConstraint> admin_constraints(
    drogon::HttpMethod method
)
{
    return {
        method,
        std::string{"blogalone::filters::SessionFilter"},
        std::string{"blogalone::filters::RequireAuthFilter"},
        std::string{"blogalone::filters::RequireAdminFilter"}
    };
}

}

void register_admin_routes()
{
    filters::ensure_session_filters_registered();

    drogon::app().registerHandler(
        "/api/admin/forums",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            http::run_guarded_request(request, callback, "admin.forums.create", [&]() {
                handle_create_forum(request, callback);
            });
        },
        admin_constraints(drogon::Post)
    );
    drogon::app().registerHandler(
        "/api/admin/forums/{1}",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback, std::int64_t forum_id) {
            http::run_guarded_request(request, callback, "admin.forums.update", [&]() {
                handle_update_forum(request, callback, forum_id);
            });
        },
        admin_constraints(drogon::Patch)
    );
    drogon::app().registerHandler(
        "/api/admin/forums/{1}",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback, std::int64_t forum_id) {
            http::run_guarded_request(request, callback, "admin.forums.delete", [&]() {
                handle_delete_forum(request, callback, forum_id);
            });
        },
        admin_constraints(drogon::Delete)
    );
    drogon::app().registerHandler(
        "/api/admin/users",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            http::run_guarded_request(request, callback, "admin.users.list", [&]() {
                handle_list_users(request, callback);
            });
        },
        admin_constraints(drogon::Get)
    );
    drogon::app().registerHandler(
        "/api/admin/reauth",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            http::run_guarded_request(request, callback, "admin.reauth", [&]() {
                handle_reauth(request, callback);
            });
        },
        admin_constraints(drogon::Post)
    );
    drogon::app().registerHandler(
        "/api/admin/threads/{1}/pin",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback, std::int64_t thread_id) {
            http::run_guarded_request(request, callback, "admin.threads.pin", [&]() {
                handle_thread_flag(request, callback, thread_id, ThreadFlag::pinned);
            });
        },
        admin_constraints(drogon::Patch)
    );
    drogon::app().registerHandler(
        "/api/admin/threads/{1}/feature",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback, std::int64_t thread_id) {
            http::run_guarded_request(request, callback, "admin.threads.feature", [&]() {
                handle_thread_flag(request, callback, thread_id, ThreadFlag::featured);
            });
        },
        admin_constraints(drogon::Patch)
    );
    drogon::app().registerHandler(
        "/api/admin/threads/{1}/delete",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback, std::int64_t thread_id) {
            http::run_guarded_request(request, callback, "admin.threads.delete", [&]() {
                handle_thread_flag(request, callback, thread_id, ThreadFlag::deleted);
            });
        },
        admin_constraints(drogon::Patch)
    );
    drogon::app().registerHandler(
        "/api/admin/posts/{1}/delete",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback, std::int64_t post_id) {
            http::run_guarded_request(request, callback, "admin.posts.delete", [&]() {
                handle_content_deleted(request, callback, post_id, false);
            });
        },
        admin_constraints(drogon::Patch)
    );
    drogon::app().registerHandler(
        "/api/admin/sub_posts/{1}/delete",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback, std::int64_t sub_post_id) {
            http::run_guarded_request(request, callback, "admin.sub_posts.delete", [&]() {
                handle_content_deleted(request, callback, sub_post_id, true);
            });
        },
        admin_constraints(drogon::Patch)
    );
    drogon::app().registerHandler(
        "/api/admin/users/{1}/role",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback, std::int64_t user_id) {
            http::run_guarded_request(request, callback, "admin.users.role", [&]() {
                handle_user_role(request, callback, user_id);
            });
        },
        admin_constraints(drogon::Patch)
    );
    drogon::app().registerHandler(
        "/api/admin/users/{1}/ban",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback, std::int64_t user_id) {
            http::run_guarded_request(request, callback, "admin.users.ban", [&]() {
                handle_user_ban(request, callback, user_id);
            });
        },
        admin_constraints(drogon::Patch)
    );
    drogon::app().registerHandler(
        "/api/admin/sessions/{1}",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback, std::string token_hash) {
            http::run_guarded_request(request, callback, "admin.sessions.revoke", [&]() {
                handle_revoke_session(request, callback, std::move(token_hash));
            });
        },
        admin_constraints(drogon::Delete)
    );
    drogon::app().registerHandler(
        "/api/admin/audit_logs",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            http::run_guarded_request(request, callback, "admin.audit_logs.list", [&]() {
                handle_list_audit_logs(request, callback);
            });
        },
        admin_constraints(drogon::Get)
    );
}

}
