#include "http/session_context.h"

#include <string>

namespace blogalone::http {

std::optional<SessionContext> session_context_of(const drogon::HttpRequestPtr& request)
{
    if(!request) {
        return std::nullopt;
    }
    const auto attrs = request->attributes();
    if(!attrs->find(std::string{session_user_id_attribute})) {
        return std::nullopt;
    }

    SessionContext context{
        .user_id = attrs->get<std::int64_t>(std::string{session_user_id_attribute}),
        .role = attrs->get<models::UserRole>(std::string{session_user_role_attribute}),
        .token_hash = attrs->get<std::string>(std::string{session_token_hash_attribute}),
        .csrf_hash = attrs->get<std::string>(std::string{session_csrf_hash_attribute}),
        .admin_confirmed_at = std::nullopt
    };
    if(attrs->find(std::string{session_admin_confirmed_at_attribute})) {
        context.admin_confirmed_at = attrs->get<std::int64_t>(
            std::string{session_admin_confirmed_at_attribute}
        );
    }
    return context;
}

void set_session_context(const drogon::HttpRequestPtr& request, const SessionContext& context)
{
    auto attrs = request->attributes();
    attrs->insert(std::string{session_user_id_attribute}, context.user_id);
    attrs->insert(std::string{session_user_role_attribute}, context.role);
    attrs->insert(std::string{session_token_hash_attribute}, context.token_hash);
    attrs->insert(std::string{session_csrf_hash_attribute}, context.csrf_hash);
    if(context.admin_confirmed_at.has_value()) {
        attrs->insert(
            std::string{session_admin_confirmed_at_attribute},
            *context.admin_confirmed_at
        );
    }
}

}
