#include "db/migrations.h"
#include "repositories/forum_repository.h"
#include "services/forum_service.h"

#include <drogon/drogon.h>
#include <gtest/gtest.h>

#include <chrono>
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
                / ("blogalone-forum-test-" + std::to_string(ticks) + "-" + std::to_string(attempt));
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

[[nodiscard]] drogon::orm::DbClientPtr fresh_forum_test_client(
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

class ForumServiceTest : public testing::Test {
  protected:
    ForumServiceTest()
        : client_{fresh_forum_test_client(workspace_.path() / "blogalone.db")}
        , service_{blogalone::repositories::ForumRepository{client_}}
    {
    }

    [[nodiscard]] std::int64_t insert_user(std::string username)
    {
        const auto rows = client_->execSqlSync(
            "INSERT INTO users (username, email, pwd_hash, created_at, updated_at) "
            "VALUES (?, NULL, 'hash', 1, 1) RETURNING id",
            std::move(username)
        );
        return rows.at(0)["id"].as<std::int64_t>();
    }

    [[nodiscard]] std::int64_t insert_forum(std::string slug, std::int64_t sort_order = 0)
    {
        const auto rows = client_->execSqlSync(
            "INSERT INTO forums (slug, name, description, sort_order, created_at, updated_at) "
            "VALUES (?, ?, 'description', ?, 1, 1) RETURNING id",
            slug,
            "Forum " + slug,
            sort_order
        );
        return rows.at(0)["id"].as<std::int64_t>();
    }

    [[nodiscard]] blogalone::services::ForumResult<blogalone::models::Thread> create_thread(
        std::int64_t author_id,
        std::string slug = "general",
        std::int64_t now = 10
    ) const
    {
        return service_.create_thread(
            author_id,
            blogalone::services::CreateThreadRequest{
                .forum_slug = std::move(slug),
                .title = "Hello thread",
                .body_md = "First body"
            },
            now
        );
    }

    TempWorkspace workspace_;
    drogon::orm::DbClientPtr client_;
    blogalone::services::ForumService service_;
};

}

TEST_F(ForumServiceTest, ListsForumsBySortOrder)
{
    static_cast<void>(insert_forum("second", 2));
    static_cast<void>(insert_forum("first", 1));

    const auto forums = service_.list_forums();

    ASSERT_EQ(forums.size(), 2);
    EXPECT_EQ(forums.at(0).slug, "first");
    EXPECT_EQ(forums.at(1).slug, "second");
}

TEST_F(ForumServiceTest, CreatesThreadAndListsIt)
{
    const auto author_id = insert_user("thread_author");
    static_cast<void>(insert_forum("general"));

    const auto created = create_thread(author_id, "general", 20);
    ASSERT_TRUE(created.has_value());
    EXPECT_EQ(created->title, "Hello thread");
    EXPECT_EQ(created->body_html, "<p>First body</p>");
    EXPECT_EQ(created->last_reply_at, std::optional<std::int64_t>{20});
    EXPECT_EQ(created->last_reply_user_id, std::optional<std::int64_t>{author_id});

    const auto page = service_.list_threads(
        "general",
        blogalone::services::PaginationRequest{.page = 1, .page_size = 20}
    );

    ASSERT_TRUE(page.has_value());
    ASSERT_EQ(page->items.size(), 1);
    EXPECT_EQ(page->total, 1);
    EXPECT_EQ(page->items.at(0).id, created->id);
}

TEST_F(ForumServiceTest, RejectsInvalidPagination)
{
    static_cast<void>(insert_forum("general"));

    const auto result = service_.list_threads(
        "general",
        blogalone::services::PaginationRequest{.page = 0, .page_size = 20}
    );

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), blogalone::services::ForumError::invalid_input);
}

TEST_F(ForumServiceTest, CreatesPostsWithSequentialFloorsAndReplyCount)
{
    const auto author_id = insert_user("post_author");
    const auto replier_id = insert_user("post_replier");
    static_cast<void>(insert_forum("general"));
    const auto thread = create_thread(author_id);
    ASSERT_TRUE(thread.has_value());

    const auto first = service_.create_post(
        replier_id,
        blogalone::services::CreatePostRequest{
            .thread_id = thread->id,
            .body_md = "first reply"
        },
        30
    );
    const auto second = service_.create_post(
        author_id,
        blogalone::services::CreatePostRequest{
            .thread_id = thread->id,
            .body_md = "second reply"
        },
        31
    );

    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    EXPECT_EQ(first->post.floor_no, 1);
    EXPECT_EQ(second->post.floor_no, 2);

    const auto detail = service_.get_thread(
        thread->id,
        blogalone::services::PaginationRequest{.page = 1, .page_size = 20}
    );

    ASSERT_TRUE(detail.has_value());
    EXPECT_EQ(detail->thread.reply_count, 2);
    EXPECT_EQ(detail->thread.last_reply_at, std::optional<std::int64_t>{31});
    EXPECT_EQ(detail->thread.last_reply_user_id, std::optional<std::int64_t>{author_id});
    ASSERT_EQ(detail->posts.items.size(), 2);
    EXPECT_EQ(detail->posts.items.at(0).post.id, first->post.id);
    EXPECT_EQ(detail->posts.items.at(1).post.id, second->post.id);
}

