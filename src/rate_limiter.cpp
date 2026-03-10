#include "rate_limiter.hpp"

namespace fulltext_search_service {

    RateLimiter::RateLimiter(int requests_per_minute) : requests_per_minute_(requests_per_minute) {}

    bool RateLimiter::try_consume(const std::string &client_ip) {
        if (requests_per_minute_ <= 0) {
            return true;
        }

        const auto now = Clock::now();
        std::lock_guard lock(mutex_);

        auto &w = windows_[client_ip];
        if (now - w.start >= window_duration) {
            w.count = 0;
            w.start = now;
        }

        if (w.count >= requests_per_minute_) {
            return false;
        }
        ++w.count;
        return true;
    }

} // namespace fulltext_search_service
