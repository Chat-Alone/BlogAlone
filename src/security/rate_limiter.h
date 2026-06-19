#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace blogalone::security {

enum class RateLimitScope {
    registration,
    login,
    upload,
    post
};

struct RateLimitPolicy {
    std::size_t max_requests{};
    std::chrono::seconds window{};
};

class RequestRateLimiter {
  public:
    using Clock = std::chrono::steady_clock;
    using TimePoint = Clock::time_point;

    class Reservation {
      public:
        Reservation(const Reservation&) = delete;
        Reservation& operator=(const Reservation&) = delete;
        Reservation(Reservation&& other) noexcept;
        Reservation& operator=(Reservation&& other) noexcept;
        ~Reservation();

        void commit(TimePoint now = Clock::now());
        void cancel();

      private:
        friend class RequestRateLimiter;

        Reservation(RequestRateLimiter& limiter, std::vector<std::string> keys);

        RequestRateLimiter* limiter_{};
        std::vector<std::string> keys_;
    };

    [[nodiscard]] bool consume(
        RateLimitScope scope,
        std::string_view client_ip,
        std::optional<std::int64_t> user_id,
        RateLimitPolicy policy,
        TimePoint now = Clock::now()
    );

    [[nodiscard]] std::optional<Reservation> reserve(
        RateLimitScope scope,
        std::string_view client_ip,
        std::optional<std::int64_t> user_id,
        RateLimitPolicy policy,
        TimePoint now = Clock::now()
    );

    void reset(
        RateLimitScope scope,
        std::string_view client_ip,
        std::optional<std::int64_t> user_id
    );

  private:
    struct Entry {
        std::deque<TimePoint> requests;
        std::chrono::seconds window{};
        std::size_t in_flight{};
    };

    [[nodiscard]] static std::vector<std::string> keys_for(
        RateLimitScope scope,
        std::string_view client_ip,
        std::optional<std::int64_t> user_id
    );
    static void prune(Entry& entry, TimePoint now);
    [[nodiscard]] bool has_capacity(
        const std::vector<std::string>& keys,
        RateLimitPolicy policy,
        TimePoint now
    );
    void prune_expired(TimePoint now);
    void commit_reservation(const std::vector<std::string>& keys, TimePoint now);
    void cancel_reservation(const std::vector<std::string>& keys);

    std::mutex mutex_;
    std::unordered_map<std::string, Entry> entries_;
    TimePoint next_cleanup_{};
};

[[nodiscard]] RequestRateLimiter& request_rate_limiter();

}
