#include "security/rate_limiter.h"

#include <stdexcept>

namespace blogalone::security {
namespace {

[[nodiscard]] std::string_view scope_name(RateLimitScope scope)
{
    switch(scope) {
    case RateLimitScope::registration:
        return "registration";
    case RateLimitScope::login:
        return "login";
    case RateLimitScope::upload:
        return "upload";
    case RateLimitScope::post:
        return "post";
    }
    return "unknown";
}

void validate_policy(RateLimitPolicy policy)
{
    if(policy.max_requests == 0 || policy.window <= std::chrono::seconds::zero()) {
        throw std::invalid_argument{"rate limit policy must be positive"};
    }
}

}

std::vector<std::string> RequestRateLimiter::keys_for(
    RateLimitScope scope,
    std::string_view client_ip,
    std::optional<std::int64_t> user_id
)
{
    const auto prefix = std::string{scope_name(scope)} + ':';
    std::vector<std::string> keys;
    keys.reserve(user_id.has_value() ? 2 : 1);
    keys.push_back(prefix + "ip:" + std::string{client_ip});
    if(user_id.has_value()) {
        keys.push_back(prefix + "user:" + std::to_string(*user_id));
    }
    return keys;
}

void RequestRateLimiter::prune(Entry& entry, TimePoint now)
{
    const auto oldest_allowed = now - entry.window;
    while(!entry.requests.empty() && entry.requests.front() <= oldest_allowed) {
        entry.requests.pop_front();
    }
}

void RequestRateLimiter::prune_expired(TimePoint now)
{
    if(now < next_cleanup_) {
        return;
    }

    for(auto entry = entries_.begin(); entry != entries_.end();) {
        prune(entry->second, now);
        if(entry->second.requests.empty()) {
            entry = entries_.erase(entry);
        } else {
            ++entry;
        }
    }
    next_cleanup_ = now + std::chrono::minutes{1};
}

bool RequestRateLimiter::consume(
    RateLimitScope scope,
    std::string_view client_ip,
    std::optional<std::int64_t> user_id,
    RateLimitPolicy policy,
    TimePoint now
)
{
    validate_policy(policy);
    const auto keys = keys_for(scope, client_ip, user_id);
    const std::scoped_lock lock{mutex_};
    prune_expired(now);

    for(const auto& key : keys) {
        const auto existing = entries_.find(key);
        if(existing == entries_.end()) {
            continue;
        }
        existing->second.window = policy.window;
        prune(existing->second, now);
        if(existing->second.requests.size() >= policy.max_requests) {
            return false;
        }
    }
    for(const auto& key : keys) {
        auto& entry = entries_[key];
        entry.window = policy.window;
        entry.requests.push_back(now);
    }
    return true;
}

bool RequestRateLimiter::is_allowed(
    RateLimitScope scope,
    std::string_view client_ip,
    std::optional<std::int64_t> user_id,
    RateLimitPolicy policy,
    TimePoint now
)
{
    validate_policy(policy);
    const auto keys = keys_for(scope, client_ip, user_id);
    const std::scoped_lock lock{mutex_};
    prune_expired(now);

    for(const auto& key : keys) {
        const auto existing = entries_.find(key);
        if(existing == entries_.end()) {
            continue;
        }
        existing->second.window = policy.window;
        prune(existing->second, now);
        if(existing->second.requests.size() >= policy.max_requests) {
            return false;
        }
    }
    return true;
}

void RequestRateLimiter::record(
    RateLimitScope scope,
    std::string_view client_ip,
    std::optional<std::int64_t> user_id,
    RateLimitPolicy policy,
    TimePoint now
)
{
    validate_policy(policy);
    const auto keys = keys_for(scope, client_ip, user_id);
    const std::scoped_lock lock{mutex_};
    prune_expired(now);

    for(const auto& key : keys) {
        auto& entry = entries_[key];
        entry.window = policy.window;
        prune(entry, now);
        entry.requests.push_back(now);
    }
}

void RequestRateLimiter::reset(
    RateLimitScope scope,
    std::string_view client_ip,
    std::optional<std::int64_t> user_id
)
{
    const auto keys = keys_for(scope, client_ip, user_id);
    const std::scoped_lock lock{mutex_};
    for(const auto& key : keys) {
        entries_.erase(key);
    }
}

RequestRateLimiter& request_rate_limiter()
{
    static RequestRateLimiter limiter;
    return limiter;
}

}
