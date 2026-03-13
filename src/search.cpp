#include "search.hpp"
#include "fuzzy.hpp"
#include "utils.hpp"
#include "tokenizer.hpp"
#include <algorithm>
#include <cmath>
#include <mutex>
#include <ranges>
#include <thread>
#include <unordered_map>
#include <unordered_set>

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

        // Проверяет, входит ли последовательность phrase в doc_terms как подряд идущий отрезок
        static bool containsPhrase(
                const std::vector<std::string> &doc_terms,
                const std::vector<std::string> &phrase
        ) {
            if (phrase.empty() || doc_terms.size() < phrase.size()) {
                return false;
            }

            for (size_t i = 0; i + phrase.size() <= doc_terms.size(); ++i) {
                bool match = true;
                for (size_t j = 0; j < phrase.size(); ++j) {
                    if (doc_terms[i + j] != phrase[j]) {
                        match = false;
                        break;
                    }
                }

                if (match) {
                    return true;
                }
            }
            return false;
        }

        // Фразовый поиск: кандидаты по пересечению постингов, проверка точной фразы по тексту документа
        // out_total: если не nullptr, записывается общее число документов, совпавших с фразой
        void process_phrase_query(
                const InvertedIndex &index,
                const std::string &query,
                int max_responses,
                std::size_t max_word_length,
                bool dev_mode,
                std::vector<RelativeIndex> &out,
                size_t *out_total = nullptr
        ) {
            std::vector<std::string> query_terms;
            query_terms.reserve(32);
            tokenizeToSequence(query, query_terms, max_word_length, index.GetStemmer());

            if (query_terms.empty()) {
                out.clear();
                return;
            }

            // Постинги по каждому термину фразы
            std::vector<std::vector<size_t>> doc_ids_per_term;
            doc_ids_per_term.reserve(query_terms.size());
            size_t min_size = SIZE_MAX;
            size_t min_index = 0;
            for (size_t t = 0; t < query_terms.size(); ++t) {
                const std::vector<Entry> &postings = index.GetWordCount(query_terms[t]);
                std::vector<size_t> ids;
                ids.reserve(postings.size());
                for (const auto &e : postings) {
                    ids.push_back(e.doc_id);
                }

                doc_ids_per_term.push_back(std::move(ids));
                if (doc_ids_per_term.back().size() < min_size) {
                    min_size = doc_ids_per_term.back().size();
                    min_index = t;
                }
            }

            // Кандидаты: документы, содержащие все термины (берём из наименьшего списка)
            const std::vector<size_t> &candidates = doc_ids_per_term[min_index];
            std::vector<std::string> doc_terms;
            doc_terms.reserve(256);
            out.clear();
            out.reserve(std::min(candidates.size(), static_cast<size_t>(max_responses * 2)));

            for (size_t doc_id : candidates) {
                bool has_all_terms = true;
                for (size_t t = 0; t < doc_ids_per_term.size(); ++t) {
                    if (t == min_index) continue;
                    const std::vector<size_t> &ids = doc_ids_per_term[t];
                    if (std::find(ids.begin(), ids.end(), doc_id) == ids.end()) {
                        has_all_terms = false;
                        break;
                    }
                }
                if (!has_all_terms) {
                    continue;
                }

                std::string text = index.GetSearchableText(doc_id);
                doc_terms.clear();
                tokenizeToSequence(text, doc_terms, max_word_length, index.GetStemmer());
                if (containsPhrase(doc_terms, query_terms)) {
                    if (out_total) (*out_total)++;
                    if (out.size() < static_cast<size_t>(max_responses)) {
                        out.push_back({doc_id, 1.0f});
                    }
                }
            }

            Log(dev_mode, "[dev] search (phrase): terms={} candidates={} matched={}", query_terms.size(), candidates.size(), out.size());
        }

        // Коэффициент снижения ранга для нечёткого совпадения: 1 - kFuzzyPenaltyPerEdit * distance
        constexpr float kFuzzyPenaltyPerEdit = 0.25f;
        // Коэффициент для совпадения по подстроке (термин в индексе содержит запрос)
        constexpr float kPartialMatchFactor = 0.85f;

        // Добавляет в doc_score постинги по термину term с множителем factor
        // при out_matched_terms добавляет term
        static void add_postings_to_score(
                const InvertedIndex &index,
                std::string_view term,
                float factor,
                size_t N,
                double avgdl,
                const std::vector<size_t> &doc_lengths,
                std::unordered_map<size_t, float> &doc_score,
                std::unordered_set<std::string> *out_matched_terms
        ) {
            const std::vector<Entry> &postings = index.GetWordCount(term);
            if (postings.empty()) {
                return;
            }

            const size_t n_t = postings.size();
            const float idf = bm25_idf(N, n_t);
            if (idf <= 0.f) {
                return;
            }

            if (out_matched_terms) {
                out_matched_terms->insert(std::string(term));
            }

            for (const auto &entry : postings) {
                const size_t doc_len = (entry.doc_id < doc_lengths.size()) ? doc_lengths[entry.doc_id] : 0u;
                const float tf = static_cast<float>(entry.count);
                const float denom = tf + kBM25_k1 * static_cast<float>(1.0 - kBM25_b + kBM25_b * (static_cast<double>(doc_len) / avgdl));
                const float term_score = idf * (tf * (kBM25_k1 + 1.f)) / denom * factor;
                doc_score[entry.doc_id] += term_score;
            }
        }

        // tokenize(query) -> BM25 по doc_id -> нормализация ранга [0, 1] -> сортировка -> топ max_responses
        // out_total: если не nullptr, записывается общее число документов, подходящих под запрос
        void process_one_query(
                const InvertedIndex &index,
                const std::string &query,
                int max_responses,
                std::size_t max_word_length,
                bool dev_mode,
                bool phrase_mode,
                bool partial_mode,
                bool fuzzy_mode,
                int fuzzy_max_edits,
                std::unordered_set<std::string> *out_matched_terms,
                std::vector <RelativeIndex> &out,
                size_t *out_total = nullptr
        ) {
            if (phrase_mode) {
                process_phrase_query(index, query, max_responses, max_word_length, dev_mode, out, out_total);
                return;
            }

            std::unordered_map <std::string, size_t> word_count;
            tokenize(query, word_count, max_word_length, index.GetStemmer());

            const size_t N = index.GetDocumentCount();
            const double avgdl = index.GetAverageDocumentLength();
            if (N == 0 || avgdl <= 0.) {
                Log(dev_mode, "[dev] search: пустой индекс (N={} avgdl={})", N, avgdl);
                out.clear();
                return;
            }

            const std::vector<size_t> &doc_lengths = index.GetDocumentLengths();

            // doc_id -> накопленный BM25 score (float)
            std::unordered_map<size_t, float> doc_score;
            doc_score.reserve(256);

            auto iterate_terms = [&index](const std::function<void(std::string_view)> &fn) {
                index.ForEachVocabularyTerm(fn);
            };

            for (const auto &[word, _]: word_count) {
                const std::vector<Entry> &postings = index.GetWordCount(word);
                if (!postings.empty()) {
                    add_postings_to_score(index, word, 1.f, N, avgdl, doc_lengths, doc_score, out_matched_terms);
                    continue;
                }

                // Точного совпадения нет - при partial ищем термины, содержащие запрос как подстроку (из любой части слова)
                bool partial_matched = false;
                if (partial_mode && !word.empty()) {
                    iterate_terms([&](std::string_view term) {
                        if (term.size() >= word.size() && term.find(word) != std::string_view::npos) {
                            add_postings_to_score(index, term, kPartialMatchFactor, N, avgdl, doc_lengths, doc_score, out_matched_terms);
                            partial_matched = true;
                        }
                    });
                }
                if (partial_matched) {
                    continue;
                }

                // При включённом fuzzy ищем близкие по Левенштейну термины
                if (!fuzzy_mode || fuzzy_max_edits <= 0) {
                    continue;
                }

                std::vector<FuzzyMatch> similar;
                find_similar_terms(word, fuzzy_max_edits, iterate_terms, similar);
                for (const FuzzyMatch &fm : similar) {
                    const float fuzzy_factor = 1.f - kFuzzyPenaltyPerEdit * static_cast<float>(fm.distance);
                    add_postings_to_score(index, fm.term, fuzzy_factor, N, avgdl, doc_lengths, doc_score, out_matched_terms);
                }
            }

            // Если ничего не нашли и включён partial - ищем по всей строке запроса как подстроке
            if (doc_score.empty() && partial_mode && !query.empty()) {
                std::string q_lower = query;
                // Обрезка пробелов и BOM по краям
                size_t start = 0;
                while (start < q_lower.size() && (q_lower[start] == ' ' || static_cast<unsigned char>(q_lower[start]) <= 32)) {
                    ++start;
                }

                // UTF-8 BOM
                if (q_lower.size() >= 3 && static_cast<unsigned char>(q_lower[0]) == 0xEF &&
                    static_cast<unsigned char>(q_lower[1]) == 0xBB && static_cast<unsigned char>(q_lower[2]) == 0xBF) {
                    if (start < 3) start = 3;
                }

                size_t end = q_lower.size();
                while (end > start && (q_lower[end - 1] == ' ' || static_cast<unsigned char>(q_lower[end - 1]) <= 32)) {
                    --end;
                }

                if (end > start) {
                    q_lower = q_lower.substr(start, end - start);
                    ToLowerUtf8(q_lower);
                }

                if (!q_lower.empty()) {
                    // Сначала пробуем по терминам индекса (подстрока в слове)
                    iterate_terms([&](std::string_view term) {
                        if (term.size() >= q_lower.size() && term.find(q_lower) != std::string_view::npos) {
                            add_postings_to_score(index, term, kPartialMatchFactor, N, avgdl, doc_lengths, doc_score, out_matched_terms);
                        }
                    });

                    // Если по терминам не нашли (разная кодировка стеммера и тому подобное) - ищем подстроку в сыром тексте документов
                    if (doc_score.empty()) {
                        const size_t num_docs = index.GetDocumentCount();
                        for (size_t doc_id = 0; doc_id < num_docs; ++doc_id) {
                            std::string doc_text = index.GetSearchableText(doc_id);
                            ToLowerUtf8(doc_text);
                            if (doc_text.size() >= q_lower.size() && doc_text.find(q_lower) != std::string::npos) {
                                doc_score[doc_id] += kPartialMatchFactor;
                                if (out_matched_terms) {
                                    out_matched_terms->insert(q_lower);
                                }
                            }
                        }
                    }
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

            if (out_total) {
                *out_total = out.size();
            }
            if (out.size() > static_cast<size_t>(max_responses)) {
                out.resize(static_cast<size_t>(max_responses));
            }

            Log(dev_mode, "[dev] search: terms={} docs_matched={} returned={} (BM25)", word_count.size(), doc_score.size(), out.size());
        }

    } // namespace

    std::vector <std::vector<RelativeIndex>> Search::search(
            const std::vector <std::string> &queries,
            int max_responses,
            bool phrase,
            bool partial,
            bool fuzzy,
            int fuzzy_max_edits,
            std::unordered_set<std::string> *out_matched_terms,
            size_t *out_total
    ) const {
        std::vector <std::vector<RelativeIndex>> results(queries.size());
        if (queries.empty()) {
            return results;
        }

        if (out_matched_terms) {
            out_matched_terms->clear();
        }

        const unsigned num_workers = std::min(
                static_cast<unsigned>(queries.size()),
                std::max(1u, std::thread::hardware_concurrency())
        );
        Log(dev_mode_, "[dev] search queries={} workers={} phrase={} partial={} fuzzy={}", queries.size(), num_workers, phrase, partial, fuzzy);
        std::mutex result_mutex;
        std::vector <std::jthread> workers;
        workers.reserve(num_workers);

        std::vector<size_t> total_per_query(queries.size(), 0);
        const int safe_max_responses = std::max(max_responses, 1);
        const int safe_fuzzy_max_edits = std::max(0, std::min(fuzzy_max_edits, 3));
        for (unsigned t = 0; t < num_workers; ++t) {
            workers.emplace_back([this, &queries, &results, &result_mutex, &total_per_query, safe_max_responses, num_workers, phrase, partial, fuzzy, safe_fuzzy_max_edits, out_matched_terms, out_total, t] {
                const auto indices = std::views::iota(static_cast<size_t>(t), queries.size()) |
                                     std::views::stride(static_cast<size_t>(num_workers));
                for (size_t i: indices) {
                    std::vector<RelativeIndex> local_list;
                    local_list.reserve(512);
                    std::unordered_set<std::string> *per_query_matched = (out_matched_terms && i == 0) ? out_matched_terms : nullptr;
                    size_t *per_query_total = (out_total && i == 0) ? &total_per_query[i] : nullptr;
                    process_one_query(index_, queries[i], safe_max_responses, max_word_length_, dev_mode_, phrase, partial, fuzzy, safe_fuzzy_max_edits, per_query_matched, local_list, per_query_total);
                    {
                        std::lock_guard lock(result_mutex);
                        results[i] = std::move(local_list);
                    }
                }
            });
        }
        workers.clear();

        if (out_total && !queries.empty()) {
            *out_total = total_per_query[0];
        }
        return results;
    }

} // namespace fulltext_search_service
