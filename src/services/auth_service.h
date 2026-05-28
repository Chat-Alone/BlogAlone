#pragma once

#include "models/user.h"
#include "repositories/session_repository.h"
#include "repositories/user_repository.h"
#include "util/password.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>

namespace blogalone::services {

struct RegisterRequest {
    std::string username;
    std::optional<std::string> email;
    std::string password;
};

struct LoginRequest {
    std::string username;
    std::string password;
};

struct UpdateProfileRequest {
    std::optional<std::string> email;
    std::optional<std::string> avatar_url;
};

struct AuthIssued {
    std::string session_token;
    std::string csrf_token;
    std::int64_t expires_at{};
    models::User user;
};

enum class AuthError {
    invalid_input,
    username_taken,
    email_taken,
    invalid_credentials,
    user_banned,
    not_found
};

[[nodiscard]] std::string_view to_string(AuthError error);

template <typename T>
class AuthResult {
  public:
    AuthResult(T value) : storage_{std::move(value)} {}
    AuthResult(AuthError error) : storage_{error} {}

    [[nodiscard]] bool has_value() const { return storage_.index() == 0; }
    [[nodiscard]] explicit operator bool() const { return has_value(); }

    [[nodiscard]] const T& value() const { return std::get<0>(storage_); }
    [[nodiscard]] T& value() { return std::get<0>(storage_); }
    [[nodiscard]] AuthError error() const { return std::get<1>(storage_); }

    [[nodiscard]] const T& operator*() const { return value(); }
    [[nodiscard]] T& operator*() { return value(); }
    [[nodiscard]] const T* operator->() const { return &value(); }
    [[nodiscard]] T* operator->() { return &value(); }

  private:
    std::variant<T, AuthError> storage_;
};

class AuthService {
  public:
    AuthService(
        repositories::UserRepository user_repository = repositories::UserRepository{},
        repositories::SessionRepository session_repository = repositories::SessionRepository{},
        std::int64_t session_ttl_seconds = 1'209'600,
        util::PasswordHashOptions password_hash_options = util::default_password_hash_options()
    );

    [[nodiscard]] AuthResult<AuthIssued> register_user(
        const RegisterRequest& request,
        std::string_view ip,
        std::string_view user_agent,
        std::int64_t now
    ) const;

    [[nodiscard]] AuthResult<AuthIssued> login(
        const LoginRequest& request,
        std::string_view ip,
        std::string_view user_agent,
        std::int64_t now
    ) const;

    void logout(std::string_view session_token, std::int64_t now) const;

    [[nodiscard]] std::optional<models::User> get_user(std::int64_t user_id) const;

    [[nodiscard]] AuthResult<models::User> update_profile(
        std::int64_t user_id,
        const UpdateProfileRequest& request,
        std::int64_t now
    ) const;

  private:
    repositories::UserRepository user_repository_;
    repositories::SessionRepository session_repository_;
    std::int64_t session_ttl_seconds_;
    util::PasswordHashOptions password_hash_options_;
};

}
