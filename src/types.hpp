#pragma once

#include <cstddef>
#include <cmath>
#include <string>

namespace fulltext_search_service {

    struct Entry {
        size_t doc_id{};
        size_t count{};

        constexpr bool operator==(const Entry &other) const = default;
    };

    struct RelativeIndex {
        size_t doc_id{};
        float rank{};

        [[nodiscard]] constexpr bool operator==(const RelativeIndex &other) const {
            return doc_id == other.doc_id && std::abs(rank - other.rank) < 1e-6f;
        }
    };

} // namespace fulltext_search_service
