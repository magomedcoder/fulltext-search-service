#include "api_handlers.hpp"
#include "highlight.hpp"
#include "tokenizer.hpp"
#include "utils.hpp"
#include <chrono>
#include <exception>
#include <nlohmann/json.hpp>
#include <optional>
#include <thread>
#include <unordered_set>

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
        const bool phrase_search = body.value("phrase", false);
        const bool partial_search = body.value("partial", true);
        const bool fuzzy_search = body.value("fuzzy", false);
        int fuzzy_max_edits = std::clamp(body.value("fuzzy_max_edits", 2), 0, 3);
        int limit = std::clamp(body.value("limit", api.max_responses), 1, api.max_limit);
        int offset = std::clamp(body.value("offset", 0), 0, api.max_offset);

        const bool highlight_enabled = body.value("highlight", false);
        std::string highlight_pre = "<em>";
        std::string highlight_post = "</em>";
        size_t snippet_length = 255;
        std::string snippet_suffix = "...";
        if (highlight_enabled && body.contains("highlight") && body["highlight"].is_object()) {
            const auto &hl = body["highlight"];
            if (hl.contains("pre") && hl["pre"].is_string()) {
                highlight_pre = hl["pre"].get<std::string>();
            }

            if (hl.contains("post") && hl["post"].is_string()) {
                highlight_post = hl["post"].get<std::string>();
            }

            if (hl.contains("snippet_length") && hl["snippet_length"].is_number_unsigned()) {
                const auto v = hl["snippet_length"].get<std::size_t>();
                if (v > 0) {
                    snippet_length = v;
                }
            }

            if (hl.contains("snippet_suffix") && hl["snippet_suffix"].is_string()) {
                snippet_suffix = hl["snippet_suffix"].get<std::string>();
            }
        }

        std::unordered_set<std::string> crop_fields_set;
        size_t crop_length = 15;
        std::string crop_marker = "...";
        if (body.contains("crop_fields") && body["crop_fields"].is_array()) {
            for (const auto &el : body["crop_fields"]) {
                if (el.is_string()) {
                    crop_fields_set.insert(el.get<std::string>());
                }
            }
        }
        if (body.contains("crop_length") && body["crop_length"].is_number_unsigned()) {
            const auto v = body["crop_length"].get<std::size_t>();
            if (v > 0) {
                crop_length = v;
            }
        }
        if (body.contains("crop_marker") && body["crop_marker"].is_string()) {
            crop_marker = body["crop_marker"].get<std::string>();
        }

        const int request_size = std::max(1, std::min(
                offset + limit,
                api.max_offset + api.max_limit
        ));

        const std::size_t max_word_length = std::max(static_cast<std::size_t>(1), static_cast<std::size_t>(index_config.max_word_length));
        Search search(*index, max_word_length, dev_mode);
        auto start = std::chrono::steady_clock::now();
        std::unordered_set<std::string> matched_terms;
        std::vector<std::vector<RelativeIndex>> results;
        size_t search_total = 0;
        try {
            results = search.search(
                    std::vector{query},
                    request_size,
                    phrase_search,
                    partial_search,
                    fuzzy_search,
                    fuzzy_max_edits,
                    (highlight_enabled || !crop_fields_set.empty() || partial_search || fuzzy_search) ? &matched_terms : nullptr,
                    &search_total
            );
        } catch (const std::exception &e) {
            Log(dev_mode, "[dev] search exception: {}", e.what());
            sendJson(res, 500, {
                    {"message", "Ошибка при выполнении поиска"},
                    {"code",    "search_error"},
                    {"detail",  e.what()}
            });
            return;
        }
        auto processing_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start
        ).count();

        std::unordered_set<std::string> query_terms;
        const bool need_terms = highlight_enabled || !crop_fields_set.empty();
        if (need_terms && !query.empty()) {
            if ((partial_search || fuzzy_search) && !matched_terms.empty()) {
                query_terms = matched_terms;
            } else {
                std::unordered_map<std::string, size_t> word_count;
                tokenize(query, word_count, static_cast<std::size_t>(index_config.max_word_length), index->GetStemmer());
                for (const auto &[word, _] : word_count) {
                    query_terms.insert(word);
                }
            }
        }

        const auto &full_list = results.empty() ? std::vector<RelativeIndex>{} : results[0];
        const size_t from = static_cast<size_t>(offset);
        const size_t to = std::min(from + static_cast<size_t>(limit), full_list.size());
        const size_t result_count = (from < to) ? (to - from) : 0;

        std::unordered_set<std::string> attributes_to_retrieve;
        if (body.contains("attributesToRetrieve") && body["attributesToRetrieve"].is_array()) {
            for (const auto &el : body["attributesToRetrieve"]) {
                if (el.is_string()) {
                    attributes_to_retrieve.insert(el.get<std::string>());
                }
            }
        }

        auto projectContent = [&attributes_to_retrieve](const nlohmann::json &content) -> nlohmann::json {
            if (attributes_to_retrieve.empty() || !content.is_object()) {
                return content;
            }

            nlohmann::json out = nlohmann::json::object();
            for (const auto &key : attributes_to_retrieve) {
                auto it = content.find(key);
                if (it != content.end()) {
                    out[key] = *it;
                }
            }

            return out;
        };

        const bool need_formatted = (highlight_enabled || !crop_fields_set.empty()) && !query_terms.empty() && index->HasCollection();
        const auto *stemmer = index->GetStemmer();
        const Collection &collection = index->GetCollection();

        std::vector<nlohmann::json> items(result_count);
        const unsigned num_workers = result_count > 1
                ? std::min(static_cast<unsigned>(result_count), std::max(1u, std::thread::hardware_concurrency()))
                : 1u;

        if (num_workers < 2u) {
            for (size_t i = 0; i < result_count; ++i) {
                const auto &rel = full_list[from + i];
                const nlohmann::json &content = index->GetDocument(rel.doc_id);
                nlohmann::json item = {
                        {"id",            static_cast<int>(rel.doc_id)},
                        {"content",       projectContent(content)},
                        {"_rankingScore", rel.rank}
                };
                if (need_formatted) {
                    item["_formatted"] = buildFormattedContent(
                            content, collection, crop_fields_set, query_terms,
                            crop_length, crop_marker, highlight_enabled,
                            highlight_pre, highlight_post, stemmer
                    );
                    if (highlight_enabled) {
                        item["snippet"] = buildSnippet(content, collection, query_terms, snippet_length, snippet_suffix, highlight_pre, highlight_post, stemmer);
                    }
                }
                items[i] = std::move(item);
            }
        } else {
            std::vector<std::jthread> workers;
            workers.reserve(num_workers);
            for (unsigned t = 0; t < num_workers; ++t) {
                workers.emplace_back([&, t] {
                    for (size_t i = t; i < result_count; i += num_workers) {
                        const auto &rel = full_list[from + i];
                        const nlohmann::json &content = index->GetDocument(rel.doc_id);
                        nlohmann::json item = {
                                {"id",            static_cast<int>(rel.doc_id)},
                                {"content",       projectContent(content)},
                                {"_rankingScore", rel.rank}
                        };
                        if (need_formatted) {
                            item["_formatted"] = buildFormattedContent(
                                    content, collection, crop_fields_set, query_terms,
                                    crop_length, crop_marker, highlight_enabled,
                                    highlight_pre, highlight_post, stemmer
                            );
                            if (highlight_enabled) {
                                item["snippet"] = buildSnippet(content, collection, query_terms, snippet_length, snippet_suffix, highlight_pre, highlight_post, stemmer);
                            }
                        }
                        items[i] = std::move(item);
                    }
                });
            }
        }

        nlohmann::json results_json = nlohmann::json::array();
        for (auto &item : items) {
            results_json.push_back(std::move(item));
        }
        Log(dev_mode, "[dev] search index={} q=\"{}\" results={} total={}", *name_opt, query, full_list.size(), search_total);
        sendJson(res, 200, {
                {"results",          results_json},
                {"total",            search_total},
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
