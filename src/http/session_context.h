#pragma once

#include "models/user.h"

#include <drogon/HttpRequest.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace blogalone::http {

inline constexpr std::string_view session_user_id_attribute{"blogalone.session.user_id"};
inline constexpr std::string_view session_user_role_attribute{"blogalone.session.user_role"};
inline constexpr std::string_view session_token_hash_attribute{"blogalone.session.token_hash"};
inline constexpr std::string_view session_csrf_hash_attribute{"blogalone.session.csrf_hash"};
inline constexpr std::string_view session_admin_confirmed_at_attribute{"blogalone.session.admin_confirmed_at"};

inline constexpr std::string_view session_cookie_name{"ba_session"};
inline constexpr std::string_view csrf_cookie_name{"ba_csrf"};
inline constexpr std::string_view csrf_header_name{"X-CSRF-Token"};

struct SessionContext {
    std::int64_t user_id{};
    models::UserRole role{models::UserRole::user};
    std::string token_hash;
    std::string csrf_hash;
    std::optional<std::int64_t> admin_confirmed_at;
};

[[nodiscard]] std::optional<SessionContext> session_context_of(const drogon::HttpRequestPtr& request);
void set_session_context(const drogon::HttpRequestPtr& request, const SessionContext& context);

}
