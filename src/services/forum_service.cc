#include "services/forum_service.h"

#include <drogon/orm/Exception.h>

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace blogalone::services {
namespace {

constexpr std::int64_t kMinPage = 1;
constexpr std::int64_t kMaxPage = 1'000'000;
constexpr std::int64_t kMaxPageSize = 50;
constexpr std::size_t kMinForumSlugLength = 2;
constexpr std::size_t kMaxForumSlugLength = 32;
constexpr std::size_t kMaxThreadTitleLength = 80;
constexpr std::size_t kMaxBodyLength = 20'000;
constexpr std::size_t kMaxSubPostBodyLength = 2'000;
constexpr int kPostFloorRetryCount = 3;
class DbTransaction {
  public:
    explicit DbTransaction(const drogon::orm::DbClientPtr& db)
        : commit_promise_{std::make_shared<std::promise<bool>>()}
        , commit_result_{commit_promise_->get_future()}
        , transaction_{db->newTransaction([promise = commit_promise_](bool committed) {
            promise->set_value(committed);
        })}
    {
    }

    DbTransaction(const DbTransaction&) = delete;
    DbTransaction& operator=(const DbTransaction&) = delete;

    ~DbTransaction()
    {
        if(transaction_) {
            transaction_->rollback();
        }
    }

    // Hand out a non-owning view: Drogon commits a transaction only when its
    // last shared_ptr reference is destroyed, so callers must not keep the
    // transaction alive past commit().
    [[nodiscard]] drogon::orm::DbClientPtr client() const
    {
        return {transaction_.get(), [](drogon::orm::DbClient*) {}};
    }

    void commit()
    {
        transaction_.reset();
        if(!commit_result_.get()) {
            throw std::runtime_error{"database transaction commit failed"};
        }
    }

  private:
    std::shared_ptr<std::promise<bool>> commit_promise_;
    std::future<bool> commit_result_;
    std::shared_ptr<drogon::orm::Transaction> transaction_;
};

[[nodiscard]] std::string trim(std::string_view value)
{
    const auto first = value.find_first_not_of(" \t\r\n");
    if(first == std::string_view::npos) {
        return {};
    }
    const auto last = value.find_last_not_of(" \t\r\n");
    return std::string{value.substr(first, last - first + 1)};
}

[[nodiscard]] bool valid_pagination(PaginationRequest pagination)
{
    return pagination.page >= kMinPage
        && pagination.page <= kMaxPage
        && pagination.page_size >= kMinPage
        && pagination.page_size <= kMaxPageSize;
}

[[nodiscard]] std::int64_t offset_for(PaginationRequest pagination)
{
    return (pagination.page - 1) * pagination.page_size;
}

[[nodiscard]] bool is_valid_forum_slug(std::string_view slug)
{
    if(slug.size() < kMinForumSlugLength || slug.size() > kMaxForumSlugLength) {
        return false;
    }
    return std::ranges::all_of(slug, [](char ch) {
        const auto byte = static_cast<unsigned char>(ch);
        return (std::islower(byte) != 0) || (std::isdigit(byte) != 0) || ch == '-';
    });
}

[[nodiscard]] std::optional<std::size_t> utf8_length(std::string_view value)
{
    std::size_t length = 0;
    std::size_t index = 0;
    while(index < value.size()) {
        const auto first = static_cast<unsigned char>(value.at(index));
        std::size_t continuation_count = 0;
        char32_t codepoint = 0;
        char32_t minimum = 0;

        if(first < 0x80) {
            ++index;
            ++length;
            continue;
        }
        if(first >= 0xc2 && first <= 0xdf) {
            continuation_count = 1;
            codepoint = first & 0x1f;
            minimum = 0x80;
        } else if(first >= 0xe0 && first <= 0xef) {
            continuation_count = 2;
            codepoint = first & 0x0f;
            minimum = 0x800;
        } else if(first >= 0xf0 && first <= 0xf4) {
            continuation_count = 3;
            codepoint = first & 0x07;
            minimum = 0x10000;
        } else {
            return std::nullopt;
        }

        if(index + continuation_count >= value.size()) {
            return std::nullopt;
        }

        for(std::size_t offset = 1; offset <= continuation_count; ++offset) {
            const auto byte = static_cast<unsigned char>(value.at(index + offset));
            if((byte & 0xc0) != 0x80) {
                return std::nullopt;
            }
            codepoint = (codepoint << 6) | (byte & 0x3f);
        }

        index += continuation_count + 1;
        if(codepoint < minimum || codepoint > 0x10ffff
            || (codepoint >= 0xd800 && codepoint <= 0xdfff)) {
            return std::nullopt;
        }
        ++length;
    }
    return length;
}

[[nodiscard]] bool is_valid_body(std::string_view body, std::size_t max_length)
{
    const auto length = utf8_length(body);
    return length.has_value() && *length > 0 && *length <= max_length;
}

[[nodiscard]] bool is_valid_title(std::string_view title)
{
    const auto length = utf8_length(title);
    return length.has_value() && *length > 0 && *length <= kMaxThreadTitleLength;
}

[[nodiscard]] std::string escape_html(std::string_view value)
{
    std::string output;
    output.reserve(value.size());
    for(const auto ch : value) {
        switch(ch) {
        case '&':
            output += "&amp;";
            break;
        case '<':
            output += "&lt;";
            break;
        case '>':
            output += "&gt;";
            break;
        case '"':
            output += "&quot;";
            break;
        case '\'':
            output += "&#39;";
            break;
        case '\r':
            break;
        case '\n':
            output += "<br>";
            break;
        default:
            output.push_back(ch);
            break;
        }
    }
    return output;
}

[[nodiscard]] std::string render_basic_html(std::string_view markdown)
{
    return "<p>" + escape_html(markdown) + "</p>";
}

[[nodiscard]] bool is_unique_violation(const drogon::orm::DrogonDbException& error)
{
    return dynamic_cast<const drogon::orm::UniqueViolation*>(&error.base()) != nullptr;
}

[[nodiscard]] bool is_user_banned(const models::User& user, std::int64_t now)
{
    return user.banned_until.has_value() && *user.banned_until > now;
}

[[nodiscard]] std::optional<ForumError> author_write_error(
    const repositories::UserRepository& user_repository,
    std::int64_t author_id,
    std::int64_t now
)
{
    const auto author = user_repository.find_by_id(author_id);
    if(!author.has_value()) {
        return ForumError::not_found;
    }
    if(is_user_banned(*author, now)) {
        return ForumError::forbidden;
    }
    return std::nullopt;
}

[[nodiscard]] Page<models::PostWithReplies> posts_page(
    const repositories::ForumRepository& repository,
    std::int64_t thread_id,
    PaginationRequest pagination
)
{
    auto posts = repository.list_posts(thread_id, pagination.page_size, offset_for(pagination));
    std::vector<models::PostWithReplies> items;
    items.reserve(posts.size());
    for(auto& post : posts) {
        const auto post_id = post.id;
        items.push_back(models::PostWithReplies{
            .post = std::move(post),
            .sub_posts = repository.list_sub_posts(post_id)
        });
    }

    return Page<models::PostWithReplies>{
        .items = std::move(items),
        .page = pagination.page,
        .page_size = pagination.page_size,
        .total = repository.count_posts(thread_id)
    };
}

[[nodiscard]] models::PostWithReplies post_with_replies(
    const repositories::ForumRepository& repository,
    models::Post post
)
{
    const auto post_id = post.id;
    return models::PostWithReplies{
        .post = std::move(post),
        .sub_posts = repository.list_sub_posts(post_id)
    };
}

}

std::string_view to_string(ForumError error)
{
    switch(error) {
    case ForumError::invalid_input:
        return "invalid_input";
    case ForumError::not_found:
        return "not_found";
    case ForumError::forbidden:
        return "forbidden";
    case ForumError::conflict:
        return "conflict";
    }
    return "invalid_input";
}

ForumService::ForumService(
    repositories::ForumRepository forum_repository,
    repositories::UserRepository user_repository
)
    : forum_repository_{std::move(forum_repository)}
    , user_repository_{std::move(user_repository)}
{
}

std::vector<models::Forum> ForumService::list_forums() const
{
    return forum_repository_.list_forums();
}

ForumResult<Page<models::Thread>> ForumService::list_threads(
    std::string_view forum_slug,
    PaginationRequest pagination
) const
{
    if(!valid_pagination(pagination) || !is_valid_forum_slug(forum_slug)) {
        return ForumError::invalid_input;
    }

    const auto forum = forum_repository_.find_forum_by_slug(forum_slug);
    if(!forum.has_value()) {
        return ForumError::not_found;
    }

    return Page<models::Thread>{
        .items = forum_repository_.list_threads(
            forum->id,
            pagination.page_size,
            offset_for(pagination)
        ),
        .page = pagination.page,
        .page_size = pagination.page_size,
        .total = forum_repository_.count_threads(forum->id)
    };
}

ForumResult<ThreadDetail> ForumService::get_thread(
    std::int64_t thread_id,
    PaginationRequest pagination
) const
{
    if(thread_id <= 0 || !valid_pagination(pagination)) {
        return ForumError::invalid_input;
    }

    auto thread = forum_repository_.find_thread(thread_id);
    if(!thread.has_value()) {
        return ForumError::not_found;
    }

    return ThreadDetail{
        .thread = std::move(*thread),
        .posts = posts_page(forum_repository_, thread_id, pagination)
    };
}

ForumResult<models::Thread> ForumService::create_thread(
    std::int64_t author_id,
    const CreateThreadRequest& request,
    std::int64_t now
) const
{
    const auto forum_slug = trim(request.forum_slug);
    const auto title = trim(request.title);
    const auto body_md = trim(request.body_md);
    if(author_id <= 0 || !is_valid_forum_slug(forum_slug) || !is_valid_title(title)
        || !is_valid_body(body_md, kMaxBodyLength)) {
        return ForumError::invalid_input;
    }
    if(const auto error = author_write_error(user_repository_, author_id, now)) {
        return *error;
    }

    const auto forum = forum_repository_.find_forum_by_slug(forum_slug);
    if(!forum.has_value()) {
        return ForumError::not_found;
    }

    DbTransaction transaction{forum_repository_.client()};
    const repositories::ForumRepository repository{transaction.client()};
    const auto thread_id = repository.create_thread(
        forum->id,
        author_id,
        title,
        body_md,
        render_basic_html(body_md),
        now
    );
    auto thread = repository.find_thread(thread_id);
    if(!thread.has_value()) {
        return ForumError::not_found;
    }
    transaction.commit();
    return *thread;
}

ForumResult<models::Thread> ForumService::update_thread(
    std::int64_t user_id,
    std::int64_t thread_id,
    const UpdateThreadRequest& request,
    std::int64_t now
) const
{
    const auto title = trim(request.title);
    const auto body_md = trim(request.body_md);
    if(user_id <= 0 || thread_id <= 0 || !is_valid_title(title)
        || !is_valid_body(body_md, kMaxBodyLength)) {
        return ForumError::invalid_input;
    }

    const auto thread = forum_repository_.find_thread(thread_id);
    if(!thread.has_value()) {
        return ForumError::not_found;
    }
    if(thread->author_id != user_id) {
        return ForumError::forbidden;
    }

    if(!forum_repository_.update_thread_content(thread_id, title, body_md, render_basic_html(body_md), now)) {
        return ForumError::not_found;
    }
    const auto updated = forum_repository_.find_thread(thread_id);
    if(!updated.has_value()) {
        return ForumError::not_found;
    }
    return *updated;
}

ForumResult<DeleteResult> ForumService::delete_thread(
    std::int64_t user_id,
    std::int64_t thread_id,
    std::int64_t now
) const
{
    if(user_id <= 0 || thread_id <= 0) {
        return ForumError::invalid_input;
    }

    const auto thread = forum_repository_.find_thread(thread_id);
    if(!thread.has_value()) {
        return ForumError::not_found;
    }
    if(thread->author_id != user_id) {
        return ForumError::forbidden;
    }

    if(!forum_repository_.soft_delete_thread(thread_id, now)) {
        return ForumError::not_found;
    }
    return DeleteResult{};
}

ForumResult<models::PostWithReplies> ForumService::create_post(
    std::int64_t author_id,
    const CreatePostRequest& request,
    std::int64_t now
) const
{
    const auto body_md = trim(request.body_md);
    if(author_id <= 0 || request.thread_id <= 0 || !is_valid_body(body_md, kMaxBodyLength)) {
        return ForumError::invalid_input;
    }
    if(const auto error = author_write_error(user_repository_, author_id, now)) {
        return *error;
    }

    for(int attempt = 0; attempt < kPostFloorRetryCount; ++attempt) {
        try {
            DbTransaction transaction{forum_repository_.client()};
            const repositories::ForumRepository repository{transaction.client()};
            if(!repository.find_thread(request.thread_id).has_value()) {
                return ForumError::not_found;
            }
            const auto floor_no = repository.next_floor_no(request.thread_id);
            const auto post_id = repository.create_post(
                request.thread_id,
                author_id,
                floor_no,
                body_md,
                render_basic_html(body_md),
                now
            );
            if(!post_id.has_value()) {
                return ForumError::not_found;
            }
            if(!repository.increment_thread_reply_count(request.thread_id, author_id, now)) {
                return ForumError::not_found;
            }

            const auto post = repository.find_post(*post_id);
            if(!post.has_value()) {
                return ForumError::not_found;
            }
            auto result = post_with_replies(repository, *post);
            transaction.commit();
            return result;
        } catch(const drogon::orm::DrogonDbException& error) {
            if(!is_unique_violation(error) || attempt + 1 == kPostFloorRetryCount) {
                if(is_unique_violation(error)) {
                    return ForumError::conflict;
                }
                throw;
            }
        }
    }

    return ForumError::conflict;
}

ForumResult<models::PostWithReplies> ForumService::update_post(
    std::int64_t user_id,
    std::int64_t post_id,
    const UpdatePostRequest& request,
    std::int64_t now
) const
{
    const auto body_md = trim(request.body_md);
    if(user_id <= 0 || post_id <= 0 || !is_valid_body(body_md, kMaxBodyLength)) {
        return ForumError::invalid_input;
    }

    const auto post = forum_repository_.find_post(post_id);
    if(!post.has_value()) {
        return ForumError::not_found;
    }
    if(post->author_id != user_id) {
        return ForumError::forbidden;
    }

    if(!forum_repository_.update_post_content(post_id, body_md, render_basic_html(body_md), now)) {
        return ForumError::not_found;
    }
    const auto updated = forum_repository_.find_post(post_id);
    if(!updated.has_value()) {
        return ForumError::not_found;
    }
    return post_with_replies(forum_repository_, *updated);
}

ForumResult<DeleteResult> ForumService::delete_post(
    std::int64_t user_id,
    std::int64_t post_id,
    std::int64_t now
) const
{
    if(user_id <= 0 || post_id <= 0) {
        return ForumError::invalid_input;
    }

    const auto post = forum_repository_.find_post(post_id);
    if(!post.has_value()) {
        return ForumError::not_found;
    }
    if(post->author_id != user_id) {
        return ForumError::forbidden;
    }

    DbTransaction transaction{forum_repository_.client()};
    const repositories::ForumRepository repository{transaction.client()};
    if(!repository.soft_delete_post(post_id, now)) {
        return ForumError::not_found;
    }
    repository.refresh_thread_reply_summary(post->thread_id);
    transaction.commit();
    return DeleteResult{};
}

ForumResult<models::SubPost> ForumService::create_sub_post(
    std::int64_t author_id,
    const CreateSubPostRequest& request,
    std::int64_t now
) const
{
    const auto body_md = trim(request.body_md);
    if(author_id <= 0 || request.post_id <= 0 || !is_valid_body(body_md, kMaxSubPostBodyLength)) {
        return ForumError::invalid_input;
    }
    if(request.reply_to_user_id.has_value() && *request.reply_to_user_id <= 0) {
        return ForumError::invalid_input;
    }
    if(const auto error = author_write_error(user_repository_, author_id, now)) {
        return *error;
    }
    if(request.reply_to_user_id.has_value() && !forum_repository_.user_exists(*request.reply_to_user_id)) {
        return ForumError::not_found;
    }

    DbTransaction transaction{forum_repository_.client()};
    const repositories::ForumRepository repository{transaction.client()};
    const auto post = repository.find_post(request.post_id);
    if(!post.has_value()) {
        return ForumError::not_found;
    }
    const auto sub_post_id = repository.create_sub_post(
        request.post_id,
        author_id,
        body_md,
        render_basic_html(body_md),
        request.reply_to_user_id,
        now
    );
    if(!sub_post_id.has_value()) {
        return ForumError::not_found;
    }
    if(!repository.update_thread_last_reply(post->thread_id, request.post_id, author_id, now)) {
        return ForumError::not_found;
    }

    const auto sub_post = repository.find_sub_post(*sub_post_id);
    if(!sub_post.has_value()) {
        return ForumError::not_found;
    }
    transaction.commit();
    return *sub_post;
}

ForumResult<models::SubPost> ForumService::update_sub_post(
    std::int64_t user_id,
    std::int64_t sub_post_id,
    const UpdateSubPostRequest& request,
    std::int64_t now
) const
{
    const auto body_md = trim(request.body_md);
    if(user_id <= 0 || sub_post_id <= 0 || !is_valid_body(body_md, kMaxSubPostBodyLength)) {
        return ForumError::invalid_input;
    }

    const auto sub_post = forum_repository_.find_sub_post(sub_post_id);
    if(!sub_post.has_value()) {
        return ForumError::not_found;
    }
    if(sub_post->author_id != user_id) {
        return ForumError::forbidden;
    }

    if(!forum_repository_.update_sub_post_content(sub_post_id, body_md, render_basic_html(body_md), now)) {
        return ForumError::not_found;
    }
    const auto updated = forum_repository_.find_sub_post(sub_post_id);
    if(!updated.has_value()) {
        return ForumError::not_found;
    }
    return *updated;
}

ForumResult<DeleteResult> ForumService::delete_sub_post(
    std::int64_t user_id,
    std::int64_t sub_post_id,
    std::int64_t now
) const
{
    if(user_id <= 0 || sub_post_id <= 0) {
        return ForumError::invalid_input;
    }

    const auto sub_post = forum_repository_.find_sub_post(sub_post_id);
    if(!sub_post.has_value()) {
        return ForumError::not_found;
    }
    if(sub_post->author_id != user_id) {
        return ForumError::forbidden;
    }

    DbTransaction transaction{forum_repository_.client()};
    const repositories::ForumRepository repository{transaction.client()};
    if(!repository.soft_delete_sub_post(sub_post_id, now)) {
        return ForumError::not_found;
    }
    repository.refresh_thread_reply_summary(sub_post->thread_id);
    transaction.commit();
    return DeleteResult{};
}

}
