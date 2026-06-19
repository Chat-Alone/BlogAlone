#include "db/migrations.h"
#include "repositories/forum_repository.h"
#include "repositories/upload_repository.h"
#include "repositories/user_repository.h"
#include "services/forum_service.h"

#include <drogon/drogon.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <barrier>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

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
        , service_{
            blogalone::repositories::ForumRepository{client_},
            blogalone::repositories::UserRepository{client_},
            blogalone::repositories::UploadRepository{client_}
        }
    {
    }

    [[nodiscard]] std::int64_t insert_user(
        std::string username,
        std::optional<std::int64_t> banned_until = std::nullopt
    )
    {
        if(banned_until.has_value()) {
            const auto rows = client_->execSqlSync(
                "INSERT INTO users (username, email, pwd_hash, banned_until, created_at, updated_at) "
                "VALUES (?, NULL, 'hash', ?, 1, 1) RETURNING id",
                std::move(username),
                *banned_until
            );
            return rows.at(0)["id"].as<std::int64_t>();
        }

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

    [[nodiscard]] std::string insert_upload_ref(std::int64_t owner_id, std::string path)
    {
        const auto rows = client_->execSqlSync(
            "INSERT INTO uploads (sha256, path, mime, size, width, height, created_at) "
            "VALUES (?, ?, 'image/png', 68, 1, 1, 1) RETURNING id",
            "sha-" + path,
            path
        );
        const auto upload_id = rows.at(0)["id"].as<std::int64_t>();
        client_->execSqlSync(
            "INSERT INTO upload_refs (owner_id, upload_id, created_at) VALUES (?, ?, 1)",
            owner_id,
            upload_id
        );
        return path;
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
    EXPECT_EQ(created->body_html, "<p>First body</p>\n");
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

    const auto zero_page = service_.list_threads(
        "general",
        blogalone::services::PaginationRequest{.page = 0, .page_size = 20}
    );
    const auto negative_page = service_.list_threads(
        "general",
        blogalone::services::PaginationRequest{.page = -1, .page_size = 20}
    );
    const auto oversized_page_size = service_.list_threads(
        "general",
        blogalone::services::PaginationRequest{.page = 1, .page_size = 51}
    );

    ASSERT_FALSE(zero_page.has_value());
    ASSERT_FALSE(negative_page.has_value());
    ASSERT_FALSE(oversized_page_size.has_value());
    EXPECT_EQ(zero_page.error(), blogalone::services::ForumError::invalid_input);
    EXPECT_EQ(negative_page.error(), blogalone::services::ForumError::invalid_input);
    EXPECT_EQ(oversized_page_size.error(), blogalone::services::ForumError::invalid_input);
}

TEST_F(ForumServiceTest, AllowsMaxPageSizeAndReturnsEmptyPagePastEnd)
{
    const auto author_id = insert_user("page_boundary_author");
    static_cast<void>(insert_forum("general"));
    const auto thread = create_thread(author_id);
    ASSERT_TRUE(thread.has_value());

    const auto threads = service_.list_threads(
        "general",
        blogalone::services::PaginationRequest{.page = 2, .page_size = 50}
    );
    const auto detail = service_.get_thread(
        thread->id,
        blogalone::services::PaginationRequest{.page = 2, .page_size = 50}
    );

    ASSERT_TRUE(threads.has_value());
    ASSERT_TRUE(detail.has_value());
    EXPECT_TRUE(threads->items.empty());
    EXPECT_EQ(threads->page_size, 50);
    EXPECT_EQ(threads->total, 1);
    EXPECT_TRUE(detail->posts.items.empty());
    EXPECT_EQ(detail->posts.page_size, 50);
    EXPECT_EQ(detail->posts.total, 0);
}

TEST_F(ForumServiceTest, RejectsPaginationThatWouldOverflowOffset)
{
    const auto author_id = insert_user("huge_page_author");
    static_cast<void>(insert_forum("general"));
    const auto thread = create_thread(author_id);
    ASSERT_TRUE(thread.has_value());

    const auto threads = service_.list_threads(
        "general",
        blogalone::services::PaginationRequest{
            .page = (std::numeric_limits<std::int64_t>::max)(),
            .page_size = 50
        }
    );
    const auto detail = service_.get_thread(
        thread->id,
        blogalone::services::PaginationRequest{
            .page = (std::numeric_limits<std::int64_t>::max)(),
            .page_size = 50
        }
    );

    ASSERT_FALSE(threads.has_value());
    ASSERT_FALSE(detail.has_value());
    EXPECT_EQ(threads.error(), blogalone::services::ForumError::invalid_input);
    EXPECT_EQ(detail.error(), blogalone::services::ForumError::invalid_input);
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

TEST_F(ForumServiceTest, CreatesConcurrentPostsWithUniqueFloors)
{
    const auto author_id = insert_user("concurrent_author");
    static_cast<void>(insert_forum("general"));
    const auto thread = create_thread(author_id);
    ASSERT_TRUE(thread.has_value());

    constexpr int kReplyCount = 12;
    std::barrier<> start{kReplyCount};
    std::mutex results_mutex;
    std::vector<std::int64_t> floors;
    std::vector<blogalone::services::ForumError> errors;
    std::vector<std::thread> workers;
    workers.reserve(kReplyCount);

    for(int index = 0; index < kReplyCount; ++index) {
        workers.emplace_back([&, index]() {
            start.arrive_and_wait();
            const auto result = service_.create_post(
                author_id,
                blogalone::services::CreatePostRequest{
                    .thread_id = thread->id,
                    .body_md = "concurrent reply " + std::to_string(index)
                },
                100 + index
            );

            const std::scoped_lock lock{results_mutex};
            if(result.has_value()) {
                floors.push_back(result->post.floor_no);
            } else {
                errors.push_back(result.error());
            }
        });
    }

    for(auto& worker : workers) {
        worker.join();
    }
    std::ranges::sort(floors);

    EXPECT_TRUE(errors.empty());
    ASSERT_EQ(floors.size(), kReplyCount);
    for(std::int64_t index = 0; index < kReplyCount; ++index) {
        EXPECT_EQ(floors.at(static_cast<std::size_t>(index)), index + 1);
    }

    const auto detail = service_.get_thread(
        thread->id,
        blogalone::services::PaginationRequest{.page = 1, .page_size = 20}
    );
    ASSERT_TRUE(detail.has_value());
    EXPECT_EQ(detail->thread.reply_count, kReplyCount);
}

TEST_F(ForumServiceTest, ReturnsConflictWhenPostFloorCollisionPersists)
{
    const auto author_id = insert_user("conflict_author");
    static_cast<void>(insert_forum("general"));
    const auto thread = create_thread(author_id);
    ASSERT_TRUE(thread.has_value());
    client_->execSqlSync(
        "CREATE TRIGGER collide_post_floor BEFORE INSERT ON posts "
        "WHEN NEW.body_md <> 'shadow' "
        "BEGIN "
        "INSERT INTO posts (thread_id, author_id, floor_no, body_md, body_html, created_at, updated_at) "
        "VALUES (NEW.thread_id, NEW.author_id, NEW.floor_no, 'shadow', '<p>shadow</p>', "
        "NEW.created_at, NEW.updated_at); "
        "END;"
    );

    const auto result = service_.create_post(
        author_id,
        blogalone::services::CreatePostRequest{
            .thread_id = thread->id,
            .body_md = "colliding reply"
        },
        30
    );
    const auto rows = client_->execSqlSync(
        "SELECT COUNT(*) AS count FROM posts WHERE thread_id = ?",
        thread->id
    );

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), blogalone::services::ForumError::conflict);
    EXPECT_EQ(rows.at(0)["count"].as<std::int64_t>(), 0);
}

