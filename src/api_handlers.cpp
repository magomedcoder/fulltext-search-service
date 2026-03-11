#include "api_handlers.hpp"
#include "utils.hpp"
#include <chrono>
#include <nlohmann/json.hpp>
#include <optional>

namespace fulltext_search_service {

    namespace {
        std::optional<std::string> getIndexNameFromPath(const httplib::Request &req) {
            auto it = req.path_params.find("name");
            if (it == req.path_params.end()) {
                return std::nullopt;
            }
            std::string name = it->second;
            if (name.empty()) {
                return std::nullopt;
            }
            return name;
        }

        void sendIndexNotFound(httplib::Response &res) {
            sendJson(res, 404, {
                    {"message", "Индекс не найден"},
                    {"code",    "index_not_found"}
            });
        }

    } // namespace

    void handleSearch(
            IndexRegistry &registry,
            const ApiConfigSection &api,
            const IndexConfig &index_config,
            const httplib::Request &req,
            httplib::Response &res,
            bool dev_mode
    ) {
        auto name_opt = getIndexNameFromPath(req);
        if (!name_opt) {
            sendIndexNotFound(res);
            return;
        }

        InvertedIndex *index = registry.GetOrLoadIndex(*name_opt);
        if (!index) {
            sendIndexNotFound(res);
            return;
        }

        nlohmann::json body;
        try {
            body = nlohmann::json::parse(req.body.empty() ? "{}" : req.body);
        } catch (const nlohmann::json::exception &) {
            Log(dev_mode, "[dev] search: неверный json");
            sendJson(res, 400, {
                    {"message", "Некорректный JSON"},
                    {"code",    "invalid_request"}
            });
            return;
        }

        std::string query = body.value("q", "");
        int limit = std::clamp(body.value("limit", api.max_responses), 1, api.max_limit);
        int offset = std::clamp(body.value("offset", 0), 0, api.max_offset);

        const int request_size = std::min(
                offset + limit,
                api.max_offset + api.max_limit
        );

        Search search(*index, static_cast<std::size_t>(index_config.max_word_length), dev_mode);
        auto start = std::chrono::steady_clock::now();
        auto results = search.search(std::vector{query}, request_size);
        auto processing_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start
        ).count();

        const auto &full_list = results.empty() ? std::vector<RelativeIndex>{} : results[0];
        const size_t from = static_cast<size_t>(offset);
        const size_t to = std::min(from + static_cast<size_t>(limit), full_list.size());

