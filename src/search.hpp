#pragma once

#include "types.hpp"
#include "inverted_index.hpp"
#include <string>
#include <vector>

namespace fulltext_search_service {

    class Search {
    public:
        explicit Search(InvertedIndex &index) : index_(index) {}

        [[nodiscard]] std::vector<std::vector<RelativeIndex>> search(
                const std::vector<std::string> &queries,
                int max_responses
        ) const;

    private:
        InvertedIndex &index_;
    };

} // namespace fulltext_search_service
