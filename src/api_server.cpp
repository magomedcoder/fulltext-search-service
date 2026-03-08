#include "api_server.hpp"
#include "api_handlers.hpp"
#include "response_utils.hpp"
#include <print>
#include <httplib.h>

namespace fulltext_search_service {

    ApiServer::ApiServer(InvertedIndex &index, int max_responses)
            : index_(index), search_(std::make_unique<Search>(index)),
              max_responses_(max_responses > 0 ? max_responses : ApiConfig::kDefaultMaxResponses),
              server_(std::make_unique<httplib::Server>()) {
        setupRoutes();
    }

    ApiServer::~ApiServer() = default;

    void ApiServer::setupRoutes() {
        server_->Post("/indexes/search", [this](const httplib::Request &req, httplib::Response &res) {
            handleSearch(index_, *search_, max_responses_, req, res);
        });
        server_->Get("/indexes/documents", [this](const httplib::Request &req, httplib::Response &res) {
            handleGetDocuments(index_, req, res);
        });
        server_->Post("/indexes/documents", [this](const httplib::Request &req, httplib::Response &res) {
            handlePostDocuments(index_, req, res);
        });
        server_->set_error_handler([](const httplib::Request &, httplib::Response &res) {
            if (res.status == 404) {
                sendJson(res, 404, {
                        {"message", "Не найдено"},
                        {"code",    "not_found"}
                });
            }
        });
    }

    bool ApiServer::listen(const std::string &host, int port) {
        std::println("{}:{}", host, port);
        return server_->listen(host.c_str(), port);
    }

    void ApiServer::stop() {
        if (server_) {
            server_->stop();
        }
    }

} // namespace fulltext_search_service
