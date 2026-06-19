#include "filters/session_filter.h"

#include "http/api_error.h"
#include "http/handler_guard.h"
#include "http/request_context.h"
#include "http/session_context.h"
#include "util/crypto.h"
#include "util/time.h"

#include <utility>

namespace blogalone::filters {
namespace {

[[nodiscard]] drogon::HttpResponsePtr unauthenticated_response(const drogon::HttpRequestPtr& request)
{
    return http::make_error_response(
        http::make_api_error(http::ErrorCode::unauthenticated, "authentication required"),
        http::request_id_from(request)
    );
}

[[nodiscard]] drogon::HttpResponsePtr forbidden_response(
    const drogon::HttpRequestPtr& request,
    std::string message
)
{
    return http::make_error_response(
        http::make_api_error(http::ErrorCode::forbidden, std::move(message)),
        http::request_id_from(request)
    );
}

}

SessionFilter::SessionFilter(
    repositories::UserRepository user_repository,
    repositories::SessionRepository session_repository
)
    : user_repository_{std::move(user_repository)}
    , session_repository_{std::move(session_repository)}
{
}

void SessionFilter::doFilter(
    const drogon::HttpRequestPtr& request,
    drogon::FilterCallback&& failure,
    drogon::FilterChainCallback&& chain
)
{
    http::run_guarded_request(request, failure, "session_filter", [&]() {
        const auto session_token = request->getCookie(std::string{http::session_cookie_name});
        if(session_token.empty()) {
            chain();
            return;
        }

        const auto token_hash = util::sha256_hex(session_token);
        const auto session = session_repository_.find_by_token_hash(token_hash);
        if(!session.has_value() || !util::constant_time_equal(token_hash, session->token_hash)) {
            chain();
            return;
        }

        const auto now = util::utc_unix_seconds();
        if(session->revoked_at.has_value() || session->expires_at <= now) {
            chain();
            return;
        }

        const auto user = user_repository_.find_by_id(session->user_id);
        if(!user.has_value()) {
            chain();
            return;
        }
        if(user->banned_until.has_value() && *user->banned_until > now) {
            failure(forbidden_response(request, "account is banned"));
            return;
        }

        http::set_session_context(request, http::SessionContext{
            .user_id = user->id,
            .role = user->role,
            .token_hash = session->token_hash,
            .csrf_hash = session->csrf_hash,
            .admin_confirmed_at = session->admin_confirmed_at
        });
        chain();
    });
}

void RequireAuthFilter::doFilter(
    const drogon::HttpRequestPtr& request,
    drogon::FilterCallback&& failure,
    drogon::FilterChainCallback&& chain
)
{
    if(!http::session_context_of(request).has_value()) {
        failure(unauthenticated_response(request));
        return;
    }
    chain();
}

void RequireAdminFilter::doFilter(
    const drogon::HttpRequestPtr& request,
    drogon::FilterCallback&& failure,
    drogon::FilterChainCallback&& chain
)
{
    const auto session = http::session_context_of(request);
    if(!session.has_value()) {
        failure(unauthenticated_response(request));
        return;
    }
    if(session->role != models::UserRole::admin) {
        failure(forbidden_response(request, "administrator access required"));
        return;
    }
    chain();
}

void ensure_session_filters_registered()
{
    static_cast<void>(SessionFilter::classTypeName());
    static_cast<void>(RequireAuthFilter::classTypeName());
    static_cast<void>(RequireAdminFilter::classTypeName());
}

}
