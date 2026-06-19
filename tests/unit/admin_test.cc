#include "db/migrations.h"
#include "filters/session_filter.h"
#include "http/session_context.h"
#include "models/user.h"
#include "repositories/admin_repository.h"
#include "repositories/session_repository.h"
#include "repositories/user_repository.h"
#include "services/admin_service.h"
#include "util/crypto.h"
#include "util/password.h"

#include <drogon/drogon.h>
#include <gtest/gtest.h>
#include <sodium.h>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
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
                / ("blogalone-admin-test-" + std::to_string(ticks) + "-" + std::to_string(attempt));
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

[[nodiscard]] drogon::orm::DbClientPtr fresh_admin_test_client(
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

class AdminServiceTest : public testing::Test {
  protected:
    AdminServiceTest()
        : client_{fresh_admin_test_client(workspace_.path() / "blogalone.db")}
        , service_{
            blogalone::repositories::AdminRepository{client_},
            blogalone::repositories::UserRepository{client_}
        }
    {
    }

    [[nodiscard]] std::int64_t insert_user(
        std::string username,
        blogalone::models::UserRole role = blogalone::models::UserRole::user,
        std::string_view password = "valid-password"
    )
    {
        const auto password_hash = blogalone::util::hash_password(
            password,
            fast_password_hash_options()
        );
        const auto rows = client_->execSqlSync(
            "INSERT INTO users (username, email, pwd_hash, role, created_at, updated_at) "
            "VALUES (?, NULL, ?, ?, 1, 1) RETURNING id",
            std::move(username),
            password_hash,
            std::string{blogalone::models::to_string(role)}
        );
        return rows.at(0)["id"].as<std::int64_t>();
    }

    [[nodiscard]] std::string insert_session(std::int64_t user_id, std::string token)
    {
        const auto token_hash = blogalone::util::sha256_hex(token);
        client_->execSqlSync(
            "INSERT INTO sessions (token_hash, user_id, csrf_hash, created_at, expires_at, ip, user_agent) "
            "VALUES (?, ?, ?, 1, 10000, '127.0.0.1', 'test')",
            token_hash,
            user_id,
            blogalone::util::sha256_hex("csrf")
        );
        return token_hash;
    }

    [[nodiscard]] std::int64_t insert_forum(std::string slug = "general")
    {
        const auto rows = client_->execSqlSync(
            "INSERT INTO forums (slug, name, description, sort_order, created_at, updated_at) "
            "VALUES (?, 'General', '', 0, 1, 1) RETURNING id",
            std::move(slug)
        );
        return rows.at(0)["id"].as<std::int64_t>();
    }

    TempWorkspace workspace_;
    drogon::orm::DbClientPtr client_;
    blogalone::services::AdminService service_;
};

}

TEST_F(AdminServiceTest, ManagesForumsAndWritesAuditLog)
{
    const auto admin_id = insert_user("forum_admin", blogalone::models::UserRole::admin);

    const auto created = service_.create_forum(
        admin_id,
        blogalone::services::CreateForumRequest{
            .slug = "news",
            .name = "News",
            .description = "Updates",
            .sort_order = 2
        },
        10
    );
    ASSERT_TRUE(created.has_value());
    EXPECT_EQ(created->slug, "news");

    const auto updated = service_.update_forum(
        admin_id,
        created->id,
        blogalone::services::UpdateForumRequest{
            .slug = std::nullopt,
            .name = "Announcements",
            .description = std::nullopt,
            .sort_order = 1
        },
        11
    );
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated->name, "Announcements");
    EXPECT_EQ(updated->sort_order, 1);

    const auto deleted = service_.delete_forum(admin_id, created->id, 12);
    ASSERT_TRUE(deleted.has_value());

    const auto logs = service_.list_audit_logs(
        admin_id,
        blogalone::services::AdminPaginationRequest{.page = 1, .page_size = 20}
    );
    ASSERT_TRUE(logs.has_value());
    ASSERT_EQ(logs->items.size(), 3);
    EXPECT_EQ(logs->items.at(0).action, "forum.delete");
    EXPECT_EQ(logs->items.at(2).action, "forum.create");
}

TEST_F(AdminServiceTest, RejectsDeletingForumThatStillOwnsThreads)
{
    const auto admin_id = insert_user("delete_admin", blogalone::models::UserRole::admin);
    const auto author_id = insert_user("thread_author");
    const auto forum_id = insert_forum();
    client_->execSqlSync(
        "INSERT INTO threads (forum_id, author_id, title, body_md, body_html, created_at, updated_at) "
        "VALUES (?, ?, 'Title', 'Body', '<p>Body</p>', 1, 1)",
        forum_id,
        author_id
    );

    const auto result = service_.delete_forum(admin_id, forum_id, 20);

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), blogalone::services::AdminError::conflict);
}

