#pragma once

#include "models/forum.h"

#include <drogon/orm/DbClient.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace blogalone::repositories {

class ForumRepository {
  public:
    explicit ForumRepository(std::string db_client_name = "default");
    explicit ForumRepository(drogon::orm::DbClientPtr db_client);

    [[nodiscard]] drogon::orm::DbClientPtr client() const;
    [[nodiscard]] std::vector<models::Forum> list_forums() const;
    [[nodiscard]] std::optional<models::Forum> find_forum_by_slug(std::string_view slug) const;
    [[nodiscard]] std::optional<models::Thread> find_thread(std::int64_t thread_id) const;
    [[nodiscard]] std::optional<models::Post> find_post(std::int64_t post_id) const;
    [[nodiscard]] std::optional<models::SubPost> find_sub_post(std::int64_t sub_post_id) const;
    [[nodiscard]] bool user_exists(std::int64_t user_id) const;
    [[nodiscard]] std::int64_t count_threads(std::int64_t forum_id) const;
    [[nodiscard]] std::vector<models::Thread> list_threads(
        std::int64_t forum_id,
        std::int64_t limit,
        std::int64_t offset
    ) const;
    [[nodiscard]] std::int64_t count_posts(std::int64_t thread_id) const;
    [[nodiscard]] std::vector<models::Post> list_posts(
        std::int64_t thread_id,
        std::int64_t limit,
        std::int64_t offset
    ) const;
    [[nodiscard]] std::vector<models::SubPost> list_sub_posts(std::int64_t post_id) const;
    [[nodiscard]] std::int64_t next_floor_no(std::int64_t thread_id) const;
    [[nodiscard]] std::int64_t create_thread(
        std::int64_t forum_id,
        std::int64_t author_id,
        std::string_view title,
        std::string_view body_md,
        std::string_view body_html,
        std::int64_t now
    ) const;
    [[nodiscard]] std::optional<std::int64_t> create_post(
        std::int64_t thread_id,
        std::int64_t author_id,
        std::int64_t floor_no,
        std::string_view body_md,
        std::string_view body_html,
        std::int64_t now
    ) const;
    [[nodiscard]] std::optional<std::int64_t> create_sub_post(
        std::int64_t post_id,
        std::int64_t author_id,
        std::string_view body_md,
        std::string_view body_html,
        const std::optional<std::int64_t>& reply_to_user_id,
        std::int64_t now
    ) const;
    [[nodiscard]] bool increment_thread_reply_count(
        std::int64_t thread_id,
        std::int64_t last_reply_user_id,
        std::int64_t last_reply_at
    ) const;
    [[nodiscard]] bool update_thread_last_reply(
        std::int64_t thread_id,
        std::int64_t post_id,
        std::int64_t last_reply_user_id,
        std::int64_t last_reply_at
    ) const;
    void refresh_thread_reply_summary(std::int64_t thread_id) const;
    void update_thread_content(
        std::int64_t thread_id,
        std::string_view title,
        std::string_view body_md,
        std::string_view body_html,
        std::int64_t updated_at
    ) const;
    void update_post_content(
        std::int64_t post_id,
        std::string_view body_md,
        std::string_view body_html,
        std::int64_t updated_at
    ) const;
    void update_sub_post_content(
        std::int64_t sub_post_id,
        std::string_view body_md,
        std::string_view body_html,
        std::int64_t updated_at
    ) const;
    void soft_delete_thread(std::int64_t thread_id, std::int64_t deleted_at) const;
    void soft_delete_post(std::int64_t post_id, std::int64_t deleted_at) const;
    void soft_delete_sub_post(std::int64_t sub_post_id, std::int64_t deleted_at) const;

  private:
    std::string db_client_name_;
    drogon::orm::DbClientPtr db_client_;
};

}
