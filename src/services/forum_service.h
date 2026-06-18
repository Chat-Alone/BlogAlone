#pragma once

#include "models/forum.h"
#include "repositories/forum_repository.h"
#include "repositories/upload_repository.h"
#include "repositories/user_repository.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace blogalone::services {

inline constexpr std::int64_t kDefaultPageSize = 20;

struct PaginationRequest {
    std::int64_t page{1};
    std::int64_t page_size{kDefaultPageSize};
};

template <typename T>
struct Page {
    std::vector<T> items;
    std::int64_t page{};
    std::int64_t page_size{};
    std::int64_t total{};
};

struct ThreadDetail {
    models::Thread thread;
    Page<models::PostWithReplies> posts;
};

struct CreateThreadRequest {
    std::string forum_slug;
    std::string title;
    std::string body_md;
};

struct UpdateThreadRequest {
    std::string title;
    std::string body_md;
};

struct CreatePostRequest {
    std::int64_t thread_id{};
    std::string body_md;
};

struct UpdatePostRequest {
    std::string body_md;
};

struct CreateSubPostRequest {
    std::int64_t post_id{};
    std::string body_md;
    std::optional<std::int64_t> reply_to_user_id;
};

struct UpdateSubPostRequest {
    std::string body_md;
};

struct DeleteResult {
};

enum class ForumError {
    invalid_input,
    not_found,
    forbidden,
    conflict
};

[[nodiscard]] std::string_view to_string(ForumError error);

template <typename T>
class ForumResult {
  public:
    ForumResult(T value) : storage_{std::move(value)} {}
    ForumResult(ForumError error) : storage_{error} {}

    [[nodiscard]] bool has_value() const { return storage_.index() == 0; }
    [[nodiscard]] explicit operator bool() const { return has_value(); }
    [[nodiscard]] const T& value() const { return std::get<0>(storage_); }
    [[nodiscard]] T& value() { return std::get<0>(storage_); }
    [[nodiscard]] ForumError error() const { return std::get<1>(storage_); }
    [[nodiscard]] const T& operator*() const { return value(); }
    [[nodiscard]] T& operator*() { return value(); }
    [[nodiscard]] const T* operator->() const { return &value(); }
    [[nodiscard]] T* operator->() { return &value(); }

  private:
    std::variant<T, ForumError> storage_;
};

class ForumService {
  public:
    explicit ForumService(
        repositories::ForumRepository forum_repository = repositories::ForumRepository{},
        repositories::UserRepository user_repository = repositories::UserRepository{},
        repositories::UploadRepository upload_repository = repositories::UploadRepository{}
    );

    [[nodiscard]] std::vector<models::Forum> list_forums() const;
    [[nodiscard]] ForumResult<Page<models::Thread>> list_threads(
        std::string_view forum_slug,
        PaginationRequest pagination
    ) const;
    [[nodiscard]] ForumResult<ThreadDetail> get_thread(
        std::int64_t thread_id,
        PaginationRequest pagination
    ) const;
    [[nodiscard]] ForumResult<models::Thread> create_thread(
        std::int64_t author_id,
        const CreateThreadRequest& request,
        std::int64_t now
    ) const;
    [[nodiscard]] ForumResult<models::Thread> update_thread(
        std::int64_t user_id,
        std::int64_t thread_id,
        const UpdateThreadRequest& request,
        std::int64_t now
    ) const;
    [[nodiscard]] ForumResult<DeleteResult> delete_thread(
        std::int64_t user_id,
        std::int64_t thread_id,
        std::int64_t now
    ) const;
    [[nodiscard]] ForumResult<models::PostWithReplies> create_post(
        std::int64_t author_id,
        const CreatePostRequest& request,
        std::int64_t now
    ) const;
    [[nodiscard]] ForumResult<models::PostWithReplies> update_post(
        std::int64_t user_id,
        std::int64_t post_id,
        const UpdatePostRequest& request,
        std::int64_t now
    ) const;
    [[nodiscard]] ForumResult<DeleteResult> delete_post(
        std::int64_t user_id,
        std::int64_t post_id,
        std::int64_t now
    ) const;
    [[nodiscard]] ForumResult<models::SubPost> create_sub_post(
        std::int64_t author_id,
        const CreateSubPostRequest& request,
        std::int64_t now
    ) const;
    [[nodiscard]] ForumResult<models::SubPost> update_sub_post(
        std::int64_t user_id,
        std::int64_t sub_post_id,
        const UpdateSubPostRequest& request,
        std::int64_t now
    ) const;
    [[nodiscard]] ForumResult<DeleteResult> delete_sub_post(
        std::int64_t user_id,
        std::int64_t sub_post_id,
        std::int64_t now
    ) const;

  private:
    [[nodiscard]] std::string render_and_bind(
        std::int64_t author_id,
        std::string_view body_md,
        std::int64_t now
    ) const;

    repositories::ForumRepository forum_repository_;
    repositories::UserRepository user_repository_;
    repositories::UploadRepository upload_repository_;
};

}
