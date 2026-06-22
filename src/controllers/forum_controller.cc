#include "controllers/forum_controller.h"

#include "config/app_config.h"
#include "filters/session_filter.h"
#include "http/api_error.h"
#include "http/client_ip.h"
#include "http/handler_guard.h"
#include "http/json_body.h"
#include "http/request_context.h"
#include "http/session_context.h"
#include "models/forum.h"
#include "security/rate_limiter.h"
#include "services/forum_service.h"
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

[[nodiscard]] http::ErrorCode to_error_code(services::ForumError error)
{
    switch(error) {
    case services::ForumError::invalid_input:
        return http::ErrorCode::invalid_argument;
    case services::ForumError::not_found:
        return http::ErrorCode::not_found;
    case services::ForumError::forbidden:
        return http::ErrorCode::forbidden;
    case services::ForumError::conflict:
        return http::ErrorCode::conflict;
    }
    return http::ErrorCode::internal_error;
}

[[nodiscard]] drogon::HttpResponsePtr error_response(
    const drogon::HttpRequestPtr& request,
    services::ForumError error
)
{
    return http::make_error_response(
        http::make_api_error(to_error_code(error), std::string{services::to_string(error)}),
        http::request_id_from(request)
    );
}

[[nodiscard]] drogon::HttpResponsePtr error_response(
    const drogon::HttpRequestPtr& request,
    http::ErrorCode code,
    std::string message
)
{
    return http::make_error_response(
        http::make_api_error(code, std::move(message)),
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

[[nodiscard]] std::optional<std::int64_t> query_int64(
    const drogon::HttpRequestPtr& request,
    std::string_view key,
    std::int64_t fallback
)
{
    const auto value = request->getParameter(std::string{key});
    if(value.empty()) {
        return fallback;
    }
    return parse_int64(value);
}

[[nodiscard]] std::optional<services::PaginationRequest> pagination_from(
    const drogon::HttpRequestPtr& request
)
{
    const auto page = query_int64(request, "page", 1);
    const auto page_size = query_int64(request, "page_size", services::kDefaultPageSize);
    if(!page.has_value() || !page_size.has_value()) {
        return std::nullopt;
    }
    return services::PaginationRequest{
        .page = *page,
        .page_size = *page_size
    };
}

[[nodiscard]] Json::Value user_ref_json(std::int64_t id, std::string_view username)
{
    Json::Value json;
    json["id"] = static_cast<Json::Int64>(id);
    json["username"] = std::string{username};
    return json;
}

[[nodiscard]] Json::Value nullable_int64(const std::optional<std::int64_t>& value)
{
    if(!value.has_value()) {
        return Json::Value{};
    }
    return static_cast<Json::Int64>(*value);
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

[[nodiscard]] Json::Value thread_to_json(const models::Thread& thread)
{
    Json::Value json;
    json["id"] = static_cast<Json::Int64>(thread.id);
    json["forum"]["id"] = static_cast<Json::Int64>(thread.forum_id);
    json["forum"]["slug"] = thread.forum_slug;
    json["forum"]["name"] = thread.forum_name;
    json["author"] = user_ref_json(thread.author_id, thread.author_username);
    json["title"] = thread.title;
    json["body_md"] = thread.body_md;
    json["body_html"] = thread.body_html;
    json["is_pinned"] = thread.is_pinned;
    json["is_featured"] = thread.is_featured;
    json["reply_count"] = static_cast<Json::Int64>(thread.reply_count);
    json["last_reply_at"] = nullable_int64(thread.last_reply_at);
    json["last_reply_user_id"] = nullable_int64(thread.last_reply_user_id);
    json["created_at"] = static_cast<Json::Int64>(thread.created_at);
    json["updated_at"] = static_cast<Json::Int64>(thread.updated_at);
    return json;
}

[[nodiscard]] Json::Value sub_post_to_json(const models::SubPost& sub_post)
{
    Json::Value json;
    json["id"] = static_cast<Json::Int64>(sub_post.id);
    json["post_id"] = static_cast<Json::Int64>(sub_post.post_id);
    json["thread_id"] = static_cast<Json::Int64>(sub_post.thread_id);
    json["author"] = user_ref_json(sub_post.author_id, sub_post.author_username);
    json["body_md"] = sub_post.body_md;
    json["body_html"] = sub_post.body_html;
    json["reply_to_user_id"] = nullable_int64(sub_post.reply_to_user_id);
    if(sub_post.reply_to_username.has_value()) {
        json["reply_to_username"] = *sub_post.reply_to_username;
    } else {
        json["reply_to_username"] = Json::Value{};
    }
    json["created_at"] = static_cast<Json::Int64>(sub_post.created_at);
    json["updated_at"] = static_cast<Json::Int64>(sub_post.updated_at);
    return json;
}

[[nodiscard]] Json::Value post_to_json(const models::PostWithReplies& post)
{
    Json::Value json;
    json["id"] = static_cast<Json::Int64>(post.post.id);
    json["thread_id"] = static_cast<Json::Int64>(post.post.thread_id);
    json["author"] = user_ref_json(post.post.author_id, post.post.author_username);
    json["floor_no"] = static_cast<Json::Int64>(post.post.floor_no);
    json["body_md"] = post.post.body_md;
    json["body_html"] = post.post.body_html;
    json["created_at"] = static_cast<Json::Int64>(post.post.created_at);
    json["updated_at"] = static_cast<Json::Int64>(post.post.updated_at);
    json["sub_posts"] = Json::arrayValue;
    for(const auto& sub_post : post.sub_posts) {
        json["sub_posts"].append(sub_post_to_json(sub_post));
    }
    return json;
}

template <typename T, typename Converter>
[[nodiscard]] Json::Value page_to_json(const services::Page<T>& page, Converter&& converter)
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

[[nodiscard]] std::optional<std::int64_t> current_user_id(
    const drogon::HttpRequestPtr& request
)
{
    const auto session = http::session_context_of(request);
    if(!session.has_value()) {
        return std::nullopt;
    }
    return session->user_id;
}

[[nodiscard]] bool consume_post_rate_limit(
    const drogon::HttpRequestPtr& request,
    std::int64_t user_id
)
{
    const auto& app_config = config::app_config_from_drogon();
    return security::request_rate_limiter().consume(
        security::RateLimitScope::post,
        http::client_ip_from(request),
        user_id,
        app_config.rate_limits.post
    );
}

void handle_list_forums(const drogon::HttpRequestPtr&, const HttpCallback& callback)
{
    Json::Value body;
    body["items"] = Json::arrayValue;
    for(const auto& forum : services::ForumService{}.list_forums()) {
        body["items"].append(forum_to_json(forum));
    }
    callback(drogon::HttpResponse::newHttpJsonResponse(body));
}

void handle_list_threads(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback,
    const std::string& slug
)
{
    const auto pagination = pagination_from(request);
    if(!pagination.has_value()) {
        callback(error_response(request, http::ErrorCode::invalid_argument, "invalid pagination"));
        return;
    }
    const auto result = services::ForumService{}.list_threads(slug, *pagination);
    if(!result.has_value()) {
        callback(error_response(request, result.error()));
        return;
    }

    const auto body = page_to_json(*result, thread_to_json);
    callback(drogon::HttpResponse::newHttpJsonResponse(body));
}

void handle_get_thread(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback,
    std::int64_t thread_id
)
{
    const auto pagination = pagination_from(request);
    if(!pagination.has_value()) {
        callback(error_response(request, http::ErrorCode::invalid_argument, "invalid pagination"));
        return;
    }
    const auto result = services::ForumService{}.get_thread(thread_id, *pagination);
    if(!result.has_value()) {
        callback(error_response(request, result.error()));
        return;
    }

    Json::Value body;
    body["thread"] = thread_to_json(result->thread);
    body["posts"] = page_to_json(result->posts, post_to_json);
    callback(drogon::HttpResponse::newHttpJsonResponse(body));
}

void handle_create_thread(const drogon::HttpRequestPtr& request, const HttpCallback& callback)
{
    const auto user_id = current_user_id(request);
    if(!user_id.has_value()) {
        callback(error_response(request, http::ErrorCode::unauthenticated, "authentication required"));
        return;
    }
    if(!consume_post_rate_limit(request, *user_id)) {
        callback(error_response(request, http::ErrorCode::rate_limited, "rate limit exceeded"));
        return;
    }

    const auto body = http::parse_json_body(request);
    if(!body) {
        callback(error_response(request, http::ErrorCode::invalid_argument, "invalid json body"));
        return;
    }

    bool type_error = false;
    const services::CreateThreadRequest create{
        .forum_slug = http::json_string(*body, "forum_slug", type_error).value_or(""),
        .title = http::json_string(*body, "title", type_error).value_or(""),
        .body_md = http::json_string(*body, "body_md", type_error).value_or("")
    };
    if(type_error) {
        callback(error_response(request, http::ErrorCode::invalid_argument, "invalid field type"));
        return;
    }

    const auto result = services::ForumService{}.create_thread(
        *user_id,
        create,
        util::utc_unix_seconds()
    );
    if(!result.has_value()) {
        callback(error_response(request, result.error()));
        return;
    }

    Json::Value response_body;
    response_body["thread"] = thread_to_json(*result);
    auto response = drogon::HttpResponse::newHttpJsonResponse(response_body);
    response->setStatusCode(drogon::k201Created);
    callback(response);
}

void handle_update_thread(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback,
    std::int64_t thread_id
)
{
    const auto user_id = current_user_id(request);
    if(!user_id.has_value()) {
        callback(error_response(request, http::ErrorCode::unauthenticated, "authentication required"));
        return;
    }

    const auto body = http::parse_json_body(request);
    if(!body) {
        callback(error_response(request, http::ErrorCode::invalid_argument, "invalid json body"));
        return;
    }

    bool type_error = false;
    const services::UpdateThreadRequest update{
        .title = http::json_string(*body, "title", type_error).value_or(""),
        .body_md = http::json_string(*body, "body_md", type_error).value_or("")
    };
    if(type_error) {
        callback(error_response(request, http::ErrorCode::invalid_argument, "invalid field type"));
        return;
    }

    const auto result = services::ForumService{}.update_thread(
        *user_id,
        thread_id,
        update,
        util::utc_unix_seconds()
    );
    if(!result.has_value()) {
        callback(error_response(request, result.error()));
        return;
    }

    Json::Value response_body;
    response_body["thread"] = thread_to_json(*result);
    callback(drogon::HttpResponse::newHttpJsonResponse(response_body));
}

void handle_delete_thread(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback,
    std::int64_t thread_id
)
{
    const auto user_id = current_user_id(request);
    if(!user_id.has_value()) {
        callback(error_response(request, http::ErrorCode::unauthenticated, "authentication required"));
        return;
    }

    const auto result = services::ForumService{}.delete_thread(
        *user_id,
        thread_id,
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

void handle_create_post(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback,
    std::int64_t thread_id
)
{
    const auto user_id = current_user_id(request);
    if(!user_id.has_value()) {
        callback(error_response(request, http::ErrorCode::unauthenticated, "authentication required"));
        return;
    }
    if(!consume_post_rate_limit(request, *user_id)) {
        callback(error_response(request, http::ErrorCode::rate_limited, "rate limit exceeded"));
        return;
    }

    const auto body = http::parse_json_body(request);
    if(!body) {
        callback(error_response(request, http::ErrorCode::invalid_argument, "invalid json body"));
        return;
    }

    bool type_error = false;
    const services::CreatePostRequest create{
        .thread_id = thread_id,
        .body_md = http::json_string(*body, "body_md", type_error).value_or("")
    };
    if(type_error) {
        callback(error_response(request, http::ErrorCode::invalid_argument, "invalid field type"));
        return;
    }

    const auto result = services::ForumService{}.create_post(
        *user_id,
        create,
        util::utc_unix_seconds()
    );
    if(!result.has_value()) {
        callback(error_response(request, result.error()));
        return;
    }

    Json::Value response_body;
    response_body["post"] = post_to_json(*result);
    auto response = drogon::HttpResponse::newHttpJsonResponse(response_body);
    response->setStatusCode(drogon::k201Created);
    callback(response);
}

void handle_update_post(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback,
    std::int64_t post_id
)
{
    const auto user_id = current_user_id(request);
    if(!user_id.has_value()) {
        callback(error_response(request, http::ErrorCode::unauthenticated, "authentication required"));
        return;
    }

    const auto body = http::parse_json_body(request);
    if(!body) {
        callback(error_response(request, http::ErrorCode::invalid_argument, "invalid json body"));
        return;
    }

    bool type_error = false;
    const services::UpdatePostRequest update{
        .body_md = http::json_string(*body, "body_md", type_error).value_or("")
    };
    if(type_error) {
        callback(error_response(request, http::ErrorCode::invalid_argument, "invalid field type"));
        return;
    }

    const auto result = services::ForumService{}.update_post(
        *user_id,
        post_id,
        update,
        util::utc_unix_seconds()
    );
    if(!result.has_value()) {
        callback(error_response(request, result.error()));
        return;
    }

    Json::Value response_body;
    response_body["post"] = post_to_json(*result);
    callback(drogon::HttpResponse::newHttpJsonResponse(response_body));
}

void handle_delete_post(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback,
    std::int64_t post_id
)
{
    const auto user_id = current_user_id(request);
    if(!user_id.has_value()) {
        callback(error_response(request, http::ErrorCode::unauthenticated, "authentication required"));
        return;
    }

    const auto result = services::ForumService{}.delete_post(
        *user_id,
        post_id,
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

void handle_create_sub_post(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback,
    std::int64_t post_id
)
{
    const auto user_id = current_user_id(request);
    if(!user_id.has_value()) {
        callback(error_response(request, http::ErrorCode::unauthenticated, "authentication required"));
        return;
    }
    if(!consume_post_rate_limit(request, *user_id)) {
        callback(error_response(request, http::ErrorCode::rate_limited, "rate limit exceeded"));
        return;
    }

    const auto body = http::parse_json_body(request);
    if(!body) {
        callback(error_response(request, http::ErrorCode::invalid_argument, "invalid json body"));
        return;
    }

    bool type_error = false;
    const services::CreateSubPostRequest create{
        .post_id = post_id,
        .body_md = http::json_string(*body, "body_md", type_error).value_or(""),
        .reply_to_user_id = http::json_int64(*body, "reply_to_user_id", type_error)
    };
    if(type_error) {
        callback(error_response(request, http::ErrorCode::invalid_argument, "invalid field type"));
        return;
    }

    const auto result = services::ForumService{}.create_sub_post(
        *user_id,
        create,
        util::utc_unix_seconds()
    );
    if(!result.has_value()) {
        callback(error_response(request, result.error()));
        return;
    }

    Json::Value response_body;
    response_body["sub_post"] = sub_post_to_json(*result);
    auto response = drogon::HttpResponse::newHttpJsonResponse(response_body);
    response->setStatusCode(drogon::k201Created);
    callback(response);
}

void handle_update_sub_post(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback,
    std::int64_t sub_post_id
)
{
    const auto user_id = current_user_id(request);
    if(!user_id.has_value()) {
        callback(error_response(request, http::ErrorCode::unauthenticated, "authentication required"));
        return;
    }

    const auto body = http::parse_json_body(request);
    if(!body) {
        callback(error_response(request, http::ErrorCode::invalid_argument, "invalid json body"));
        return;
    }

    bool type_error = false;
    const services::UpdateSubPostRequest update{
        .body_md = http::json_string(*body, "body_md", type_error).value_or("")
    };
    if(type_error) {
        callback(error_response(request, http::ErrorCode::invalid_argument, "invalid field type"));
        return;
    }

    const auto result = services::ForumService{}.update_sub_post(
        *user_id,
        sub_post_id,
        update,
        util::utc_unix_seconds()
    );
    if(!result.has_value()) {
        callback(error_response(request, result.error()));
        return;
    }

    Json::Value response_body;
    response_body["sub_post"] = sub_post_to_json(*result);
    callback(drogon::HttpResponse::newHttpJsonResponse(response_body));
}

void handle_delete_sub_post(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback,
    std::int64_t sub_post_id
)
{
    const auto user_id = current_user_id(request);
    if(!user_id.has_value()) {
        callback(error_response(request, http::ErrorCode::unauthenticated, "authentication required"));
        return;
    }

    const auto result = services::ForumService{}.delete_sub_post(
        *user_id,
        sub_post_id,
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

}

void register_forum_routes()
{
    filters::ensure_session_filters_registered();

    drogon::app().registerHandler(
        "/api/forums",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            http::run_guarded_request(request, callback, "forums.list", [&]() {
                handle_list_forums(request, callback);
            });
        },
        {drogon::Get}
    );

    drogon::app().registerHandler(
        "/api/forums/{1}/threads",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback, std::string slug) {
            http::run_guarded_request(request, callback, "forums.threads", [&]() {
                handle_list_threads(request, callback, slug);
            });
        },
        {drogon::Get}
    );

    drogon::app().registerHandler(
        "/api/threads/{1}",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback, std::int64_t thread_id) {
            http::run_guarded_request(request, callback, "threads.get", [&]() {
                handle_get_thread(request, callback, thread_id);
            });
        },
        {drogon::Get}
    );

    drogon::app().registerHandler(
        "/api/threads",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            http::run_guarded_request(request, callback, "threads.create", [&]() {
                handle_create_thread(request, callback);
            });
        },
        {drogon::Post,
         std::string{"blogalone::filters::SessionFilter"},
         std::string{"blogalone::filters::RequireAuthFilter"}}
    );

    drogon::app().registerHandler(
        "/api/threads/{1}",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback, std::int64_t thread_id) {
            http::run_guarded_request(request, callback, "threads.update", [&]() {
                handle_update_thread(request, callback, thread_id);
            });
        },
        {drogon::Patch,
         std::string{"blogalone::filters::SessionFilter"},
         std::string{"blogalone::filters::RequireAuthFilter"}}
    );

    drogon::app().registerHandler(
        "/api/threads/{1}",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback, std::int64_t thread_id) {
            http::run_guarded_request(request, callback, "threads.delete", [&]() {
                handle_delete_thread(request, callback, thread_id);
            });
        },
        {drogon::Delete,
         std::string{"blogalone::filters::SessionFilter"},
         std::string{"blogalone::filters::RequireAuthFilter"}}
    );

    drogon::app().registerHandler(
        "/api/threads/{1}/posts",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback, std::int64_t thread_id) {
            http::run_guarded_request(request, callback, "posts.create", [&]() {
                handle_create_post(request, callback, thread_id);
            });
        },
        {drogon::Post,
         std::string{"blogalone::filters::SessionFilter"},
         std::string{"blogalone::filters::RequireAuthFilter"}}
    );

    drogon::app().registerHandler(
        "/api/posts/{1}",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback, std::int64_t post_id) {
            http::run_guarded_request(request, callback, "posts.update", [&]() {
                handle_update_post(request, callback, post_id);
            });
        },
        {drogon::Patch,
         std::string{"blogalone::filters::SessionFilter"},
         std::string{"blogalone::filters::RequireAuthFilter"}}
    );

    drogon::app().registerHandler(
        "/api/posts/{1}",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback, std::int64_t post_id) {
            http::run_guarded_request(request, callback, "posts.delete", [&]() {
                handle_delete_post(request, callback, post_id);
            });
        },
        {drogon::Delete,
         std::string{"blogalone::filters::SessionFilter"},
         std::string{"blogalone::filters::RequireAuthFilter"}}
    );

    drogon::app().registerHandler(
        "/api/posts/{1}/sub_posts",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback, std::int64_t post_id) {
            http::run_guarded_request(request, callback, "sub_posts.create", [&]() {
                handle_create_sub_post(request, callback, post_id);
            });
        },
        {drogon::Post,
         std::string{"blogalone::filters::SessionFilter"},
         std::string{"blogalone::filters::RequireAuthFilter"}}
    );

    drogon::app().registerHandler(
        "/api/sub_posts/{1}",
        [](const drogon::HttpRequestPtr& request,
           HttpCallback&& callback,
           std::int64_t sub_post_id) {
            http::run_guarded_request(request, callback, "sub_posts.update", [&]() {
                handle_update_sub_post(request, callback, sub_post_id);
            });
        },
        {drogon::Patch,
         std::string{"blogalone::filters::SessionFilter"},
         std::string{"blogalone::filters::RequireAuthFilter"}}
    );

    drogon::app().registerHandler(
        "/api/sub_posts/{1}",
        [](const drogon::HttpRequestPtr& request,
           HttpCallback&& callback,
           std::int64_t sub_post_id) {
            http::run_guarded_request(request, callback, "sub_posts.delete", [&]() {
                handle_delete_sub_post(request, callback, sub_post_id);
            });
        },
        {drogon::Delete,
         std::string{"blogalone::filters::SessionFilter"},
         std::string{"blogalone::filters::RequireAuthFilter"}}
    );
}

}
