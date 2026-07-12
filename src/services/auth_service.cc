#include "services/auth_service.h"

#include "db/transaction.h"
#include "repositories/upload_repository.h"
#include "util/credentials.h"
#include "util/crypto.h"
#include "util/text.h"

#include <drogon/orm/Exception.h>

#include <algorithm>
#include <cctype>
#include <map>
#include <mutex>
#include <optional>
#include <utility>

namespace blogalone::services {
namespace {

constexpr std::size_t kMaxEmailLength = 254;
constexpr std::size_t kTokenByteCount = 32;
constexpr std::size_t kMaxAvatarUrlLength = 512;
constexpr std::string_view kAvatarUrlPrefix = "/uploads/";
constexpr std::string_view kFakeLoginPassword = "blogalone fixed fake login password";

using PasswordHashKey = std::pair<unsigned long long, std::size_t>;

[[nodiscard]] std::string to_lower_ascii(std::string_view value)
{
    std::string output(value.size(), '\0');
    std::ranges::transform(value, output.begin(), [](char ch) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    });
    return output;
}

[[nodiscard]] bool is_valid_email(std::string_view email)
{
    if(email.empty() || email.size() > kMaxEmailLength) {
        return false;
    }
    const auto at = email.find('@');
    if(at == std::string_view::npos || at == 0 || at == email.size() - 1) {
        return false;
    }
    return email.find('.', at) != std::string_view::npos;
}

[[nodiscard]] bool is_valid_avatar_url(std::string_view url)
{
    if(!url.starts_with(kAvatarUrlPrefix)
        || url.size() == kAvatarUrlPrefix.size()
        || url.size() > kMaxAvatarUrlLength) {
        return false;
    }
    if(url.find("..") != std::string_view::npos) {
        return false;
    }
    return std::ranges::none_of(url, [](char ch) {
        const auto byte = static_cast<unsigned char>(ch);
        return byte < 0x20 || byte == 0x7f || ch == '\\';
    });
}

[[nodiscard]] bool is_user_banned(const models::User& user, std::int64_t now)
{
    return user.banned_until.has_value() && *user.banned_until > now;
}

[[nodiscard]] const std::string& fake_password_hash(const util::PasswordHashOptions& options)
{
    static std::mutex mutex;
    static std::map<PasswordHashKey, std::string> hashes;

    const auto key = PasswordHashKey{options.opslimit, options.memlimit};
    {
        const std::scoped_lock lock{mutex};
        const auto found = hashes.find(key);
        if(found != hashes.end()) {
            return found->second;
        }
    }

    auto hash = util::hash_password(kFakeLoginPassword, options);
    const std::scoped_lock lock{mutex};
    const auto entry = hashes.try_emplace(key, std::move(hash)).first;
    return entry->second;
}

[[nodiscard]] bool is_unique_violation(const drogon::orm::DrogonDbException& error)
{
    return dynamic_cast<const drogon::orm::UniqueViolation*>(&error.base()) != nullptr;
}

[[nodiscard]] std::optional<AuthError> registration_conflict_error(
    const repositories::UserRepository& repository,
    std::string_view username,
    const std::optional<std::string>& email
)
{
    if(repository.find_by_username(username).has_value()) {
        return AuthError::username_taken;
    }
    if(email.has_value() && repository.find_by_email(*email).has_value()) {
        return AuthError::email_taken;
    }
    return std::nullopt;
}

}

std::string_view to_string(AuthError error)
{
    switch(error) {
    case AuthError::invalid_input:
        return "invalid_input";
    case AuthError::username_taken:
        return "username_taken";
    case AuthError::email_taken:
        return "email_taken";
    case AuthError::invalid_credentials:
        return "invalid_credentials";
    case AuthError::user_banned:
        return "user_banned";
    case AuthError::not_found:
        return "not_found";
    }
    return "invalid_input";
}

AuthService::AuthService(
    repositories::UserRepository user_repository,
    repositories::SessionRepository session_repository,
    std::int64_t session_ttl_seconds,
    util::PasswordHashOptions password_hash_options
)
    : user_repository_{std::move(user_repository)}
    , session_repository_{std::move(session_repository)}
    , session_ttl_seconds_{session_ttl_seconds}
    , password_hash_options_{password_hash_options}
{
}