        nlohmann::json results_json = nlohmann::json::array();
        for (size_t i = from; i < to; ++i) {
            const auto &rel = full_list[i];
            results_json.push_back(
                    {
                            {"id",            static_cast<int>(rel.doc_id)},
                            {"content",       index->GetDocument(rel.doc_id)},
                            {"_rankingScore", rel.rank}
                    }
            );
        }
        Log(dev_mode, "[dev] search index={} q=\"{}\" results={}", *name_opt, query, full_list.size());
        sendJson(res, 200, {
                {"results",          results_json},
                {"offset",           offset},
                {"limit",            limit},
                {"total",            full_list.size()},
                {"processingTimeMs", processing_time_ms},
                {"query",            query}
        });
    }

    void handleGetDocuments(
            IndexRegistry &registry,
            const ApiConfigSection &api,
            const httplib::Request &req,
            httplib::Response &res,
            bool dev_mode
    ) {
        auto name_opt = getIndexNameFromPath(req);
        if (!name_opt) {
            sendIndexNotFound(res);
            return;
        }
        InvertedIndex *index = registry.GetOrLoadIndex(*name_opt);
        if (!index) {
            sendIndexNotFound(res);
            return;
        }

        const size_t total = index->GetDocumentCount();
        const int offset = parseQueryInt(req, "offset", 0, 0, api.max_offset);
        const int limit = parseQueryInt(req, "limit", api.default_limit, 1, api.max_limit);
        nlohmann::json results = nlohmann::json::array();
        for (size_t i = static_cast<size_t>(offset), n = 0; i < total && n < static_cast<size_t>(limit); ++i, ++n) {
            results.push_back(
                    {
                            {"id",      static_cast<int>(i)},
                            {"content", index->GetDocument(i)}
                    }
            );
        }
        Log(dev_mode, "[dev] docs index={} offset={} limit={} total={}", *name_opt, offset, limit, total);
        sendJson(res, 200, {
                {"results", results},
                {"offset",  offset},
                {"limit",   limit},
                {"total",   static_cast<int>(total)}
        });
    }

    void handlePostDocuments(
            IndexRegistry &registry,
            const httplib::Request &req,
            httplib::Response &res,
            bool dev_mode
    ) {
        auto name_opt = getIndexNameFromPath(req);
        if (!name_opt) {
            sendIndexNotFound(res);
            return;
        }

        InvertedIndex *index = registry.GetOrLoadIndex(*name_opt);
        if (!index) {
            sendIndexNotFound(res);
            return;
        }

        if (req.body.empty()) {
            Log(dev_mode, "[dev] post: пустое тело");
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
            Log(dev_mode, "[dev] post: неверный json");
            sendJson(res, 400, {
                    {"message", "Некорректный JSON"},
                    {"code",    "invalid_request"}
            });
            return;
        }

        if (!body.is_array()) {
            Log(dev_mode, "[dev] post: не массив");
            sendJson(res, 400, {
                    {"message", "JSON должно быть массивом документов"},
                    {"code",    "invalid_request"}
            });
            return;
        }

        if (!index->HasCollection()) {
            Log(dev_mode, "[dev] post: коллекция не задана");
            sendJson(res, 400, {
                    {"message", "Создайте коллекцию"},
                    {"code",    "collection_required"}
            });
            return;
        }

        const Collection &collection = index->GetCollection();
        std::vector<InvertedIndex::DocumentInput> documents;
        documents.reserve(body.size());

        for (auto &item: body) {
            if (!item.is_object()) {
                Log(dev_mode, "[dev] post: элемент не объект");
                sendJson(res, 400, {
                        {"message", "Каждый документ должен быть json объектом с полем content"},
                        {"code",    "invalid_request"}
                });
                return;
            }

            auto it = item.find("content");
            if (it == item.end() || !it->is_object()) {
                Log(dev_mode, "[dev] post: нет content или content не объект");
                sendJson(res, 400, {
                        {"message", "У документа должно быть поле content (объект по полям коллекции)"},
                        {"code",    "invalid_request"}
                });
                return;
            }

            nlohmann::json content = *it;
            for (const auto &field: collection.fields) {
                auto f = content.find(field.name);
                if (f == content.end()) {
                    sendJson(res, 400, {
                            {"message", "В документе отсутствует поле: " + field.name},
                            {"code",    "invalid_request"}
                    });
                    return;
                }

                if (field.type == "int") {
                    if (!f->is_number_integer() && !f->is_number_unsigned()) {
                        sendJson(res, 400, {
                                {"message", "Поле " + field.name + " должно быть типа int"},
                                {"code",    "invalid_request"}
                        });
                        return;
                    }
                } else if (field.type == "string") {
                    if (!f->is_string()) {
                        sendJson(res, 400, {
                                {"message", "Поле " + field.name + " должно быть типа string"},
                                {"code",    "invalid_request"}
                        });
                        return;
                    }
                }
            }
            documents.push_back({std::move(content)});
        }

        const int received = static_cast<int>(documents.size());
        Log(dev_mode, "[dev] post index={} received={}", *name_opt, received);
        index->UpdateDocumentBase(std::move(documents));
        sendJson(res, 202, {
                {"received", received}
        });
    }

    void handleListCollections(
            IndexRegistry &registry,
            const httplib::Request &,
            httplib::Response &res,
            bool dev_mode
    ) {
        auto list = registry.ListCollections();
        nlohmann::json collections = nlohmann::json::array();
        for (const auto &c: list) {
            collections.push_back({{"name", c.name}});
        }
        Log(dev_mode, "[dev] list collections: {} коллекций", list.size());
        sendJson(res, 200, {{"collections", collections}});
    }

    void handleGetCollection(
            IndexRegistry &registry,
            const httplib::Request &req,
            httplib::Response &res,
            bool dev_mode
    ) {
        auto name_opt = getIndexNameFromPath(req);
        if (!name_opt) {
            sendIndexNotFound(res);
            return;
        }
        InvertedIndex *index = registry.GetOrLoadIndex(*name_opt);
        if (!index || !index->HasCollection()) {
            sendIndexNotFound(res);
            return;
        }

        nlohmann::json fields = nlohmann::json::array();
        for (const auto &f: index->GetCollection().fields) {
            fields.push_back(
                    {
                            {"name", f.name},
                            {"type", f.type}
                    }
            );
        }

        nlohmann::json out = {
                {"name", *name_opt},
                {"fields", fields}
        };
        sendJson(res, 200, out);
    }

    void handlePostCollection(
            IndexRegistry &registry,
            const httplib::Request &req,
            httplib::Response &res,
            bool dev_mode
    ) {
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

        std::string name = body.value("name", "");
        if (name.empty()) {
            sendJson(res, 400, {
                    {"message", "Ожидается поле name (имя коллекции)"},
                    {"code",    "invalid_request"}
            });
            return;
        }

        auto it = body.find("fields");
        if (it == body.end() || !it->is_array()) {
            sendJson(res, 400, {
                    {"message", "Ожидается объект с полем fields (массив полей)"},
                    {"code",    "invalid_request"}
            });
            return;
        }

        Collection collection;
        for (auto &el: *it) {
            if (!el.is_object()) {
                sendJson(res, 400, {
                        {"message", "Каждый элемент fields должен быть объектом с name и type"},
                        {"code",    "invalid_request"}
                });
                return;
            }

            std::string field_name = el.value("name", "");
            std::string type = el.value("type", "");
            if (field_name.empty() || (type != "int" && type != "string")) {
                sendJson(res, 400, {
                        {"message", "Поле должно иметь name (строка) и type (int или string)"},
                        {"code",    "invalid_request"}
                });
                return;
            }
            collection.fields.push_back({std::move(field_name), std::move(type)});
        }

        if (collection.fields.empty()) {
            sendJson(res, 400, {
                    {"message", "Коллекция должна содержать хотя бы одно поле"},
                    {"code",    "invalid_request"}
            });
            return;
        }

        if (!registry.CreateCollection(name, std::move(collection))) {
            sendJson(res, 400, {
                    {"message", "Недопустимое имя коллекции или ошибка сохранения"},
                    {"code",    "invalid_request"}
            });
            return;
        }

        InvertedIndex *created = registry.GetOrLoadIndex(name);
        nlohmann::json fields = nlohmann::json::array();
        for (const auto &f: created->GetCollection().fields) {
            fields.push_back(
                    {
                            {"name", f.name},
                            {"type", f.type}
                    }
            );
        }
        nlohmann::json out = {
                {"name", name},
                {"fields", fields}
        };
        Log(dev_mode, "[dev] создана коллекция name={} fields={}", name, created->GetCollection().fields.size());
        sendJson(res, 201, out);
    }

    void handleDeleteCollection(
            IndexRegistry &registry,
            const httplib::Request &req,
            httplib::Response &res,
            bool dev_mode
    ) {
        auto name_opt = getIndexNameFromPath(req);
        if (!name_opt) {
            sendIndexNotFound(res);
            return;
        }

        if (!registry.HasCollection(*name_opt)) {
            sendIndexNotFound(res);
            return;
        }

        if (!registry.DeleteCollection(*name_opt)) {
            sendJson(res, 500, {
                    {"message", "Не удалось удалить коллекцию"},
                    {"code",    "internal_error"}
            });
            return;
        }

        Log(dev_mode, "[dev] схема удалена name={}", *name_opt);
        sendJson(res, 200, {{"message", "Схема удалена"}});
    }

} // namespace fulltext_search_service
