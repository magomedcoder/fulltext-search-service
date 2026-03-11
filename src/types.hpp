#pragma once

#include <cstddef>
#include <cmath>
#include <string>
#include <vector>

namespace fulltext_search_service {

    // Поле коллекции: имя и тип (int или string)
    struct CollectionField {
        std::string name;
        std::string type;
    };

    // Коллекция - список полей для валидации и индексации документов
    struct Collection {
        std::vector<CollectionField> fields;
    };

    // Один постинг в инвертированном индексе: doc_id и количество вхождений термина в документе
    struct Entry {
        size_t doc_id{};
        size_t count{};

        constexpr bool operator==(const Entry &other) const = default;
    };

    // Результат ранжирования по одному запросу: doc_id и нормализованный ранг в [0, 1]
    struct RelativeIndex {
        size_t doc_id{};
        float rank{};

        // Сравнение rank через epsilon из-за float
        [[nodiscard]] constexpr bool operator==(const RelativeIndex &other) const {
            return doc_id == other.doc_id && std::abs(rank - other.rank) < 1e-6f;
        }
    };

} // namespace fulltext_search_service