AuthResult<AuthIssued> AuthService::register_user(
    const RegisterRequest& request,
    std::string_view ip,
    std::string_view user_agent,
    std::int64_t now
) const
{
    const auto username = util::trim_ascii_whitespace(request.username);
    const auto password = request.password;
    if(!util::is_valid_username(username) || !util::is_valid_password(password)) {
        return AuthError::invalid_input;
    }

    std::optional<std::string> normalized_email;
    if(request.email.has_value()) {
        const auto email = to_lower_ascii(util::trim_ascii_whitespace(*request.email));
        if(!is_valid_email(email)) {
            return AuthError::invalid_input;
        }
        normalized_email = email;
    }

    const auto session_token = util::random_token_hex(kTokenByteCount);
    const auto csrf_token = util::random_token_hex(kTokenByteCount);
    const auto expires_at = now + session_ttl_seconds_;
    const auto pwd_hash = util::hash_password(password, password_hash_options_);
    models::User user;
    try {
        db::Transaction transaction{
            user_repository_.client(),
            drogon::orm::TransactionType::Immediate
        };
        const auto transaction_client = transaction.client();
        const repositories::UserRepository user_repository{transaction_client};
        const repositories::SessionRepository session_repository{transaction_client};

        const auto user_id = user_repository.create(username, normalized_email, pwd_hash, now);
        const auto created = user_repository.find_by_id(user_id);
        if(!created.has_value()) {
            return AuthError::not_found;
        }
        user = *created;

        session_repository.create(models::Session{
            .token_hash = util::sha256_hex(session_token),
            .user_id = user.id,
            .csrf_hash = util::sha256_hex(csrf_token),
            .created_at = now,
            .expires_at = expires_at,
            .revoked_at = std::nullopt,
            .admin_confirmed_at = std::nullopt,
            .ip = std::string{ip},
            .user_agent = std::string{user_agent}
        });
        transaction.commit();
    } catch(const drogon::orm::DrogonDbException& error) {
        if(is_unique_violation(error)) {
            if(const auto conflict = registration_conflict_error(
                user_repository_,
                username,
                normalized_email
            )) {
                return *conflict;
            }
        }
        throw;
    }

    return AuthIssued{
        .session_token = session_token,
        .csrf_token = csrf_token,
        .expires_at = expires_at,
        .user = user
    };
}

AuthResult<AuthIssued> AuthService::login(
    const LoginRequest& request,
    std::string_view ip,
    std::string_view user_agent,
    std::int64_t now
) const
{
    const auto username = util::trim_ascii_whitespace(request.username);
    if(username.empty() || request.password.empty()) {
        return AuthError::invalid_credentials;
    }

    const auto user = user_repository_.find_by_username(username);
    if(!user.has_value()) {
        static_cast<void>(util::verify_password(
            request.password,
            fake_password_hash(password_hash_options_)
        ));
        return AuthError::invalid_credentials;
    }
    if(!util::verify_password(request.password, user->pwd_hash)) {
        return AuthError::invalid_credentials;
    }
    if(is_user_banned(*user, now)) {
        return AuthError::user_banned;
    }

    const auto session_token = util::random_token_hex(kTokenByteCount);
    const auto csrf_token = util::random_token_hex(kTokenByteCount);
    const auto expires_at = now + session_ttl_seconds_;

    session_repository_.create(models::Session{
        .token_hash = util::sha256_hex(session_token),
        .user_id = user->id,
        .csrf_hash = util::sha256_hex(csrf_token),
        .created_at = now,
        .expires_at = expires_at,
        .revoked_at = std::nullopt,
        .admin_confirmed_at = std::nullopt,
        .ip = std::string{ip},
        .user_agent = std::string{user_agent}
    });

    return AuthIssued{
        .session_token = session_token,
        .csrf_token = csrf_token,
        .expires_at = expires_at,
        .user = *user
    };
}

void AuthService::logout(std::string_view session_token, std::int64_t now) const
{
    if(session_token.empty()) {
        return;
    }
    session_repository_.revoke(util::sha256_hex(session_token), now);
}

std::optional<models::User> AuthService::get_user(std::int64_t user_id) const
{
    return user_repository_.find_by_id(user_id);
}

AuthResult<models::User> AuthService::update_profile(
    std::int64_t user_id,
    const UpdateProfileRequest& request,
    std::int64_t now
) const
{
    std::optional<std::string> email;
    if(request.email.has_value()) {
        const auto trimmed = to_lower_ascii(util::trim_ascii_whitespace(*request.email));
        if(!is_valid_email(trimmed)) {
            return AuthError::invalid_input;
        }
        email = trimmed;
    }

    std::optional<std::string> avatar_url;
    if(request.avatar_url.has_value()) {
        const auto trimmed = util::trim_ascii_whitespace(*request.avatar_url);
        if(!is_valid_avatar_url(trimmed)) {
            return AuthError::invalid_input;
        }
        avatar_url = trimmed;
    }

    if(!email.has_value() && !avatar_url.has_value()) {
        const auto current = user_repository_.find_by_id(user_id);
        if(!current.has_value()) {
            return AuthError::not_found;
        }
        return *current;
    }

    try {
        db::Transaction transaction{
            user_repository_.client(),
            drogon::orm::TransactionType::Immediate
        };
        const auto transaction_client = transaction.client();
        const repositories::UserRepository user_repository{transaction_client};
        const repositories::UploadRepository upload_repository{transaction_client};
        if(email.has_value()) {
            const auto existing = user_repository.find_by_email(*email);
            if(existing.has_value() && existing->id != user_id) {
                return AuthError::email_taken;
            }
        }
        if(avatar_url.has_value()) {
            const auto upload_path = std::string_view{*avatar_url}.substr(kAvatarUrlPrefix.size());
            if(!upload_repository.mark_ref_attached(user_id, upload_path, now)) {
                return AuthError::not_found;
            }
        }
        user_repository.update_profile(user_id, email, avatar_url, now);
        const auto updated = user_repository.find_by_id(user_id);
        if(!updated.has_value()) {
            return AuthError::not_found;
        }
        transaction.commit();
        return *updated;
    } catch(const drogon::orm::DrogonDbException& error) {
        if(email.has_value() && is_unique_violation(error)) {
            return AuthError::email_taken;
        }
        throw;
    }
}

}
