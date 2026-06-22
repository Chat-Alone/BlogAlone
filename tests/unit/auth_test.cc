#include "db/migrations.h"
#include "filters/csrf_filter.h"
#include "filters/session_filter.h"
#include "http/session_context.h"
#include "models/user.h"
#include "repositories/session_repository.h"
#include "services/auth_service.h"
#include "util/crypto.h"
#include "util/password.h"

#include <drogon/DrClassMap.h>
#include <drogon/drogon.h>
#include <gtest/gtest.h>
#include <sodium.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace {

class TempWorkspace {
  public:
    TempWorkspace()
    {
        const auto base = std::filesystem::temp_directory_path();
        const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();

        for(int attempt = 0; attempt < 100; ++attempt) {
            auto candidate = base
                / ("blogalone-auth-test-" + std::to_string(ticks) + "-" + std::to_string(attempt));
            if(std::filesystem::create_directory(candidate)) {
                path_ = std::move(candidate);
                return;
            }
        }

        throw std::runtime_error{"unable to create temporary test directory"};
    }

    TempWorkspace(const TempWorkspace&) = delete;
    TempWorkspace& operator=(const TempWorkspace&) = delete;

    ~TempWorkspace()
    {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    [[nodiscard]] const std::filesystem::path& path() const
    {
        return path_;
    }

  private:
    std::filesystem::path path_;
};

[[nodiscard]] blogalone::util::PasswordHashOptions fast_password_hash_options()
{
    return blogalone::util::PasswordHashOptions{
        .opslimit = crypto_pwhash_OPSLIMIT_MIN,
        .memlimit = static_cast<std::size_t>(crypto_pwhash_MEMLIMIT_MIN)
    };
}

[[nodiscard]] drogon::orm::DbClientPtr fresh_auth_test_client(
    const std::filesystem::path& database_path
)
{
    static_cast<void>(blogalone::db::run_migrations({
        .database_path = database_path,
        .migrations_dir = std::filesystem::path{BLOGALONE_SOURCE_DIR} / "migrations"
    }));
    const auto client = drogon::orm::DbClient::newSqlite3Client(
        "filename=" + database_path.generic_string(),
        1
    );
    client->execSqlSync("PRAGMA foreign_keys = ON;");
    client->execSqlSync("PRAGMA busy_timeout = 5000;");
    return client;
}

[[nodiscard]] drogon::orm::DbClientPtr auth_test_client()
{
    static const auto workspace = std::make_unique<TempWorkspace>();
    static const auto initialized = fresh_auth_test_client(workspace->path() / "blogalone.db");
    return initialized;
}

[[nodiscard]] blogalone::services::AuthService auth_test_service(
    const drogon::orm::DbClientPtr& client
)
{
    return blogalone::services::AuthService{
        blogalone::repositories::UserRepository{client},
        blogalone::repositories::SessionRepository{client},
        3'600,
        fast_password_hash_options()
    };
}

[[nodiscard]] blogalone::services::AuthService auth_test_service()
{
    return auth_test_service(auth_test_client());
}

[[nodiscard]] std::string unique_username(std::string_view prefix)
{
    static int counter = 0;
    ++counter;
    return std::string{prefix} + std::to_string(counter);
}

[[nodiscard]] std::int64_t insert_filter_test_user(
    const std::string& username,
    std::optional<std::int64_t> banned_until = std::nullopt
)
{
    const auto client = auth_test_client();
    if(banned_until.has_value()) {
        const auto rows = client->execSqlSync(
            "INSERT INTO users (username, email, pwd_hash, banned_until, created_at, updated_at) "
            "VALUES (?, NULL, 'hash', ?, 1, 1) RETURNING id",
            username,
            *banned_until
        );
        return rows.at(0)["id"].as<std::int64_t>();
    }

    const auto rows = client->execSqlSync(
        "INSERT INTO users (username, email, pwd_hash, created_at, updated_at) "
        "VALUES (?, NULL, 'hash', 1, 1) RETURNING id",
        username
    );
    return rows.at(0)["id"].as<std::int64_t>();
}

void insert_filter_test_session(
    std::string token,
    std::int64_t user_id,
    std::int64_t expires_at,
    std::optional<std::int64_t> revoked_at = std::nullopt
)
{
    const auto client = auth_test_client();
    const auto token_hash = blogalone::util::sha256_hex(token);
    const auto csrf_hash = blogalone::util::sha256_hex("csrf-token");
    if(revoked_at.has_value()) {
        client->execSqlSync(
            "INSERT INTO sessions (token_hash, user_id, csrf_hash, created_at, expires_at, "
            "revoked_at, ip, user_agent) VALUES (?, ?, ?, 1, ?, ?, '127.0.0.1', 'test')",
            token_hash,
            user_id,
            csrf_hash,
            expires_at,
            *revoked_at
        );
        return;
    }

    client->execSqlSync(
        "INSERT INTO sessions (token_hash, user_id, csrf_hash, created_at, expires_at, ip, user_agent) "
        "VALUES (?, ?, ?, 1, ?, '127.0.0.1', 'test')",
        token_hash,
        user_id,
        csrf_hash,
        expires_at
    );
}

[[nodiscard]] drogon::HttpRequestPtr csrf_request(
    std::string origin,
    std::string referer,
    std::string token
)
{
    auto request = drogon::HttpRequest::newHttpRequest();
    request->setMethod(drogon::Post);
    request->addHeader("host", "forum.example.test");
    if(!origin.empty()) {
        request->addHeader("origin", std::move(origin));
    }
    if(!referer.empty()) {
        request->addHeader("referer", std::move(referer));
    }
    if(!token.empty()) {
        request->addHeader(std::string{blogalone::http::csrf_header_name}, std::move(token));
    }
    return request;
}

void attach_session_context(const drogon::HttpRequestPtr& request, std::string csrf_token)
{
    blogalone::http::set_session_context(request, blogalone::http::SessionContext{
        .user_id = 1,
        .role = blogalone::models::UserRole::user,
        .token_hash = "session-hash",
        .csrf_hash = blogalone::util::sha256_hex(csrf_token),
        .admin_confirmed_at = std::nullopt
    });
}

struct FilterResult {
    bool failed{};
    bool chained{};
    drogon::HttpStatusCode status{drogon::kUnknown};
};

[[nodiscard]] FilterResult run_session_and_require_auth_filters(
    const drogon::HttpRequestPtr& request
)
{
    FilterResult result;
    const auto client = auth_test_client();
    blogalone::filters::SessionFilter session_filter{
        blogalone::repositories::UserRepository{client},
        blogalone::repositories::SessionRepository{client}
    };
    blogalone::filters::RequireAuthFilter require_auth_filter;
    session_filter.doFilter(
        request,
        [&result](const drogon::HttpResponsePtr& response) {
            result.failed = true;
            result.status = response->statusCode();
        },
        [&]() {
            require_auth_filter.doFilter(
                request,
                [&result](const drogon::HttpResponsePtr& response) {
                    result.failed = true;
                    result.status = response->statusCode();
                },
                [&result]() {
                    result.chained = true;
                }
            );
        }
    );
    return result;
}

[[nodiscard]] FilterResult run_csrf_guard(const drogon::HttpRequestPtr& request)
{
    FilterResult result;
    if(const auto rejection = blogalone::filters::check_write_csrf(request)) {
        result.failed = true;
        result.status = (*rejection)->statusCode();
        return result;
    }
    result.chained = true;
    return result;
}

}

