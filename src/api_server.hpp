#pragma once

#include "api_handlers.hpp"
#include "config.hpp"
#include "inverted_index.hpp"
#include "rate_limiter.hpp"
#include "search.hpp"
#include <memory>
#include <string>

namespace httplib {
    class Server;
}

namespace fulltext_search_service {

    class ApiServer {
    public:
        ApiServer(InvertedIndex &index,
                  const ApiConfigSection &api_config,
                  const ServerConfig &server_config,
                  const IndexConfig &index_config,
                  bool dev_mode = false
        );

        ~ApiServer();

        ApiServer(const ApiServer &) = delete;

        ApiServer &operator=(const ApiServer &) = delete;

        bool listen(const std::string &host, int port);

        void stop();

    private:
        void setupRoutes();

        // true если лимит не превышен
        // res 429 и возвращает false
        bool checkRateLimit(const httplib::Request &req, httplib::Response &res);

        InvertedIndex &index_;

        ApiConfigSection api_config_;

        ServerConfig server_config_;

        IndexConfig index_config_;

        bool dev_mode_ = false;

        std::unique_ptr<Search> search_;

        std::unique_ptr<httplib::Server> server_;

        RateLimiter rate_limiter_;
    };

} // namespace fulltext_search_service
