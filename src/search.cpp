#include "search.hpp"
#include "tokenizer.hpp"
#include <algorithm>
#include <unordered_map>

namespace fulltext_search_service {

    namespace {

        void process_one_query(
                const InvertedIndex &index,
                const std::string &query,
                int max_responses,
                std::vector<RelativeIndex> &out
        ) {
            std::unordered_map<std::string, size_t> word_count;
            tokenize(query, word_count);

            std::unordered_map<size_t, size_t> doc_relevance;
            doc_relevance.reserve(256);
            for (const auto &[word, _]: word_count) {
                for (const auto &entry: index.GetWordCount(word)) {
                    doc_relevance[entry.doc_id] += entry.count;
                }
            }

            if (doc_relevance.empty()) {
                out.clear();
                return;
            }

            const size_t max_rel = std::ranges::max_element(doc_relevance, {}, [](const auto &p) {
                return p.second;
            })->second;

            const float max_rel_f = static_cast<float>(max_rel);
            out.clear();
            out.reserve(doc_relevance.size());
            for (const auto &[doc_id, count]: doc_relevance) {
                out.push_back({doc_id, static_cast<float>(count) / max_rel_f});
            }

            std::ranges::sort(out, [](const auto &a, const auto &b) {
                if (a.rank != b.rank) {
                    return a.rank > b.rank;
                }

                return a.doc_id < b.doc_id;
            });

            if (out.size() > static_cast<size_t>(max_responses)) {
                out.resize(static_cast<size_t>(max_responses));
            }
        }

    } // namespace

    std::vector<std::vector<RelativeIndex>> Search::search(
            const std::vector<std::string> &queries,
            int max_responses
    ) const {
        std::vector<std::vector<RelativeIndex>> results(queries.size());
        std::vector<RelativeIndex> local_list;
        for (size_t i = 0; i < queries.size(); ++i) {
            process_one_query(index_, queries[i], max_responses, local_list);
            results[i] = std::move(local_list);
        }

        return results;
    }

} // namespace fulltext_search_service