TEST(PasswordTest, HashesAndVerifiesRoundTrip)
{
    const auto hash = blogalone::util::hash_password("correct horse battery staple");

    EXPECT_FALSE(hash.empty());
    EXPECT_TRUE(blogalone::util::verify_password("correct horse battery staple", hash));
    EXPECT_FALSE(blogalone::util::verify_password("wrong password", hash));
}

TEST(PasswordTest, RejectsEmptyAndOversizedPasswords)
{
    EXPECT_THROW(
        static_cast<void>(blogalone::util::hash_password("")),
        std::invalid_argument
    );

    const std::string huge(129, 'a');
    EXPECT_THROW(
        static_cast<void>(blogalone::util::hash_password(huge)),
        std::invalid_argument
    );
}

TEST(PasswordTest, ProducesDistinctHashesForSamePassword)
{
    const auto first = blogalone::util::hash_password("hello world");
    const auto second = blogalone::util::hash_password("hello world");

    EXPECT_NE(first, second);
    EXPECT_TRUE(blogalone::util::verify_password("hello world", first));
    EXPECT_TRUE(blogalone::util::verify_password("hello world", second));
}

TEST(UserRoleTest, ConvertsBetweenStringAndEnum)
{
    EXPECT_EQ(blogalone::models::to_string(blogalone::models::UserRole::user), "user");
    EXPECT_EQ(blogalone::models::to_string(blogalone::models::UserRole::admin), "admin");
    EXPECT_EQ(
        blogalone::models::user_role_from_string("admin"),
        blogalone::models::UserRole::admin
    );
    EXPECT_EQ(
        blogalone::models::user_role_from_string("user"),
        blogalone::models::UserRole::user
    );
    EXPECT_THROW(
        static_cast<void>(blogalone::models::user_role_from_string("superuser")),
        std::invalid_argument
    );
}

