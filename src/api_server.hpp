#pragma once

#include "api_handlers.hpp"
#include "inverted_index.hpp"
#include "search.hpp"
#include <memory>
#include <string>

namespace httplib {
    class Server;
}

namespace fulltext_search_service {

    class ApiServer {
    public:
        ApiServer(InvertedIndex &index, int max_responses = ApiConfig::kDefaultMaxResponses);

        ~ApiServer();

        ApiServer(const ApiServer &) = delete;

        ApiServer &operator=(const ApiServer &) = delete;

        bool listen(const std::string &host, int port);

        void stop();

    private:
        void setupRoutes();

        InvertedIndex &index_;

        std::unique_ptr<Search> search_;

        int max_responses_;

        std::unique_ptr<httplib::Server> server_;
    };

} // namespace fulltext_search_service