TEST_F(ForumServiceTest, RollsBackPostWhenThreadIsDeletedDuringCreate)
{
    const auto author_id = insert_user("thread_race_author");
    static_cast<void>(insert_forum("general"));
    const auto thread = create_thread(author_id);
    ASSERT_TRUE(thread.has_value());
    client_->execSqlSync(
        "CREATE TRIGGER delete_thread_before_reply BEFORE INSERT ON posts "
        "WHEN NEW.body_md = 'race reply' "
        "BEGIN "
        "UPDATE threads SET is_deleted = 1 WHERE id = NEW.thread_id; "
        "END;"
    );

    const auto result = service_.create_post(
        author_id,
        blogalone::services::CreatePostRequest{
            .thread_id = thread->id,
            .body_md = "race reply"
        },
        30
    );
    const auto rows = client_->execSqlSync(
        "SELECT t.is_deleted, t.reply_count, COUNT(p.id) AS post_count "
        "FROM threads t LEFT JOIN posts p ON p.thread_id = t.id "
        "WHERE t.id = ? GROUP BY t.id",
        thread->id
    );

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), blogalone::services::ForumError::not_found);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows.at(0)["is_deleted"].as<int>(), 0);
    EXPECT_EQ(rows.at(0)["reply_count"].as<std::int64_t>(), 0);
    EXPECT_EQ(rows.at(0)["post_count"].as<std::int64_t>(), 0);
}