TEST(AuthValidationTest, RejectsNonChineseUnicodeUsernames)
{
    const blogalone::services::AuthService service;

    const auto emoji = service.register_user(
        blogalone::services::RegisterRequest{
            .username = "ab\xf0\x9f\x98\x80",
            .email = std::nullopt,
            .password = "valid-password"
        },
        "127.0.0.1",
        "test",
        1
    );
    const auto invalid_utf8 = service.register_user(
        blogalone::services::RegisterRequest{
            .username = std::string{"abc\xff", 4},
            .email = std::nullopt,
            .password = "valid-password"
        },
        "127.0.0.1",
        "test",
        1
    );

    ASSERT_FALSE(emoji.has_value());
    ASSERT_FALSE(invalid_utf8.has_value());
    EXPECT_EQ(emoji.error(), blogalone::services::AuthError::invalid_input);
    EXPECT_EQ(invalid_utf8.error(), blogalone::services::AuthError::invalid_input);
}

TEST(AuthServiceTest, RegistersAndRejectsDuplicateUsername)
{
    const auto service = auth_test_service();
    const auto username = unique_username("dupuser");

    const auto first = service.register_user(
        blogalone::services::RegisterRequest{
            .username = username,
            .email = username + "@example.test",
            .password = "valid-password"
        },
        "127.0.0.1",
        "test",
        10
    );
    const auto duplicate = service.register_user(
        blogalone::services::RegisterRequest{
            .username = username,
            .email = unique_username("mail") + "@example.test",
            .password = "valid-password"
        },
        "127.0.0.1",
        "test",
        11
    );

    ASSERT_TRUE(first.has_value());
    ASSERT_FALSE(duplicate.has_value());
    EXPECT_EQ(duplicate.error(), blogalone::services::AuthError::username_taken);
}

TEST(AuthServiceTest, RegistersAndRejectsDuplicateEmail)
{
    const auto service = auth_test_service();
    const auto email = unique_username("email") + "@example.test";

    const auto first = service.register_user(
        blogalone::services::RegisterRequest{
            .username = unique_username("mailuser"),
            .email = email,
            .password = "valid-password"
        },
        "127.0.0.1",
        "test",
        20
    );
    const auto duplicate = service.register_user(
        blogalone::services::RegisterRequest{
            .username = unique_username("mailuser"),
            .email = email,
            .password = "valid-password"
        },
        "127.0.0.1",
        "test",
        21
    );

    ASSERT_TRUE(first.has_value());
    ASSERT_FALSE(duplicate.has_value());
    EXPECT_EQ(duplicate.error(), blogalone::services::AuthError::email_taken);
}

