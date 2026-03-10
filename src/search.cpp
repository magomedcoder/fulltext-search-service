#include "search.hpp"
#include "utils.hpp"
#include "tokenizer.hpp"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <ranges>
#include <thread>
#include <unordered_map>

namespace fulltext_search_service {

    namespace {

        // Параметры BM25 (классические значения)
        constexpr float kBM25_k1 = 1.2f;
        constexpr float kBM25_b = 0.75f;

        // IDF для термина - log((N - n_t + 0.5) / (n_t + 0.5) + 1)
        // вариант Robertson/Spark Jones
        inline float bm25_idf(size_t N, size_t n_t) {
            if (N == 0 || n_t == 0) {
                return 0.f;
            }

            if (n_t >= N) {
                return 0.f;
            }

            const double x = (static_cast<double>(N) - static_cast<double>(n_t) + 0.5) / (static_cast<double>(n_t) + 0.5) + 1.0;

            return static_cast<float>(std::log(x));
        }

        // tokenize(query) -> BM25 по doc_id -> нормализация ранга [0, 1] -> сортировка -> топ max_responses
        void process_one_query(
                const InvertedIndex &index,
                const std::string &query,
                int max_responses,
                std::size_t max_word_length,
                bool dev_mode,
                std::vector <RelativeIndex> &out
        ) {
            std::unordered_map <std::string, size_t> word_count;
            tokenize(query, word_count, max_word_length);

            const size_t N = index.GetDocumentCount();
            const double avgdl = index.GetAverageDocumentLength();
            if (N == 0 || avgdl <= 0.) {
                Log(dev_mode, "[dev] search: пустой индекс (N={} avgdl={})", N, avgdl);
                out.clear();
                return;
            }

            // doc_id -> накопленный BM25 score (float)
            std::unordered_map<size_t, float> doc_score;
            doc_score.reserve(256);

            for (const auto &[word, _]: word_count) {
                const std::vector<Entry> &postings = index.GetWordCount(word);
                const size_t n_t = postings.size();
                const float idf = bm25_idf(N, n_t);
                if (idf <= 0.f) {
                    continue;
                }

                for (const auto &entry: postings) {
                    const size_t doc_len = index.GetDocumentLength(entry.doc_id);
                    const float tf = static_cast<float>(entry.count);
                    // BM25: idf * (tf * (k1+1)) / (tf + k1 * (1 - b + b * doc_len/avgdl))
                    const float denom = tf + kBM25_k1 * static_cast<float>(1.0 - kBM25_b + kBM25_b * (static_cast<double>(doc_len) / avgdl));
                    const float term_score = idf * (tf * (kBM25_k1 + 1.f)) / denom;
                    doc_score[entry.doc_id] += term_score;
                }
            }

            if (doc_score.empty()) {
                Log(dev_mode, "[dev] search: по запросу нет совпадений (terms={})", word_count.size());
                out.clear();
                return;
            }

            const float max_score = std::ranges::max_element(doc_score, {}, [](const auto &p) {
                return p.second;
            })->second;
            const float max_score_norm = (max_score > 0.f) ? max_score : 1.f;

            out.clear();
            out.reserve(doc_score.size());
            for (const auto &[doc_id, score]: doc_score) {
                out.push_back({doc_id, score / max_score_norm});
            }

            // Сначала по убыванию ранга, при равенстве - по doc_id для стабильного порядка
            std::ranges::sort(out, [](const auto &a, const auto &b) {
                if (a.rank != b.rank) {
                    return a.rank > b.rank;
                }
                return a.doc_id < b.doc_id;
            });

            if (out.size() > static_cast<size_t>(max_responses)) {
                out.resize(static_cast<size_t>(max_responses));
            }

            Log(dev_mode, "[dev] search: terms={} docs_matched={} returned={} (BM25)", word_count.size(), doc_score.size(), out.size());
        }

    } // namespace

    std::vector <std::vector<RelativeIndex>> Search::search(
            const std::vector <std::string> &queries,
            int max_responses
    ) const {
        std::vector <std::vector<RelativeIndex>> results(queries.size());
        if (queries.empty()) {
            return results;
        }

        const unsigned num_workers = std::min(
                static_cast<unsigned>(queries.size()),
                std::max(1u, std::thread::hardware_concurrency())
        );
        Log(dev_mode_, "[dev] search queries={} workers={}", queries.size(), num_workers);
        std::mutex result_mutex;
        std::vector <std::jthread> workers;
        workers.reserve(num_workers);

        // Каждый поток пишет только в results[i] для своих запросов; i совпадает с индексом запроса
        for (unsigned t = 0; t < num_workers; ++t) {
            workers.emplace_back([this, &queries, &results, &result_mutex, max_responses, num_workers, t] {
                std::vector <RelativeIndex> local_list;
                local_list.reserve(512);
                const auto indices = std::views::iota(static_cast<size_t>(t), queries.size()) |
                                     std::views::stride(static_cast<size_t>(num_workers));
                for (size_t i: indices) {
                    process_one_query(index_, queries[i], max_responses, max_word_length_, dev_mode_, local_list);
                    {
                        std::lock_guard lock(result_mutex);
                        results[i] = std::move(local_list);
                    }
                }
            });
        }
        workers.clear();

        return results;
    }

} // namespace fulltext_search_service
