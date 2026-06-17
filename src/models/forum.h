#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace blogalone::models {

struct Forum {
    std::int64_t id{};
    std::string slug;
    std::string name;
    std::string description;
    std::int64_t sort_order{};
    std::int64_t created_at{};
    std::int64_t updated_at{};
};

struct Thread {
    std::int64_t id{};
    std::int64_t forum_id{};
    std::string forum_slug;
    std::string forum_name;
    std::int64_t author_id{};
    std::string author_username;
    std::string title;
    std::string body_md;
    std::string body_html;
    bool is_pinned{};
    bool is_featured{};
    std::int64_t reply_count{};
    std::optional<std::int64_t> last_reply_at;
    std::optional<std::int64_t> last_reply_user_id;
    std::int64_t created_at{};
    std::int64_t updated_at{};
};

struct Post {
    std::int64_t id{};
    std::int64_t thread_id{};
    std::int64_t author_id{};
    std::string author_username;
    std::int64_t floor_no{};
    std::string body_md;
    std::string body_html;
    std::int64_t created_at{};
    std::int64_t updated_at{};
};

struct SubPost {
    std::int64_t id{};
    std::int64_t post_id{};
    std::int64_t thread_id{};
    std::int64_t author_id{};
    std::string author_username;
    std::string body_md;
    std::string body_html;
    std::optional<std::int64_t> reply_to_user_id;
    std::optional<std::string> reply_to_username;
    std::int64_t created_at{};
    std::int64_t updated_at{};
};

struct PostWithReplies {
    Post post;
    std::vector<SubPost> sub_posts;
};

}