TEST_F(ForumServiceTest, RollsBackThreadWhenUploadAttachFails)
{
    const auto author_id = insert_user("attach_failure_author");
    static_cast<void>(insert_forum("general"));
    const auto upload_path = insert_upload_ref(author_id, "2026/06/aa/x.png");
    client_->execSqlSync(
        "CREATE TRIGGER fail_upload_attach BEFORE UPDATE OF attached_at ON upload_refs "
        "WHEN NEW.attached_at IS NOT NULL "
        "BEGIN "
        "SELECT RAISE(ABORT, 'attach failed'); "
        "END;"
    );

    EXPECT_THROW(
        static_cast<void>(service_.create_thread(
            author_id,
            blogalone::services::CreateThreadRequest{
                .forum_slug = "general",
                .title = "Thread with image",
                .body_md = "![img](/uploads/" + upload_path + ")"
            },
            30
        )),
        drogon::orm::DrogonDbException
    );

    const auto rows = client_->execSqlSync(
        "SELECT "
        "(SELECT COUNT(*) FROM threads WHERE author_id = ?) AS thread_count, "
        "(SELECT attached_at FROM upload_refs WHERE owner_id = ?) AS attached_at",
        author_id,
        author_id
    );
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows.at(0)["thread_count"].as<std::int64_t>(), 0);
    EXPECT_TRUE(rows.at(0)["attached_at"].isNull());
}

TEST_F(ForumServiceTest, RollsBackThreadWhenUploadReferenceDisappears)
{
    const auto author_id = insert_user("missing_upload_author");
    static_cast<void>(insert_forum("general"));
    const auto upload_path = insert_upload_ref(author_id, "2026/06/aa/missing.png");
    client_->execSqlSync(
        "CREATE TRIGGER remove_upload_ref BEFORE INSERT ON threads "
        "BEGIN "
        "DELETE FROM upload_refs WHERE owner_id = NEW.author_id; "
        "END;"
    );

    const auto result = service_.create_thread(
        author_id,
        blogalone::services::CreateThreadRequest{
            .forum_slug = "general",
            .title = "Thread with disappearing image",
            .body_md = "![image](/uploads/" + upload_path + ")"
        },
        1'700'000'000
    );

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), blogalone::services::ForumError::not_found);
    EXPECT_EQ(client_->execSqlSync("SELECT id FROM threads").size(), 0);
    EXPECT_EQ(client_->execSqlSync("SELECT id FROM upload_refs").size(), 1);
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