TEST(AuthServiceTest, RollsBackCreatedUserWhenSessionCreateFails)
{
    TempWorkspace workspace;
    const auto client = fresh_auth_test_client(workspace.path() / "rollback.db");
    client->execSqlSync(
        "CREATE TRIGGER fail_session_insert BEFORE INSERT ON sessions "
        "BEGIN SELECT RAISE(FAIL, 'session insert failed'); END;"
    );
    const auto service = auth_test_service(client);
    const auto username = unique_username("rollbackuser");

    EXPECT_THROW(
        static_cast<void>(service.register_user(
            blogalone::services::RegisterRequest{
                .username = username,
                .email = std::nullopt,
                .password = "valid-password"
            },
            "127.0.0.1",
            "test",
            25
        )),
        drogon::orm::DrogonDbException
    );

    const auto rows = client->execSqlSync(
        "SELECT COUNT(*) AS count FROM users WHERE username = ?",
        username
    );
    EXPECT_EQ(rows.at(0)["count"].as<int>(), 0);
}

TEST(AuthServiceTest, RejectsMissingAndWrongPasswordWithSameError)
{
    const auto service = auth_test_service();
    const auto username = unique_username("loginuser");
    const auto registered = service.register_user(
        blogalone::services::RegisterRequest{
            .username = username,
            .email = std::nullopt,
            .password = "valid-password"
        },
        "127.0.0.1",
        "test",
        30
    );

    ASSERT_TRUE(registered.has_value());
    const auto wrong_password = service.login(
        blogalone::services::LoginRequest{
            .username = username,
            .password = "wrong-password"
        },
        "127.0.0.1",
        "test",
        31
    );
    const auto missing_user = service.login(
        blogalone::services::LoginRequest{
            .username = unique_username("missing"),
            .password = "wrong-password"
        },
        "127.0.0.1",
        "test",
        31
    );

    ASSERT_FALSE(wrong_password.has_value());
    ASSERT_FALSE(missing_user.has_value());
    EXPECT_EQ(wrong_password.error(), blogalone::services::AuthError::invalid_credentials);
    EXPECT_EQ(missing_user.error(), blogalone::services::AuthError::invalid_credentials);
}

TEST(AuthServiceTest, UpdatesProfileAndRejectsDuplicateEmail)
{
    const auto service = auth_test_service();
    const auto first = service.register_user(
        blogalone::services::RegisterRequest{
            .username = unique_username("profileuser"),
            .email = unique_username("profilemail") + "@example.test",
            .password = "valid-password"
        },
        "127.0.0.1",
        "test",
        40
    );
    const auto second = service.register_user(
        blogalone::services::RegisterRequest{
            .username = unique_username("profileuser"),
            .email = std::nullopt,
            .password = "valid-password"
        },
        "127.0.0.1",
        "test",
        41
    );
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());

    const auto duplicate = service.update_profile(
        second->user.id,
        blogalone::services::UpdateProfileRequest{.email = first->user.email},
        42
    );
    ASSERT_FALSE(duplicate.has_value());
    EXPECT_EQ(duplicate.error(), blogalone::services::AuthError::email_taken);

    const auto updated = service.update_profile(
        second->user.id,
        blogalone::services::UpdateProfileRequest{
            .email = "  Updated@Example.Test ",
            .avatar_url = " /uploads/avatar.png "
        },
        43
    );
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated->email, std::optional<std::string>{"updated@example.test"});
    EXPECT_EQ(updated->avatar_url, std::optional<std::string>{"/uploads/avatar.png"});
    EXPECT_EQ(updated->updated_at, 43);
}

