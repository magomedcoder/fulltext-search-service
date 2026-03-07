#include "inverted_index.hpp"
#include "search.hpp"
#include <print>

int main() {
    try {
        using namespace fulltext_search_service;
        InvertedIndex index;
        index.SetStoragePath("./index-data");
        if (!index.Load()) {
            std::println(stderr, "Не удалось загрузить индекс.");
        } else {
            std::println("Индекс загружен, документов: {}", index.GetDocumentCount());
        }

        index.UpdateDocumentBase({
             "тест документ с текстом",
             "тест второй документ",
             "мда текст и еще текст"
        });

        Search search(index);

        auto results = search.search({"документ", "мда текст"}, 5);
        for (size_t i = 0; i < results.size(); ++i) {
            std::println("Запрос {}: {} результатов", i, results[i].size());

            for (const auto &r: results[i]) {
                std::println("  doc-id={} rank={:.4f} \"{}\"", r.doc_id, r.rank, index.GetDocument(r.doc_id));
            }
        }

    } catch (const std::exception &ex) {
        std::println(stderr, "Ошибка: {}", ex.what());
        return 1;
    }

    return 0;
}