TEST_F(ForumServiceTest, RollsBackSubPostWhenPostIsDeletedDuringCreate)
{
    const auto author_id = insert_user("sub_race_author");
    const auto replier_id = insert_user("sub_race_replier");
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
    client_->execSqlSync(
        "CREATE TRIGGER delete_post_before_sub_reply BEFORE INSERT ON sub_posts "
        "WHEN NEW.body_md = 'race nested' "
        "BEGIN "
        "UPDATE posts SET is_deleted = 1 WHERE id = NEW.post_id; "
        "END;"
    );

    const auto result = service_.create_sub_post(
        replier_id,
        blogalone::services::CreateSubPostRequest{
            .post_id = post->post.id,
            .body_md = "race nested",
            .reply_to_user_id = author_id
        },
        41
    );
    const auto rows = client_->execSqlSync(
        "SELECT p.is_deleted, t.reply_count, t.last_reply_at, COUNT(sp.id) AS sub_post_count "
        "FROM posts p "
        "JOIN threads t ON t.id = p.thread_id "
        "LEFT JOIN sub_posts sp ON sp.post_id = p.id "
        "WHERE p.id = ? GROUP BY p.id",
        post->post.id
    );

    ASSERT_FALSE(result.has_value());
    EXPECT_EQ(result.error(), blogalone::services::ForumError::not_found);
    ASSERT_EQ(rows.size(), 1);
    EXPECT_EQ(rows.at(0)["is_deleted"].as<int>(), 0);
    EXPECT_EQ(rows.at(0)["reply_count"].as<std::int64_t>(), 1);
    EXPECT_EQ(rows.at(0)["last_reply_at"].as<std::int64_t>(), 40);
    EXPECT_EQ(rows.at(0)["sub_post_count"].as<std::int64_t>(), 0);
}

TEST_F(ForumServiceTest, RepositoryRefusesUpdatesForHiddenContent)
{
    const blogalone::repositories::ForumRepository repository{client_};
    const auto author_id = insert_user("hidden_update_author");
    static_cast<void>(insert_forum("general"));

    const auto hidden_thread = create_thread(author_id, "general", 20);
    ASSERT_TRUE(hidden_thread.has_value());
    client_->execSqlSync("UPDATE threads SET is_deleted = 1 WHERE id = ?", hidden_thread->id);

    EXPECT_FALSE(repository.update_thread_content(
        hidden_thread->id,
        "Changed title",
        "Changed body",
        "<p>Changed body</p>",
        21
    ));
    const auto thread_rows = client_->execSqlSync(
        "SELECT title, body_md FROM threads WHERE id = ?",
        hidden_thread->id
    );
    ASSERT_EQ(thread_rows.size(), 1);
    EXPECT_EQ(thread_rows.at(0)["title"].as<std::string>(), "Hello thread");
    EXPECT_EQ(thread_rows.at(0)["body_md"].as<std::string>(), "First body");

    const auto thread_with_hidden_parent = create_thread(author_id, "general", 30);
    ASSERT_TRUE(thread_with_hidden_parent.has_value());
    const auto post = service_.create_post(
        author_id,
        blogalone::services::CreatePostRequest{
            .thread_id = thread_with_hidden_parent->id,
            .body_md = "floor reply"
        },
        31
    );
    ASSERT_TRUE(post.has_value());
    client_->execSqlSync(
        "UPDATE threads SET is_deleted = 1 WHERE id = ?",
        thread_with_hidden_parent->id
    );

    EXPECT_FALSE(repository.update_post_content(
        post->post.id,
        "Changed floor",
        "<p>Changed floor</p>",
        32
    ));
    const auto post_rows = client_->execSqlSync(
        "SELECT body_md FROM posts WHERE id = ?",
        post->post.id
    );
    ASSERT_EQ(post_rows.size(), 1);
    EXPECT_EQ(post_rows.at(0)["body_md"].as<std::string>(), "floor reply");

    const auto nested_thread = create_thread(author_id, "general", 40);
    ASSERT_TRUE(nested_thread.has_value());
    const auto visible_post = service_.create_post(
        author_id,
        blogalone::services::CreatePostRequest{
            .thread_id = nested_thread->id,
            .body_md = "visible floor"
        },
        41
    );
    ASSERT_TRUE(visible_post.has_value());
    const auto sub_post = service_.create_sub_post(
        author_id,
        blogalone::services::CreateSubPostRequest{
            .post_id = visible_post->post.id,
            .body_md = "nested reply",
            .reply_to_user_id = std::nullopt
        },
        42
    );
    ASSERT_TRUE(sub_post.has_value());
    client_->execSqlSync("UPDATE posts SET is_deleted = 1 WHERE id = ?", visible_post->post.id);

    EXPECT_FALSE(repository.update_sub_post_content(
        sub_post->id,
        "Changed nested",
        "<p>Changed nested</p>",
        43
    ));
    const auto sub_post_rows = client_->execSqlSync(
        "SELECT body_md FROM sub_posts WHERE id = ?",
        sub_post->id
    );
    ASSERT_EQ(sub_post_rows.size(), 1);
    EXPECT_EQ(sub_post_rows.at(0)["body_md"].as<std::string>(), "nested reply");
}

