#pragma once

#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>

namespace fulltext_search_service {

    class RateLimiter {
    public:
        explicit RateLimiter(int requests_per_minute);

         // Проверяет лимит для данного ip
         // true если запрос разрешён
         // false если лимит исчерпан
        bool try_consume(const std::string &client_ip);

        // 0 = отключено (всегда разрешать)
        bool is_enabled() const { return requests_per_minute_ > 0; }

    private:
        using Clock = std::chrono::steady_clock;
        static constexpr auto window_duration = std::chrono::minutes(1);

        struct Window {
            int count = 0;
            Clock::time_point start = Clock::now();
        };

        int requests_per_minute_;
        std::mutex mutex_;
        std::unordered_map <std::string, Window> windows_;
    };

} // namespace fulltext_search_service
