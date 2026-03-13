#pragma once

#include "types.hpp"
#include "inverted_index.hpp"
#include <cstddef>
#include <string>
#include <unordered_set>
#include <vector>

namespace fulltext_search_service {

    class Search {
    public:
        Search(
                InvertedIndex &index,
                std::size_t max_word_length = 100,
                bool dev_mode = false
        ) : index_(index), max_word_length_(std::max(max_word_length, std::size_t(1))), dev_mode_(dev_mode) {}

        // Для каждого запроса до max_responses документов, отсортированных по убыванию ранга (нормализован в [0, 1])
        // phrase == true - каждый запрос трактуется как точная фраза (все слова подряд в том же порядке)
        // partial == true - при отсутствии точного совпадения ищем термины, содержащие запрос как подстроку (из любой части слова)
        // fuzzy == true - для терминов, отсутствующих в индексе, подбираются близкие по Левенштейну (до fuzzy_max_edits правок)
        // out_matched_terms: если не nullptr, заполняется множеством индексных терминов, по которым найден результат (для подсветки)
        // out_total: если не nullptr, для первого запроса записывается общее число документов, подходящих под запрос (до пагинации)
        [[nodiscard]] std::vector<std::vector<RelativeIndex>> search(
                const std::vector<std::string> &queries,
                int max_responses,
                bool phrase = false,
                bool partial = true,
                bool fuzzy = false,
                int fuzzy_max_edits = 2,
                std::unordered_set<std::string> *out_matched_terms = nullptr,
                size_t *out_total = nullptr
        ) const;

    private:
        InvertedIndex &index_;
        std::size_t max_word_length_;
        bool dev_mode_ = false;
    };

} // namespace fulltext_search_service