TEST(SessionRepositoryTest, DeletesOnlyExpiredSessions)
{
    const auto client = auth_test_client();
    const auto user_id = insert_filter_test_user(unique_username("cleanupuser"));
    const auto expired_token = unique_username("expiredtoken");
    const auto active_token = unique_username("activetoken");
    insert_filter_test_session(expired_token, user_id, 100);
    insert_filter_test_session(active_token, user_id, 101);
    const blogalone::repositories::SessionRepository repository{client};

    EXPECT_EQ(repository.delete_expired(100), 1);
    EXPECT_FALSE(repository.find_by_token_hash(
        blogalone::util::sha256_hex(expired_token)
    ).has_value());
    EXPECT_TRUE(repository.find_by_token_hash(
        blogalone::util::sha256_hex(active_token)
    ).has_value());
}

TEST(FilterRegistrationTest, RegistersAuthFiltersForDrogonLookup)
{
    blogalone::filters::ensure_session_filters_registered();

    const auto session_filter = std::unique_ptr<drogon::DrObjectBase>{
        drogon::DrClassMap::newObject("blogalone::filters::SessionFilter")
    };
    const auto require_auth_filter = std::unique_ptr<drogon::DrObjectBase>{
        drogon::DrClassMap::newObject("blogalone::filters::RequireAuthFilter")
    };
    const auto require_admin_filter = std::unique_ptr<drogon::DrObjectBase>{
        drogon::DrClassMap::newObject("blogalone::filters::RequireAdminFilter")
    };

    EXPECT_NE(session_filter.get(), nullptr);
    EXPECT_NE(require_auth_filter.get(), nullptr);
    EXPECT_NE(require_admin_filter.get(), nullptr);
}

TEST(CsrfGuardTest, AllowsSameOriginRequestWithValidToken)
{
    const std::string token{"csrf-token"};
    const auto request = csrf_request("https://forum.example.test", "", token);
    attach_session_context(request, token);

    const auto result = run_csrf_guard(request);

    EXPECT_FALSE(result.failed);
    EXPECT_TRUE(result.chained);
}

TEST(CsrfGuardTest, RejectsCrossSiteOrigin)
{
    const std::string token{"csrf-token"};
    const auto request = csrf_request("https://evil.example.test", "", token);
    attach_session_context(request, token);

    const auto result = run_csrf_guard(request);

    EXPECT_TRUE(result.failed);
    EXPECT_FALSE(result.chained);
    EXPECT_EQ(result.status, drogon::k403Forbidden);
}

TEST(CsrfGuardTest, RejectsCrossSiteRefererWhenOriginIsAbsent)
{
    const std::string token{"csrf-token"};
    const auto request = csrf_request("", "https://evil.example.test/path", token);
    attach_session_context(request, token);

    const auto result = run_csrf_guard(request);

    EXPECT_TRUE(result.failed);
    EXPECT_FALSE(result.chained);
    EXPECT_EQ(result.status, drogon::k403Forbidden);
}

TEST(CsrfGuardTest, RejectsCrossSiteRefererWhenOriginIsPresent)
{
    const std::string token{"csrf-token"};
    const auto request = csrf_request(
        "https://forum.example.test",
        "https://evil.example.test/path",
        token
    );
    attach_session_context(request, token);

    const auto result = run_csrf_guard(request);

    EXPECT_TRUE(result.failed);
    EXPECT_FALSE(result.chained);
    EXPECT_EQ(result.status, drogon::k403Forbidden);
}

TEST(CsrfGuardTest, RejectsMissingAndInvalidTokens)
{
    const std::string token{"csrf-token"};
    const auto missing = csrf_request("https://forum.example.test", "", "");
    const auto invalid = csrf_request("https://forum.example.test", "", "wrong-token");
    attach_session_context(missing, token);
    attach_session_context(invalid, token);

    const auto missing_result = run_csrf_guard(missing);
    const auto invalid_result = run_csrf_guard(invalid);

    EXPECT_TRUE(missing_result.failed);
    EXPECT_FALSE(missing_result.chained);
    EXPECT_EQ(missing_result.status, drogon::k403Forbidden);
    EXPECT_TRUE(invalid_result.failed);
    EXPECT_FALSE(invalid_result.chained);
    EXPECT_EQ(invalid_result.status, drogon::k403Forbidden);
}