TEST_F(ForumServiceTest, RepositoryRefusesDeletesForHiddenContent)
{
    const blogalone::repositories::ForumRepository repository{client_};
    const auto author_id = insert_user("hidden_delete_author");
    static_cast<void>(insert_forum("general"));

    const auto hidden_thread = create_thread(author_id, "general", 20);
    ASSERT_TRUE(hidden_thread.has_value());
    client_->execSqlSync("UPDATE threads SET is_deleted = 1 WHERE id = ?", hidden_thread->id);

    EXPECT_FALSE(repository.soft_delete_thread(hidden_thread->id, 21));
    const auto thread_rows = client_->execSqlSync(
        "SELECT deleted_at FROM threads WHERE id = ?",
        hidden_thread->id
    );
    ASSERT_EQ(thread_rows.size(), 1);
    EXPECT_TRUE(thread_rows.at(0)["deleted_at"].isNull());

    const auto thread_with_hidden_parent = create_thread(author_id, "general", 30);
    ASSERT_TRUE(thread_with_hidden_parent.has_value());
    const auto post = service_.create_post(
        author_id,
        blogalone::services::CreatePostRequest{
            .thread_id = thread_with_hidden_parent->id,
            .body_md = "floor reply"
        },
        31
    );
    ASSERT_TRUE(post.has_value());
    client_->execSqlSync(
        "UPDATE threads SET is_deleted = 1 WHERE id = ?",
        thread_with_hidden_parent->id
    );

    EXPECT_FALSE(repository.soft_delete_post(post->post.id, 32));
    const auto post_rows = client_->execSqlSync(
        "SELECT is_deleted, deleted_at FROM posts WHERE id = ?",
        post->post.id
    );
    ASSERT_EQ(post_rows.size(), 1);
    EXPECT_EQ(post_rows.at(0)["is_deleted"].as<int>(), 0);
    EXPECT_TRUE(post_rows.at(0)["deleted_at"].isNull());

    const auto nested_thread = create_thread(author_id, "general", 40);
    ASSERT_TRUE(nested_thread.has_value());
    const auto visible_post = service_.create_post(
        author_id,
        blogalone::services::CreatePostRequest{
            .thread_id = nested_thread->id,
            .body_md = "visible floor"
        },
        41
    );
    ASSERT_TRUE(visible_post.has_value());
    const auto sub_post = service_.create_sub_post(
        author_id,
        blogalone::services::CreateSubPostRequest{
            .post_id = visible_post->post.id,
            .body_md = "nested reply",
            .reply_to_user_id = std::nullopt
        },
        42
    );
    ASSERT_TRUE(sub_post.has_value());
    client_->execSqlSync("UPDATE posts SET is_deleted = 1 WHERE id = ?", visible_post->post.id);

    EXPECT_FALSE(repository.soft_delete_sub_post(sub_post->id, 43));
    const auto sub_post_rows = client_->execSqlSync(
        "SELECT is_deleted, deleted_at FROM sub_posts WHERE id = ?",
        sub_post->id
    );
    ASSERT_EQ(sub_post_rows.size(), 1);
    EXPECT_EQ(sub_post_rows.at(0)["is_deleted"].as<int>(), 0);
    EXPECT_TRUE(sub_post_rows.at(0)["deleted_at"].isNull());
}

