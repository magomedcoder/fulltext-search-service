#include "api_handlers.hpp"
#include "response_utils.hpp"
#include <chrono>
#include <nlohmann/json.hpp>

namespace fulltext_search_service {

    void handleSearch(
            InvertedIndex &index,
            Search &search,
            int max_responses,
            const httplib::Request &req,
            httplib::Response &res
    ) {
        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
        } catch (const nlohmann::json::exception &) {
            sendJson(res, 400, {
                    {"message", "Некорректный JSON"},
                    {"code",    "invalid_request"}
            });
            return;
        }

        std::string query = body.value("q", "");
        int limit = std::clamp(body.value("limit", max_responses), 1, ApiConfig::kMaxLimit);
        int offset = std::max(body.value("offset", 0), 0);

        auto start = std::chrono::steady_clock::now();
        auto results = search.search(std::vector{query}, limit);
        auto processing_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start
        ).count();

        nlohmann::json hits = nlohmann::json::array();
        const auto &list = results.empty() ? std::vector<RelativeIndex>{} : results[0];
        for (const auto &rel: list) {
            hits.push_back(
                    {
                            {"id",            static_cast<int>(rel.doc_id)},
                            {"content",       index.GetDocument(rel.doc_id)},
                            {"_rankingScore", rel.rank}
                    }
            );
        }
        sendJson(res, 200, {
                {"hits",               hits},
                {"offset",             offset},
                {"limit",              limit},
                {"estimatedTotalHits", hits.size()},
                {"processingTimeMs",   processing_time_ms},
                {"query",              query}
        });
    }

    void handleGetDocuments(InvertedIndex &index, const httplib::Request &req, httplib::Response &res) {
        const size_t total = index.GetDocumentCount();
        const int offset = parseQueryInt(req, "offset", 0, 0, ApiConfig::kMaxOffset);
        const int limit = parseQueryInt(req, "limit", ApiConfig::kDefaultLimit, 1, ApiConfig::kMaxLimit);
        nlohmann::json results = nlohmann::json::array();
        for (size_t i = static_cast<size_t>(offset), n = 0; i < total && n < static_cast<size_t>(limit); ++i, ++n) {
            results.push_back(
                    {
                            {"id",      static_cast<int>(i)},
                            {"content", index.GetDocument(i)}
                    }
            );
        }
        sendJson(res, 200, {
                {"results", results},
                {"offset",  offset},
                {"limit",   limit},
                {"total",   static_cast<int>(total)}
        });
    }

    void handlePostDocuments(InvertedIndex &index, const httplib::Request &req, httplib::Response &res) {
        if (req.body.empty()) {
            sendJson(res, 400, {
                    {"message", "Тело запроса пусто"},
                    {"code",    "invalid_request"}
            });
            return;
        }

        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body);
        } catch (const nlohmann::json::exception &) {
            sendJson(res, 400, {
                    {"message", "Некорректный JSON"},
                    {"code",    "invalid_request"}
            });
            return;
        }

        if (!body.is_array()) {
            sendJson(res, 400, {
                    {"message", "JSON должно быть массивом документов"},
                    {"code",    "invalid_request"}
            });
            return;
        }

        std::vector<std::string> documents;
        documents.reserve(body.size());
        for (auto &item: body) {
            if (!item.is_object()) {
                sendJson(res, 400, {
                        {"message", "Каждый документ должен быть JSON-объектом с полем content"},
                        {"code",    "invalid_request"}
                });
                return;
            }

            auto it = item.find("content");
            if (it == item.end() || !it->is_string()) {
                sendJson(res, 400, {
                        {"message", "У каждого документа должно быть строковое поле content"},
                        {"code",    "invalid_request"}
                });
                return;
            }
            documents.push_back(it->get<std::string>());
        }

        const int received = static_cast<int>(documents.size());
        index.UpdateDocumentBase(std::move(documents));
        sendJson(res, 202, {
                {"received", received}
        });
    }

} // namespace fulltext_search_service