TEST(SessionFilterTest, AllowsValidSession)
{
    const auto user_id = insert_filter_test_user("valid_session_user");
    insert_filter_test_session("valid-session-token", user_id, 4'102'444'800);
    const auto request = drogon::HttpRequest::newHttpRequest();
    request->addCookie(std::string{blogalone::http::session_cookie_name}, "valid-session-token");

    const auto result = run_session_and_require_auth_filters(request);
    const auto context = blogalone::http::session_context_of(request);

    EXPECT_FALSE(result.failed);
    EXPECT_TRUE(result.chained);
    ASSERT_TRUE(context.has_value());
    EXPECT_EQ(context->user_id, user_id);
}

TEST(SessionFilterTest, RejectsExpiredSessionThroughRequireAuth)
{
    const auto user_id = insert_filter_test_user("expired_session_user");
    insert_filter_test_session("expired-session-token", user_id, 1);
    const auto request = drogon::HttpRequest::newHttpRequest();
    request->addCookie(std::string{blogalone::http::session_cookie_name}, "expired-session-token");

    const auto result = run_session_and_require_auth_filters(request);

    EXPECT_TRUE(result.failed);
    EXPECT_FALSE(result.chained);
    EXPECT_EQ(result.status, drogon::k401Unauthorized);
    EXPECT_FALSE(blogalone::http::session_context_of(request).has_value());
}

TEST(SessionFilterTest, RejectsRevokedSessionThroughRequireAuth)
{
    const auto user_id = insert_filter_test_user("revoked_session_user");
    insert_filter_test_session("revoked-session-token", user_id, 4'102'444'800, 2);
    const auto request = drogon::HttpRequest::newHttpRequest();
    request->addCookie(std::string{blogalone::http::session_cookie_name}, "revoked-session-token");

    const auto result = run_session_and_require_auth_filters(request);

    EXPECT_TRUE(result.failed);
    EXPECT_FALSE(result.chained);
    EXPECT_EQ(result.status, drogon::k401Unauthorized);
    EXPECT_FALSE(blogalone::http::session_context_of(request).has_value());
}

TEST(SessionFilterTest, RejectsBannedUser)
{
    const auto user_id = insert_filter_test_user("banned_session_user", 4'102'444'800);
    insert_filter_test_session("banned-session-token", user_id, 4'102'444'800);
    const auto request = drogon::HttpRequest::newHttpRequest();
    request->addCookie(std::string{blogalone::http::session_cookie_name}, "banned-session-token");

    const auto result = run_session_and_require_auth_filters(request);

    EXPECT_TRUE(result.failed);
    EXPECT_FALSE(result.chained);
    EXPECT_EQ(result.status, drogon::k403Forbidden);
    EXPECT_FALSE(blogalone::http::session_context_of(request).has_value());
}

TEST(SessionFilterTest, ConvertsDatabaseExceptionsToInternalError)
{
    TempWorkspace workspace;
    const auto client = drogon::orm::DbClient::newSqlite3Client(
        "filename=" + (workspace.path() / "missing-schema.db").generic_string(),
        1
    );
    blogalone::filters::SessionFilter filter{
        blogalone::repositories::UserRepository{client},
        blogalone::repositories::SessionRepository{client}
    };
    auto request = drogon::HttpRequest::newHttpRequest();
    request->addCookie(std::string{blogalone::http::session_cookie_name}, "broken-session-token");

    FilterResult result;
    filter.doFilter(
        request,
        [&result](const drogon::HttpResponsePtr& response) {
            result.failed = true;
            result.status = response->statusCode();
        },
        [&result]() {
            result.chained = true;
        }
    );

    EXPECT_TRUE(result.failed);
    EXPECT_FALSE(result.chained);
    EXPECT_EQ(result.status, drogon::k500InternalServerError);
}