TEST_F(AdminServiceTest, ModeratesAndRestoresThreadPostAndSubPost)
{
    const auto admin_id = insert_user("content_admin", blogalone::models::UserRole::admin);
    const auto author_id = insert_user("content_author");
    const auto forum_id = insert_forum();
    const auto thread_rows = client_->execSqlSync(
        "INSERT INTO threads (forum_id, author_id, title, body_md, body_html, reply_count, "
        "last_reply_at, last_reply_user_id, created_at, updated_at) "
        "VALUES (?, ?, 'Title', 'Body', '<p>Body</p>', 1, 20, ?, 10, 10) RETURNING id",
        forum_id,
        author_id,
        author_id
    );
    const auto thread_id = thread_rows.at(0)["id"].as<std::int64_t>();
    const auto post_rows = client_->execSqlSync(
        "INSERT INTO posts (thread_id, author_id, floor_no, body_md, body_html, created_at, updated_at) "
        "VALUES (?, ?, 1, 'Reply', '<p>Reply</p>', 20, 20) RETURNING id",
        thread_id,
        author_id
    );
    const auto post_id = post_rows.at(0)["id"].as<std::int64_t>();
    const auto sub_post_rows = client_->execSqlSync(
        "INSERT INTO sub_posts (post_id, author_id, body_md, body_html, created_at, updated_at) "
        "VALUES (?, ?, 'Nested', '<p>Nested</p>', 30, 30) RETURNING id",
        post_id,
        author_id
    );
    const auto sub_post_id = sub_post_rows.at(0)["id"].as<std::int64_t>();

    ASSERT_TRUE(service_.set_thread_pinned(admin_id, thread_id, true, 40).has_value());
    ASSERT_TRUE(service_.set_thread_featured(admin_id, thread_id, true, 41).has_value());
    ASSERT_TRUE(service_.set_thread_deleted(admin_id, thread_id, true, 42).has_value());
    ASSERT_TRUE(service_.set_thread_deleted(admin_id, thread_id, false, 43).has_value());
    ASSERT_TRUE(service_.set_post_deleted(admin_id, post_id, true, 44).has_value());
    ASSERT_TRUE(service_.set_post_deleted(admin_id, post_id, false, 45).has_value());
    ASSERT_TRUE(service_.set_sub_post_deleted(admin_id, sub_post_id, true, 46).has_value());
    ASSERT_TRUE(service_.set_sub_post_deleted(admin_id, sub_post_id, false, 47).has_value());

    const auto thread_state = client_->execSqlSync(
        "SELECT is_pinned, is_featured, is_deleted, reply_count, last_reply_at FROM threads WHERE id = ?",
        thread_id
    );
    ASSERT_EQ(thread_state.size(), 1);
    EXPECT_EQ(thread_state.at(0)["is_pinned"].as<int>(), 1);
    EXPECT_EQ(thread_state.at(0)["is_featured"].as<int>(), 1);
    EXPECT_EQ(thread_state.at(0)["is_deleted"].as<int>(), 0);
    EXPECT_EQ(thread_state.at(0)["reply_count"].as<int>(), 1);
    EXPECT_EQ(thread_state.at(0)["last_reply_at"].as<std::int64_t>(), 30);

    const auto content_state = client_->execSqlSync(
        "SELECT p.is_deleted AS post_deleted, sp.is_deleted AS sub_deleted "
        "FROM posts p JOIN sub_posts sp ON sp.post_id = p.id WHERE p.id = ?",
        post_id
    );
    EXPECT_EQ(content_state.at(0)["post_deleted"].as<int>(), 0);
    EXPECT_EQ(content_state.at(0)["sub_deleted"].as<int>(), 0);
}

