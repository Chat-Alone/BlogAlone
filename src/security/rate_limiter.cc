#include "security/rate_limiter.h"

#include <stdexcept>
#include <utility>

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

RequestRateLimiter::Reservation::Reservation(
    RequestRateLimiter& limiter,
    std::vector<std::string> keys
)
    : limiter_{&limiter}
    , keys_{std::move(keys)}
{
}

RequestRateLimiter::Reservation::Reservation(Reservation&& other) noexcept
    : limiter_{std::exchange(other.limiter_, nullptr)}
    , keys_{std::move(other.keys_)}
{
}

RequestRateLimiter::Reservation& RequestRateLimiter::Reservation::operator=(
    Reservation&& other
) noexcept
{
    if(this == &other) {
        return *this;
    }
    cancel();
    limiter_ = std::exchange(other.limiter_, nullptr);
    keys_ = std::move(other.keys_);
    return *this;
}

RequestRateLimiter::Reservation::~Reservation()
{
    cancel();
}

void RequestRateLimiter::Reservation::commit(TimePoint now)
{
    if(limiter_ == nullptr) {
        throw std::logic_error{"rate limit reservation is not active"};
    }
    limiter_->commit_reservation(keys_, now);
    limiter_ = nullptr;
    keys_.clear();
}

void RequestRateLimiter::Reservation::cancel()
{
    if(limiter_ == nullptr) {
        return;
    }
    limiter_->cancel_reservation(keys_);
    limiter_ = nullptr;
    keys_.clear();
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
        if(entry->second.requests.empty() && entry->second.in_flight == 0) {
            entry = entries_.erase(entry);
        } else {
            ++entry;
        }
    }
    next_cleanup_ = now + std::chrono::minutes{1};
}

bool RequestRateLimiter::has_capacity(
    const std::vector<std::string>& keys,
    RateLimitPolicy policy,
    TimePoint now
)
{
    for(const auto& key : keys) {
        const auto existing = entries_.find(key);
        if(existing == entries_.end()) {
            continue;
        }
        existing->second.window = policy.window;
        prune(existing->second, now);
        const auto used = existing->second.requests.size() + existing->second.in_flight;
        if(used >= policy.max_requests) {
            return false;
        }
    }
    return true;
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

    if(!has_capacity(keys, policy, now)) {
        return false;
    }
    for(const auto& key : keys) {
        auto& entry = entries_[key];
        entry.window = policy.window;
        entry.requests.push_back(now);
    }
    return true;
}

std::optional<RequestRateLimiter::Reservation> RequestRateLimiter::reserve(
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

    if(!has_capacity(keys, policy, now)) {
        return std::nullopt;
    }

    for(const auto& key : keys) {
        auto& entry = entries_[key];
        entry.window = policy.window;
        ++entry.in_flight;
    }
    return Reservation{*this, std::move(keys)};
}

void RequestRateLimiter::commit_reservation(
    const std::vector<std::string>& keys,
    TimePoint now
)
{
    const std::scoped_lock lock{mutex_};
    std::vector<Entry*> reserved_entries;
    reserved_entries.reserve(keys.size());
    for(const auto& key : keys) {
        const auto existing = entries_.find(key);
        if(existing == entries_.end() || existing->second.in_flight == 0) {
            throw std::logic_error{"rate limit reservation is missing"};
        }
        reserved_entries.push_back(&existing->second);
    }

    std::size_t committed = 0;
    try {
        for(auto* entry : reserved_entries) {
            entry->requests.push_back(now);
            ++committed;
        }
    } catch(...) {
        for(std::size_t index = 0; index < committed; ++index) {
            reserved_entries.at(index)->requests.pop_back();
        }
        throw;
    }
    for(auto* entry : reserved_entries) {
        --entry->in_flight;
    }
}

void RequestRateLimiter::cancel_reservation(const std::vector<std::string>& keys)
{
    const std::scoped_lock lock{mutex_};
    for(const auto& key : keys) {
        const auto existing = entries_.find(key);
        if(existing == entries_.end() || existing->second.in_flight == 0) {
            continue;
        }
        --existing->second.in_flight;
        if(existing->second.requests.empty() && existing->second.in_flight == 0) {
            entries_.erase(existing);
        }
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
        const auto existing = entries_.find(key);
        if(existing == entries_.end()) {
            continue;
        }
        existing->second.requests.clear();
        if(existing->second.in_flight == 0) {
            entries_.erase(existing);
        }
    }
}

RequestRateLimiter& request_rate_limiter()
{
    static RequestRateLimiter limiter;
    return limiter;
}

}
