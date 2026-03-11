#include "api_server.hpp"
#include "api_handlers.hpp"
#include "utils.hpp"
#include <print>

// Размер пула потоков cpp-httplib
// число одновременных запросов без блокировки
#ifndef CPPHTTPLIB_THREAD_POOL_COUNT
#define CPPHTTPLIB_THREAD_POOL_COUNT 64
#endif

#include <httplib.h>

namespace fulltext_search_service {

    ApiServer::ApiServer(
            IndexRegistry &registry,
            const ApiConfigSection &api_config,
            const ServerConfig &server_config,
            const IndexConfig &index_config,
            bool dev_mode
    ) : registry_(registry), api_config_(api_config), server_config_(server_config),
        index_config_(index_config),
        dev_mode_(dev_mode),
        server_(std::make_unique<httplib::Server>()),
        rate_limiter_(api_config.rate_limit_requests_per_minute) {
        server_->set_keep_alive_max_count(server_config_.keep_alive_max_count);
        if (server_config_.max_request_body_bytes > 0) {
            server_->set_payload_max_length(server_config_.max_request_body_bytes);
        }
        setupRoutes();
    }

    ApiServer::~ApiServer() = default;

    bool ApiServer::checkRateLimit(const httplib::Request &req, httplib::Response &res) {
        if (!rate_limiter_.is_enabled()) {
            return true;
        }

        std::string client_ip = req.remote_addr;
        if (client_ip.empty()) {
            std::string v = req.get_header_value("REMOTE_ADDR");
            client_ip = v.empty() ? "unknown" : v;
        }

        if (!rate_limiter_.try_consume(client_ip)) {
            Log(dev_mode_, "[dev] rate limit exceeded ip={}", client_ip);
            sendJson(res, 429, {
                    {"message", "Слишком много запросов"},
                    {"code",    "rate_limit_exceeded"}
            });
            return false;
        }

        return true;
    }

    void ApiServer::setupRoutes() {
        server_->Post("/indexes/collections", [this](const httplib::Request &req, httplib::Response &res) {
            Log(dev_mode_, "[dev] {} {}", req.method, req.path);
            if (!checkRateLimit(req, res)) {
                return;
            }
            handlePostCollection(registry_, req, res, dev_mode_);
        });

        server_->Get("/indexes/collections/:name", [this](const httplib::Request &req, httplib::Response &res) {
            Log(dev_mode_, "[dev] {} {}", req.method, req.path);
            if (!checkRateLimit(req, res)) {
                return;
            }
            handleGetCollection(registry_, req, res, dev_mode_);
        });

        server_->Get("/indexes/collections", [this](const httplib::Request &req, httplib::Response &res) {
            Log(dev_mode_, "[dev] {} {}", req.method, req.path);
            if (!checkRateLimit(req, res)) {
                return;
            }
            handleListCollections(registry_, req, res, dev_mode_);
        });

        server_->Delete("/indexes/collections/:name", [this](const httplib::Request &req, httplib::Response &res) {
            Log(dev_mode_, "[dev] {} {}", req.method, req.path);
            if (!checkRateLimit(req, res)) {
                return;
            }
            handleDeleteCollection(registry_, req, res, dev_mode_);
        });

        server_->Post("/indexes/:name/search", [this](const httplib::Request &req, httplib::Response &res) {
            Log(dev_mode_, "[dev] {} {}", req.method, req.path);
            if (!checkRateLimit(req, res)) {
                return;
            }
            handleSearch(registry_, api_config_, index_config_, req, res, dev_mode_);
        });

        server_->Get("/indexes/:name/documents", [this](const httplib::Request &req, httplib::Response &res) {
            Log(dev_mode_, "[dev] {} {}", req.method, req.path);
            if (!checkRateLimit(req, res)) {
                return;
            }
            handleGetDocuments(registry_, api_config_, req, res, dev_mode_);
        });

        server_->Post("/indexes/:name/documents", [this](const httplib::Request &req, httplib::Response &res) {
            Log(dev_mode_, "[dev] {} {}", req.method, req.path);
            if (!checkRateLimit(req, res)) {
                return;
            }

            handlePostDocuments(registry_, req, res, dev_mode_);
        });

        server_->set_error_handler([this](const httplib::Request &req, httplib::Response &res) {
            if (res.status == 404) {
                Log(dev_mode_, "[dev] 404 path={}", req.path);
                sendJson(res, 404, {
                        {"message", "Не найдено"},
                        {"code",    "not_found"}
                });
            } else if (res.status == 413) {
                Log(dev_mode_, "[dev] 413 тело запроса превышает допустимый размер");
                sendJson(res, 413, {
                        {"message", "Тело запроса превышает допустимый размер"},
                        {"code",    "payload_too_large"}
                });
            }
        });
    }

    bool ApiServer::listen(const std::string &host, int port) {
        std::println("{}:{}", host, port);
        return server_->listen(host, port);
    }

    void ApiServer::stop() {
        if (server_) {
            server_->stop();
        }
    }

} // namespace fulltext_search_service
