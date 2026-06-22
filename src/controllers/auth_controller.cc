#include "controllers/auth_controller.h"

#include "config/app_config.h"
#include "filters/session_filter.h"
#include "http/api_error.h"
#include "http/client_ip.h"
#include "http/handler_guard.h"
#include "http/json_body.h"
#include "http/request_context.h"
#include "http/session_context.h"
#include "models/user.h"
#include "security/rate_limiter.h"
#include "services/auth_service.h"
#include "util/time.h"

#include <drogon/drogon.h>
#include <json/value.h>

#include <functional>
#include <optional>
#include <string>
#include <utility>

namespace blogalone::controllers {
namespace {

using HttpCallback = std::function<void(const drogon::HttpResponsePtr&)>;

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

[[nodiscard]] http::ErrorCode to_error_code(services::AuthError error)
{
    switch(error) {
    case services::AuthError::invalid_input:
    case services::AuthError::invalid_credentials:
        return http::ErrorCode::invalid_argument;
    case services::AuthError::username_taken:
    case services::AuthError::email_taken:
        return http::ErrorCode::conflict;
    case services::AuthError::user_banned:
        return http::ErrorCode::forbidden;
    case services::AuthError::not_found:
        return http::ErrorCode::not_found;
    }
    return http::ErrorCode::internal_error;
}

[[nodiscard]] Json::Value current_user_to_json(const models::User& user)
{
    Json::Value json;
    json["id"] = static_cast<Json::Int64>(user.id);
    json["username"] = user.username;
    json["role"] = std::string{models::to_string(user.role)};
    json["created_at"] = static_cast<Json::Int64>(user.created_at);
    json["updated_at"] = static_cast<Json::Int64>(user.updated_at);
    if(user.email.has_value()) {
        json["email"] = *user.email;
    } else {
        json["email"] = Json::Value{};
    }
    if(user.avatar_url.has_value()) {
        json["avatar_url"] = *user.avatar_url;
    } else {
        json["avatar_url"] = Json::Value{};
    }
    return json;
}

void set_session_cookies(
    const drogon::HttpResponsePtr& response,
    const services::AuthIssued& issued,
    int ttl_seconds
)
{
    drogon::Cookie session_cookie{
        std::string{http::session_cookie_name},
        issued.session_token
    };
    session_cookie.setHttpOnly(true);
    session_cookie.setSecure(true);
    session_cookie.setSameSite(drogon::Cookie::SameSite::kLax);
    session_cookie.setPath("/");
    session_cookie.setMaxAge(ttl_seconds);
    response->addCookie(std::move(session_cookie));

    drogon::Cookie csrf_cookie{
        std::string{http::csrf_cookie_name},
        issued.csrf_token
    };
    csrf_cookie.setHttpOnly(false);
    csrf_cookie.setSecure(true);
    csrf_cookie.setSameSite(drogon::Cookie::SameSite::kLax);
    csrf_cookie.setPath("/");
    csrf_cookie.setMaxAge(ttl_seconds);
    response->addCookie(std::move(csrf_cookie));
}

void clear_session_cookies(const drogon::HttpResponsePtr& response)
{
    for(const auto name : {http::session_cookie_name, http::csrf_cookie_name}) {
        drogon::Cookie cookie{std::string{name}, std::string{}};
        cookie.setPath("/");
        cookie.setMaxAge(0);
        cookie.setSecure(true);
        cookie.setSameSite(drogon::Cookie::SameSite::kLax);
        if(name == http::session_cookie_name) {
            cookie.setHttpOnly(true);
        }
        response->addCookie(std::move(cookie));
    }
}

[[nodiscard]] drogon::HttpResponsePtr auth_success(
    const services::AuthIssued& issued,
    int ttl_seconds
)
{
    Json::Value body;
    body["user"] = current_user_to_json(issued.user);
    body["csrf_token"] = issued.csrf_token;
    body["expires_at"] = static_cast<Json::Int64>(issued.expires_at);

    auto response = drogon::HttpResponse::newHttpJsonResponse(body);
    set_session_cookies(response, issued, ttl_seconds);
    return response;
}

void handle_register(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback
)
{
    const auto& app_config = config::app_config_from_drogon();
    const auto client_ip = http::client_ip_from(request);
    if(!security::request_rate_limiter().consume(
        security::RateLimitScope::registration,
        client_ip,
        std::nullopt,
        app_config.rate_limits.registration
    )) {
        callback(error_response(request, http::ErrorCode::rate_limited, "rate limit exceeded"));
        return;
    }

    const auto body = http::parse_json_body(request);
    if(!body) {
        callback(error_response(request, http::ErrorCode::invalid_argument, "invalid json body"));
        return;
    }

    bool type_error = false;
    services::RegisterRequest registration{
        .username = http::json_string(*body, "username", type_error).value_or(""),
        .email = http::json_string(*body, "email", type_error),
        .password = http::json_string(*body, "password", type_error).value_or("")
    };
    if(type_error) {
        callback(error_response(request, http::ErrorCode::invalid_argument, "invalid field type"));
        return;
    }

    const services::AuthService service{
        repositories::UserRepository{},
        repositories::SessionRepository{},
        app_config.session_ttl_seconds,
        app_config.password_hash_options
    };

    const auto result = service.register_user(
        registration,
        client_ip,
        request->getHeader("user-agent"),
        util::utc_unix_seconds()
    );
    if(!result.has_value()) {
        callback(error_response(
            request,
            to_error_code(result.error()),
            std::string{services::to_string(result.error())}
        ));
        return;
    }

    callback(auth_success(*result, app_config.session_ttl_seconds));
}

void handle_login(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback
)
{
    const auto& app_config = config::app_config_from_drogon();
    const auto client_ip = http::client_ip_from(request);
    const auto body = http::parse_json_body(request);
    if(!body) {
        callback(error_response(request, http::ErrorCode::invalid_argument, "invalid json body"));
        return;
    }

    bool type_error = false;
    services::LoginRequest login{
        .username = http::json_string(*body, "username", type_error).value_or(""),
        .password = http::json_string(*body, "password", type_error).value_or("")
    };
    if(type_error) {
        callback(error_response(request, http::ErrorCode::invalid_argument, "invalid field type"));
        return;
    }

    auto& limiter = security::request_rate_limiter();
    auto reservation = limiter.reserve(
        security::RateLimitScope::login,
        client_ip,
        std::nullopt,
        app_config.rate_limits.login
    );
    if(!reservation.has_value()) {
        callback(error_response(request, http::ErrorCode::rate_limited, "rate limit exceeded"));
        return;
    }

    const services::AuthService service{
        repositories::UserRepository{},
        repositories::SessionRepository{},
        app_config.session_ttl_seconds,
        app_config.password_hash_options
    };

    const auto result = service.login(
        login,
        client_ip,
        request->getHeader("user-agent"),
        util::utc_unix_seconds()
    );
    if(!result.has_value()) {
        if(result.error() == services::AuthError::invalid_credentials) {
            reservation->commit();
        }
        const auto code = result.error() == services::AuthError::invalid_credentials
            ? http::ErrorCode::unauthenticated
            : to_error_code(result.error());
        callback(error_response(
            request,
            code,
            std::string{services::to_string(result.error())}
        ));
        return;
    }

    reservation->cancel();
    limiter.reset(security::RateLimitScope::login, client_ip, std::nullopt);
    callback(auth_success(*result, app_config.session_ttl_seconds));
}

void handle_logout(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback
)
{
    const auto token = request->getCookie(std::string{http::session_cookie_name});
    services::AuthService{}.logout(token, util::utc_unix_seconds());

    auto response = drogon::HttpResponse::newHttpResponse();
    response->setStatusCode(drogon::k204NoContent);
    clear_session_cookies(response);
    callback(response);
}

void handle_get_me(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback
)
{
    const auto session = http::session_context_of(request);
    if(!session.has_value()) {
        callback(error_response(
            request,
            http::ErrorCode::unauthenticated,
            "authentication required"
        ));
        return;
    }

    const auto user = services::AuthService{}.get_user(session->user_id);
    if(!user.has_value()) {
        callback(error_response(request, http::ErrorCode::not_found, "user not found"));
        return;
    }

    Json::Value body;
    body["user"] = current_user_to_json(*user);
    callback(drogon::HttpResponse::newHttpJsonResponse(body));
}

void handle_patch_me(
    const drogon::HttpRequestPtr& request,
    const HttpCallback& callback
)
{
    const auto session = http::session_context_of(request);
    if(!session.has_value()) {
        callback(error_response(
            request,
            http::ErrorCode::unauthenticated,
            "authentication required"
        ));
        return;
    }

    const auto body = http::parse_json_body(request);
    if(!body) {
        callback(error_response(request, http::ErrorCode::invalid_argument, "invalid json body"));
        return;
    }

    bool type_error = false;
    services::UpdateProfileRequest update{
        .email = http::json_string(*body, "email", type_error),
        .avatar_url = http::json_string(*body, "avatar_url", type_error)
    };
    if(type_error) {
        callback(error_response(request, http::ErrorCode::invalid_argument, "invalid field type"));
        return;
    }

    const auto result = services::AuthService{}.update_profile(
        session->user_id,
        update,
        util::utc_unix_seconds()
    );
    if(!result.has_value()) {
        callback(error_response(
            request,
            to_error_code(result.error()),
            std::string{services::to_string(result.error())}
        ));
        return;
    }

    Json::Value response_body;
    response_body["user"] = current_user_to_json(*result);
    callback(drogon::HttpResponse::newHttpJsonResponse(response_body));
}

}

void register_auth_routes()
{
    filters::ensure_session_filters_registered();

    drogon::app().registerHandler(
        "/api/auth/register",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            http::run_guarded_request(request, callback, "auth.register", [&]() {
                handle_register(request, callback);
            });
        },
        {drogon::Post}
    );

