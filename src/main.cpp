#include "api_server.hpp"
#include "inverted_index.hpp"
#include <exception>
#include <print>

int main() {
    try {
        using namespace fulltext_search_service;
        InvertedIndex index;
        index.SetStoragePath("./index-data");
        if (!index.Load()) {
            std::println(stderr, "Не удалось загрузить индекс.");
        }

        ApiServer api(index, ApiConfig::kDefaultMaxResponses);
        if (!api.listen("0.0.0.0", 8000)) {
            std::println(stderr, "Не удалось запустить http сервер.");
            return 1;
        }
    } catch (const std::exception &ex) {
        std::println(stderr, "Ошибка: {}", ex.what());
        return 1;
    }

    return 0;
}