TEST_F(ForumServiceTest, RejectsInvalidForumAndBodyInputs)
{
    const auto author_id = insert_user("invalid_input_author");
    static_cast<void>(insert_forum("general"));
    const auto thread = create_thread(author_id);
    ASSERT_TRUE(thread.has_value());
    const auto post = service_.create_post(
        author_id,
        blogalone::services::CreatePostRequest{
            .thread_id = thread->id,
            .body_md = "floor reply"
        },
        30
    );
    ASSERT_TRUE(post.has_value());

    const auto invalid_slug_list = service_.list_threads(
        "BadSlug",
        blogalone::services::PaginationRequest{.page = 1, .page_size = 20}
    );
    const auto invalid_slug_create = service_.create_thread(
        author_id,
        blogalone::services::CreateThreadRequest{
            .forum_slug = "BadSlug",
            .title = "Valid title",
            .body_md = "Valid body"
        },
        31
    );
    const auto empty_post = service_.create_post(
        author_id,
        blogalone::services::CreatePostRequest{
            .thread_id = thread->id,
            .body_md = "   "
        },
        32
    );
    const auto oversized_post = service_.update_post(
        author_id,
        post->post.id,
        blogalone::services::UpdatePostRequest{.body_md = std::string(20'001, 'x')},
        33
    );
    const auto invalid_reply_target = service_.create_sub_post(
        author_id,
        blogalone::services::CreateSubPostRequest{
            .post_id = post->post.id,
            .body_md = "nested reply",
            .reply_to_user_id = 0
        },
        34
    );
    const auto oversized_sub_post = service_.create_sub_post(
        author_id,
        blogalone::services::CreateSubPostRequest{
            .post_id = post->post.id,
            .body_md = std::string(2'001, 'x'),
            .reply_to_user_id = std::nullopt
        },
        35
    );

    ASSERT_FALSE(invalid_slug_list.has_value());
    ASSERT_FALSE(invalid_slug_create.has_value());
    ASSERT_FALSE(empty_post.has_value());
    ASSERT_FALSE(oversized_post.has_value());
    ASSERT_FALSE(invalid_reply_target.has_value());
    ASSERT_FALSE(oversized_sub_post.has_value());
    EXPECT_EQ(invalid_slug_list.error(), blogalone::services::ForumError::invalid_input);
    EXPECT_EQ(invalid_slug_create.error(), blogalone::services::ForumError::invalid_input);
    EXPECT_EQ(empty_post.error(), blogalone::services::ForumError::invalid_input);
    EXPECT_EQ(oversized_post.error(), blogalone::services::ForumError::invalid_input);
    EXPECT_EQ(invalid_reply_target.error(), blogalone::services::ForumError::invalid_input);
    EXPECT_EQ(oversized_sub_post.error(), blogalone::services::ForumError::invalid_input);
}

TEST_F(ForumServiceTest, RejectsBannedAuthorsForCreates)
{
    const auto author_id = insert_user("active_author");
    const auto banned_id = insert_user("banned_author", 1'000);
    static_cast<void>(insert_forum("general"));
    const auto thread = create_thread(author_id);
    ASSERT_TRUE(thread.has_value());
    const auto post = service_.create_post(
        author_id,
        blogalone::services::CreatePostRequest{
            .thread_id = thread->id,
            .body_md = "visible reply"
        },
        30
    );
    ASSERT_TRUE(post.has_value());

    const auto banned_thread = service_.create_thread(
        banned_id,
        blogalone::services::CreateThreadRequest{
            .forum_slug = "general",
            .title = "Blocked thread",
            .body_md = "blocked body"
        },
        40
    );
    const auto banned_post = service_.create_post(
        banned_id,
        blogalone::services::CreatePostRequest{
            .thread_id = thread->id,
            .body_md = "blocked reply"
        },
        41
    );
    const auto banned_sub_post = service_.create_sub_post(
        banned_id,
        blogalone::services::CreateSubPostRequest{
            .post_id = post->post.id,
            .body_md = "blocked nested reply",
            .reply_to_user_id = author_id
        },
        42
    );
    const auto rows = client_->execSqlSync(
        "SELECT "
        "(SELECT COUNT(*) FROM threads WHERE author_id = ?) AS thread_count, "
        "(SELECT COUNT(*) FROM posts WHERE author_id = ?) AS post_count, "
        "(SELECT COUNT(*) FROM sub_posts WHERE author_id = ?) AS sub_post_count",
        banned_id,
        banned_id,
        banned_id
    );

    ASSERT_FALSE(banned_thread.has_value());
    ASSERT_FALSE(banned_post.has_value());
    ASSERT_FALSE(banned_sub_post.has_value());
    EXPECT_EQ(banned_thread.error(), blogalone::services::ForumError::forbidden);
    EXPECT_EQ(banned_post.error(), blogalone::services::ForumError::forbidden);
    EXPECT_EQ(banned_sub_post.error(), blogalone::services::ForumError::forbidden);
    EXPECT_EQ(rows.at(0)["thread_count"].as<std::int64_t>(), 0);
    EXPECT_EQ(rows.at(0)["post_count"].as<std::int64_t>(), 0);
    EXPECT_EQ(rows.at(0)["sub_post_count"].as<std::int64_t>(), 0);
}

TEST_F(ForumServiceTest, DeletingPostsRecomputesReplyCountAndLastReply)
{
    const auto author_id = insert_user("delete_post_author");
    const auto replier_id = insert_user("delete_post_replier");
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

    const auto deleted_second = service_.delete_post(author_id, second->post.id, 40);
    ASSERT_TRUE(deleted_second.has_value());
    const auto after_second_delete = service_.get_thread(
        thread->id,
        blogalone::services::PaginationRequest{.page = 1, .page_size = 20}
    );

    ASSERT_TRUE(after_second_delete.has_value());
    EXPECT_EQ(after_second_delete->thread.reply_count, 1);
    EXPECT_EQ(after_second_delete->thread.last_reply_at, std::optional<std::int64_t>{30});
    EXPECT_EQ(after_second_delete->thread.last_reply_user_id, std::optional<std::int64_t>{replier_id});
    ASSERT_EQ(after_second_delete->posts.items.size(), 1);
    EXPECT_EQ(after_second_delete->posts.items.at(0).post.id, first->post.id);

    const auto deleted_first = service_.delete_post(replier_id, first->post.id, 41);
    ASSERT_TRUE(deleted_first.has_value());
    const auto after_all_deleted = service_.get_thread(
        thread->id,
        blogalone::services::PaginationRequest{.page = 1, .page_size = 20}
    );

    ASSERT_TRUE(after_all_deleted.has_value());
    EXPECT_EQ(after_all_deleted->thread.reply_count, 0);
    EXPECT_EQ(after_all_deleted->thread.last_reply_at, std::optional<std::int64_t>{10});
    EXPECT_EQ(after_all_deleted->thread.last_reply_user_id, std::optional<std::int64_t>{author_id});
    EXPECT_TRUE(after_all_deleted->posts.items.empty());
}

TEST_F(ForumServiceTest, DeletingSubPostMovesLastReplyBackToVisiblePost)
{
    const auto author_id = insert_user("delete_sub_author");
    const auto replier_id = insert_user("delete_sub_replier");
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

    const auto deleted = service_.delete_sub_post(replier_id, sub_post->id, 42);
    ASSERT_TRUE(deleted.has_value());
    const auto detail = service_.get_thread(
        thread->id,
        blogalone::services::PaginationRequest{.page = 1, .page_size = 20}
    );

    ASSERT_TRUE(detail.has_value());
    EXPECT_EQ(detail->thread.reply_count, 1);
    EXPECT_EQ(detail->thread.last_reply_at, std::optional<std::int64_t>{40});
    EXPECT_EQ(detail->thread.last_reply_user_id, std::optional<std::int64_t>{author_id});
    ASSERT_EQ(detail->posts.items.size(), 1);
    EXPECT_TRUE(detail->posts.items.at(0).sub_posts.empty());
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

TEST_F(ForumServiceTest, AllowsOnlyOwnersToEditAndDeleteReplies)
{
    const auto author_id = insert_user("reply_owner_author");
    const auto other_id = insert_user("reply_owner_other");
    static_cast<void>(insert_forum("general"));
    const auto thread = create_thread(author_id);
    ASSERT_TRUE(thread.has_value());
    const auto post = service_.create_post(
        author_id,
        blogalone::services::CreatePostRequest{
            .thread_id = thread->id,
            .body_md = "floor reply"
        },
        60
    );
    ASSERT_TRUE(post.has_value());
    const auto sub_post = service_.create_sub_post(
        other_id,
        blogalone::services::CreateSubPostRequest{
            .post_id = post->post.id,
            .body_md = "nested reply",
            .reply_to_user_id = author_id
        },
        61
    );
    ASSERT_TRUE(sub_post.has_value());

    const auto forbidden_post_update = service_.update_post(
        other_id,
        post->post.id,
        blogalone::services::UpdatePostRequest{.body_md = "wrong owner"},
        62
    );
    const auto updated_post = service_.update_post(
        author_id,
        post->post.id,
        blogalone::services::UpdatePostRequest{.body_md = "updated floor"},
        63
    );
    const auto forbidden_sub_update = service_.update_sub_post(
        author_id,
        sub_post->id,
        blogalone::services::UpdateSubPostRequest{.body_md = "wrong owner"},
        64
    );
    const auto updated_sub = service_.update_sub_post(
        other_id,
        sub_post->id,
        blogalone::services::UpdateSubPostRequest{.body_md = "updated nested"},
        65
    );

    ASSERT_FALSE(forbidden_post_update.has_value());
    EXPECT_EQ(forbidden_post_update.error(), blogalone::services::ForumError::forbidden);
    ASSERT_TRUE(updated_post.has_value());
    EXPECT_EQ(updated_post->post.body_html, "<p>updated floor</p>\n");
    ASSERT_FALSE(forbidden_sub_update.has_value());
    EXPECT_EQ(forbidden_sub_update.error(), blogalone::services::ForumError::forbidden);
    ASSERT_TRUE(updated_sub.has_value());
    EXPECT_EQ(updated_sub->body_html, "<p>updated nested</p>\n");

    const auto forbidden_post_delete = service_.delete_post(other_id, post->post.id, 66);
    const auto forbidden_sub_delete = service_.delete_sub_post(author_id, sub_post->id, 67);
    const auto deleted_sub = service_.delete_sub_post(other_id, sub_post->id, 68);

    ASSERT_FALSE(forbidden_post_delete.has_value());
    EXPECT_EQ(forbidden_post_delete.error(), blogalone::services::ForumError::forbidden);
    ASSERT_FALSE(forbidden_sub_delete.has_value());
    EXPECT_EQ(forbidden_sub_delete.error(), blogalone::services::ForumError::forbidden);
    ASSERT_TRUE(deleted_sub.has_value());
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
    EXPECT_EQ(updated->body_html, "<p>Updated body</p>\n");

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