    drogon::app().registerHandler(
        "/api/auth/login",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            http::run_guarded_request(request, callback, "auth.login", [&]() {
                handle_login(request, callback);
            });
        },
        {drogon::Post}
    );

    drogon::app().registerHandler(
        "/api/auth/logout",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            http::run_guarded_request(request, callback, "auth.logout", [&]() {
                handle_logout(request, callback);
            });
        },
        {drogon::Post,
         std::string{"blogalone::filters::SessionFilter"},
         std::string{"blogalone::filters::RequireAuthFilter"}}
    );

    drogon::app().registerHandler(
        "/api/me",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            http::run_guarded_request(request, callback, "auth.me", [&]() {
                handle_get_me(request, callback);
            });
        },
        {drogon::Get,
         std::string{"blogalone::filters::SessionFilter"},
         std::string{"blogalone::filters::RequireAuthFilter"}}
    );

    drogon::app().registerHandler(
        "/api/me",
        [](const drogon::HttpRequestPtr& request, HttpCallback&& callback) {
            http::run_guarded_request(request, callback, "auth.patch_me", [&]() {
                handle_patch_me(request, callback);
            });
        },
        {drogon::Patch,
         std::string{"blogalone::filters::SessionFilter"},
         std::string{"blogalone::filters::RequireAuthFilter"}}
    );
}

}
