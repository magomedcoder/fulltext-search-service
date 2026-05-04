#include "fulltextsearch_c_api.h"
#include "config.hpp"
#include "index_registry.hpp"
#include "search.hpp"
#include "stop_words.hpp"
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

struct fulltextsearch_engine {
    fulltext_search_service::IndexRegistry registry;
    int max_word_length = 100;
    int dev_mode = 0;
};

namespace {

    void write_err(char *err_buf, size_t err_buf_len, std::string_view msg) {
        if (!err_buf || err_buf_len == 0) {
            return;
        }

        const size_t n = std::min(err_buf_len - 1, msg.size());
        std::memcpy(err_buf, msg.data(), n);
        err_buf[n] = '\0';
    }

    int default_max_limit() {
        return fulltext_search_service::DefaultConfig().api.max_limit;
    }

    int default_max_offset() {
        return fulltext_search_service::DefaultConfig().api.max_offset;
    }

    int default_max_responses() {
        return fulltext_search_service::DefaultConfig().api.max_responses;
    }

}

extern "C" const char *fulltextsearch_version_string(void) {
    return "fulltextsearch_embed 1";
}

extern "C" fulltextsearch_engine *fulltextsearch_engine_create(const fulltextsearch_engine_options *opts) {
    if (!opts || !opts->storage_path || opts->storage_path[0] == '\0') {
        return nullptr;
    }

    try {
        auto *eng = new fulltextsearch_engine{};
        eng->registry.SetBaseStoragePath(opts->storage_path);
        const int mwl = (opts->max_word_length > 0) ? opts->max_word_length : 100;
        eng->max_word_length = mwl;
        eng->dev_mode = opts->dev_mode ? 1 : 0;
        eng->registry.SetMaxWordLength(mwl);
        eng->registry.SetStemming(
            opts->stemming_enabled != 0,
            opts->stemming_language ? opts->stemming_language : "russian"
        );
        eng->registry.SetDevMode(opts->dev_mode != 0);

        fulltext_search_service::IndexConfig idx{};
        idx.storage_path = opts->storage_path;
        idx.max_word_length = mwl;
        idx.stemming_enabled = opts->stemming_enabled != 0;
        idx.stemming_language = opts->stemming_language ? opts->stemming_language : "russian";
        if (opts->stop_words_file && opts->stop_words_file[0] != '\0') {
            idx.stop_words_file = opts->stop_words_file;
        }

        const char *base = opts->config_base_path;
        if (!base || base[0] == '\0') {
            base = opts->storage_path;
        }

        eng->registry.SetStopWords(fulltext_search_service::LoadStopWordsSet(idx, base, opts->dev_mode != 0));
        return eng;
    } catch (...) {
        return nullptr;
    }
}

extern "C" void fulltextsearch_engine_destroy(fulltextsearch_engine *engine) {
    delete engine;
}

extern "C" int fulltextsearch_search_json(
    fulltextsearch_engine *engine,
    const fulltextsearch_search_params *params,
    int max_limit,
    int max_offset,
    char **out_json,
    char *err_buf,
    size_t err_buf_len
) {
    if (out_json) {
        *out_json = nullptr;
    }

    if (!engine || !params || !out_json) {
        write_err(err_buf, err_buf_len, "неверные аргументы");
        return FULLTEXTSEARCH_ERR_INVALID;
    }

    if (!params->collection_name || params->collection_name[0] == '\0') {
        write_err(err_buf, err_buf_len, "не указано имя коллекции");
        return FULLTEXTSEARCH_ERR_INVALID;
    }

    const int cap_limit = (max_limit > 0) ? max_limit : default_max_limit();
    const int cap_offset = (max_offset > 0) ? max_offset : default_max_offset();

    fulltext_search_service::InvertedIndex *index = engine->registry.GetOrLoadIndex(params->collection_name);
    if (!index) {
        write_err(err_buf, err_buf_len, "индекс не найден");
        return FULLTEXTSEARCH_ERR_INDEX_NOT_FOUND;
    }

    const std::string query = params->query ? std::string(params->query) : std::string();
    const bool phrase_search = params->phrase != 0;
    const bool partial_search = params->partial != 0;
    const bool fuzzy_search = params->fuzzy != 0;
    const int fuzzy_max_edits = std::clamp(params->fuzzy_max_edits, 0, 3);
    const int default_limit = default_max_responses();
    const int limit_req = (params->limit > 0) ? params->limit : default_limit;
    const int limit = std::clamp(limit_req, 1, cap_limit);
    const int offset = std::clamp(params->offset, 0, cap_offset);
    const int request_size = std::max(1, std::min(offset + limit, cap_offset + cap_limit));

    const std::size_t max_word_length = static_cast<std::size_t>(std::max(1, engine->max_word_length));
    fulltext_search_service::Search search(*index, max_word_length, engine->dev_mode != 0);

    auto start = std::chrono::steady_clock::now();
    std::unordered_set <std::string> matched_terms;
    std::vector <std::vector<fulltext_search_service::RelativeIndex>> results;
    size_t search_total = 0;

    try {
        results = search.search(
            std::vector < std::string > {query},
            request_size,
            phrase_search,
            partial_search,
            fuzzy_search,
            fuzzy_max_edits,
            (partial_search || fuzzy_search) ? &matched_terms : nullptr,
            &search_total
        );
    } catch (const std::exception &e) {
        write_err(err_buf, err_buf_len, e.what());
        return FULLTEXTSEARCH_ERR_SEARCH;
    }

    const auto processing_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    const auto &full_list = results.empty() ? std::vector < fulltext_search_service::RelativeIndex > {} : results[0];
    const size_t from = static_cast<size_t>(offset);
    const size_t to = std::min(from + static_cast<size_t>(limit), full_list.size());
    const size_t result_count = (from < to) ? (to - from) : 0;

    nlohmann::json results_json = nlohmann::json::array();
    results_json.get_ref<nlohmann::json::array_t &>().reserve(result_count);
    for (size_t i = 0; i < result_count; ++i) {
        const auto &rel = full_list[from + i];
        const nlohmann::json &content = index->GetDocument(rel.doc_id);
        results_json.push_back(nlohmann::json{
            {"id",            static_cast<int>(rel.doc_id)},
            {"content",       content},
            {"_rankingScore", rel.rank}
        });
    }

    nlohmann::json root = {
        {"results",          std::move(results_json)},
        {"total",            search_total},
        {"processingTimeMs", processing_time_ms},
        {"query",            query}
    };

    std::string dumped = root.dump();
    char *buf = static_cast<char *>(std::malloc(dumped.size() + 1));
    if (!buf) {
        write_err(err_buf, err_buf_len, "нет памяти");
        return FULLTEXTSEARCH_ERR_OOM;
    }

    std::memcpy(buf, dumped.data(), dumped.size() + 1);
    *out_json = buf;
    return FULLTEXTSEARCH_OK;
}

extern "C" void fulltextsearch_free_string(char *p) {
    std::free(p);
}
