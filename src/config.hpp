#pragma once

#include <optional>
#include <string>

namespace fulltext_search_service {

    struct ServerConfig {
        std::string host = "0.0.0.0";
        int port = 8000;

        // Максимальная число keep-alive соединений
        int keep_alive_max_count = 300;

        // Максимальный размер тела запроса в байтах
        // (0 = без ограничения)
        // по умолчанию 2 ГБ
        size_t max_request_body_bytes = 2ULL * 1024 * 1024 * 1024;
    };

    struct IndexConfig {
        // По умолчанию в коде используется /var/lib/fulltext-search-service
        std::string storage_path = "/var/lib/fulltext-search-service";

        // Максимальная длина слова
        // Более длинные токены отбрасываются при индексации и при разборе запроса
        int max_word_length = 100;
    };

    struct ApiConfigSection {
        // Лимит по умолчанию (GET /documents, поиск)
        int default_limit = 10;

        // Максимальный limit в одном запросе
        int max_limit = 1000;

        // Максимальный offset для пагинации
        int max_offset = 10000;

        // Число результатов поиска по умолчанию
        int max_responses = 5;

        // Максимальный запросов в минуту с одного ip (0 = отключено)
        int rate_limit_requests_per_minute = 120;
    };

    struct AppConfig {
        ServerConfig server;
        IndexConfig index;
        ApiConfigSection api;
        bool dev_mode = false;
    };

    [[nodiscard]] std::optional<AppConfig> LoadConfig(const std::string &config_path, bool dev_mode = false);

    [[nodiscard]] AppConfig DefaultConfig();

} // namespace fulltext_search_service