TEST_F(AdminServiceTest, ReauthenticatesAndRevokesSessionsWhenAdminIsDemoted)
{
    const auto admin_id = insert_user("role_admin", blogalone::models::UserRole::admin);
    const auto user_id = insert_user("role_user");
    const auto admin_token_hash = insert_session(admin_id, "admin-session");
    const auto user_token_hash = insert_session(user_id, "user-session");

    const auto denied = service_.update_user_role(
        admin_id,
        std::nullopt,
        user_id,
        blogalone::models::UserRole::admin,
        100
    );
    ASSERT_FALSE(denied.has_value());
    EXPECT_EQ(denied.error(), blogalone::services::AdminError::reauth_required);

    const auto reauth = service_.reauth(admin_id, admin_token_hash, "valid-password", 100);
    ASSERT_TRUE(reauth.has_value());
    EXPECT_EQ(reauth->confirmed_at, 100);

    const auto promoted = service_.update_user_role(
        admin_id,
        100,
        user_id,
        blogalone::models::UserRole::admin,
        101
    );
    ASSERT_TRUE(promoted.has_value());
    EXPECT_EQ(promoted->role, blogalone::models::UserRole::admin);

    const auto demoted = service_.update_user_role(
        admin_id,
        100,
        user_id,
        blogalone::models::UserRole::user,
        102
    );
    ASSERT_TRUE(demoted.has_value());
    EXPECT_EQ(demoted->role, blogalone::models::UserRole::user);

    const auto session = blogalone::repositories::SessionRepository{client_}.find_by_token_hash(
        user_token_hash
    );
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->revoked_at, std::optional<std::int64_t>{102});
}

TEST_F(AdminServiceTest, RequiresRecentReauthForBanAndSessionRevocation)
{
    const auto admin_id = insert_user("security_admin", blogalone::models::UserRole::admin);
    const auto user_id = insert_user("security_user");
    const auto token_hash = insert_session(user_id, "target-session");

    const auto expired = service_.update_user_ban(admin_id, 100, user_id, 1'000, 701);
    ASSERT_FALSE(expired.has_value());
    EXPECT_EQ(expired.error(), blogalone::services::AdminError::reauth_required);

    const auto banned = service_.update_user_ban(admin_id, 200, user_id, 1'000, 201);
    ASSERT_TRUE(banned.has_value());
    EXPECT_EQ(banned->banned_until, std::optional<std::int64_t>{1'000});

    const auto revoked = service_.revoke_session(admin_id, 200, token_hash, 202);
    ASSERT_TRUE(revoked.has_value());
    const auto session = blogalone::repositories::SessionRepository{client_}.find_by_token_hash(
        token_hash
    );
    ASSERT_TRUE(session.has_value());
    EXPECT_EQ(session->revoked_at, std::optional<std::int64_t>{202});
}

TEST_F(AdminServiceTest, ListsUsersByRoleWithPagination)
{
    const auto admin_id = insert_user("list_admin", blogalone::models::UserRole::admin);
    static_cast<void>(insert_user("list_user_one"));
    static_cast<void>(insert_user("list_user_two"));

    const auto page = service_.list_users(
        admin_id,
        blogalone::models::UserRole::user,
        blogalone::services::AdminPaginationRequest{.page = 2, .page_size = 1}
    );

    ASSERT_TRUE(page.has_value());
    EXPECT_EQ(page->total, 2);
    ASSERT_EQ(page->items.size(), 1);
    EXPECT_EQ(page->items.at(0).username, "list_user_two");
}

TEST(RequireAdminFilterTest, RejectsUsersAndAcceptsAdmins)
{
    blogalone::filters::RequireAdminFilter filter;
    auto user_request = drogon::HttpRequest::newHttpRequest();
    blogalone::http::set_session_context(user_request, blogalone::http::SessionContext{
        .user_id = 1,
        .role = blogalone::models::UserRole::user,
        .token_hash = "hash",
        .csrf_hash = "csrf",
        .admin_confirmed_at = std::nullopt
    });
    bool user_failed = false;
    bool user_chained = false;
    filter.doFilter(
        user_request,
        [&user_failed](const drogon::HttpResponsePtr&) { user_failed = true; },
        [&user_chained]() { user_chained = true; }
    );
    EXPECT_TRUE(user_failed);
    EXPECT_FALSE(user_chained);

    auto admin_request = drogon::HttpRequest::newHttpRequest();
    blogalone::http::set_session_context(admin_request, blogalone::http::SessionContext{
        .user_id = 2,
        .role = blogalone::models::UserRole::admin,
        .token_hash = "hash",
        .csrf_hash = "csrf",
        .admin_confirmed_at = std::nullopt
    });
    bool admin_failed = false;
    bool admin_chained = false;
    filter.doFilter(
        admin_request,
        [&admin_failed](const drogon::HttpResponsePtr&) { admin_failed = true; },
        [&admin_chained]() { admin_chained = true; }
    );
    EXPECT_FALSE(admin_failed);
    EXPECT_TRUE(admin_chained);
}