TEST_F(ForumServiceTest, CreatesSubPostsWithoutIncrementingReplyCount)
{
    const auto author_id = insert_user("sub_author");
    const auto replier_id = insert_user("sub_replier");
    static_cast<void>(insert_forum("general"));
    const auto thread = create_thread(author_id);
    ASSERT_TRUE(thread.has_value());
    const auto post = service_.create_post(
        author_id,
        blogalone::services::CreatePostRequest{
            .thread_id = thread->id,
            .body_md = "floor reply"
        },
        40
    );
    ASSERT_TRUE(post.has_value());

    const auto sub_post = service_.create_sub_post(
        replier_id,
        blogalone::services::CreateSubPostRequest{
            .post_id = post->post.id,
            .body_md = "nested reply",
            .reply_to_user_id = author_id
        },
        41
    );

    ASSERT_TRUE(sub_post.has_value());
    EXPECT_EQ(sub_post->post_id, post->post.id);
    EXPECT_EQ(sub_post->reply_to_user_id, std::optional<std::int64_t>{author_id});

    const auto detail = service_.get_thread(
        thread->id,
        blogalone::services::PaginationRequest{.page = 1, .page_size = 20}
    );

    ASSERT_TRUE(detail.has_value());
    EXPECT_EQ(detail->thread.reply_count, 1);
    EXPECT_EQ(detail->thread.last_reply_at, std::optional<std::int64_t>{41});
    ASSERT_EQ(detail->posts.items.size(), 1);
    ASSERT_EQ(detail->posts.items.at(0).sub_posts.size(), 1);
    EXPECT_EQ(detail->posts.items.at(0).sub_posts.at(0).id, sub_post->id);
}

TEST_F(ForumServiceTest, RejectsMissingReplyTargetUser)
{
    const auto author_id = insert_user("missing_reply_target_author");
    static_cast<void>(insert_forum("general"));
    const auto thread = create_thread(author_id);
    ASSERT_TRUE(thread.has_value());
    const auto post = service_.create_post(
        author_id,
        blogalone::services::CreatePostRequest{
            .thread_id = thread->id,
            .body_md = "floor reply"
        },
        45
    );
    ASSERT_TRUE(post.has_value());

    const auto sub_post = service_.create_sub_post(
        author_id,
        blogalone::services::CreateSubPostRequest{
            .post_id = post->post.id,
            .body_md = "nested reply",
            .reply_to_user_id = 9'999
        },
        46
    );

    ASSERT_FALSE(sub_post.has_value());
    EXPECT_EQ(sub_post.error(), blogalone::services::ForumError::not_found);
}

TEST_F(ForumServiceTest, AllowsOnlyOwnersToEditAndDeleteContent)
{
    const auto author_id = insert_user("owner_user");
    const auto other_id = insert_user("other_user");
    static_cast<void>(insert_forum("general"));
    const auto thread = create_thread(author_id);
    ASSERT_TRUE(thread.has_value());

    const auto forbidden_update = service_.update_thread(
        other_id,
        thread->id,
        blogalone::services::UpdateThreadRequest{
            .title = "Other title",
            .body_md = "Other body"
        },
        50
    );
    ASSERT_FALSE(forbidden_update.has_value());
    EXPECT_EQ(forbidden_update.error(), blogalone::services::ForumError::forbidden);

    const auto updated = service_.update_thread(
        author_id,
        thread->id,
        blogalone::services::UpdateThreadRequest{
            .title = "Updated title",
            .body_md = "Updated body"
        },
        51
    );
    ASSERT_TRUE(updated.has_value());
    EXPECT_EQ(updated->title, "Updated title");
    EXPECT_EQ(updated->body_html, "<p>Updated body</p>");

    const auto forbidden_delete = service_.delete_thread(other_id, thread->id, 52);
    ASSERT_FALSE(forbidden_delete.has_value());
    EXPECT_EQ(forbidden_delete.error(), blogalone::services::ForumError::forbidden);

    const auto deleted = service_.delete_thread(author_id, thread->id, 53);
    ASSERT_TRUE(deleted.has_value());

    const auto after_delete = service_.get_thread(
        thread->id,
        blogalone::services::PaginationRequest{.page = 1, .page_size = 20}
    );
    ASSERT_FALSE(after_delete.has_value());
    EXPECT_EQ(after_delete.error(), blogalone::services::ForumError::not_found);
}
