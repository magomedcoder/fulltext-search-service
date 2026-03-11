#include "api_server.hpp"
#include "config.hpp"
#include "index_registry.hpp"
#include "utils.hpp"
#include <exception>
#include <print>
#include <string_view>

namespace {

    // Возвращает путь к конфигу при наличии --config=<путь>, иначе nullopt
    std::optional <std::string> get_config_path(int argc, char *argv[]) {
        for (int i = 1; i < argc; ++i) {
            std::string_view arg(argv[i]);
            if (arg.starts_with("--config=")) {
                std::string path(arg.substr(9));
                return path.empty() ? std::optional<std::string>("") : std::optional(path);
            }
        }

        return std::nullopt;
    }

    bool get_dev_mode(int argc, char *argv[]) {
        for (int i = 1; i < argc; ++i) {
            if (std::string_view(argv[i]) == "--dev") {
                return true;
            }
        }

        return false;
    }

} // namespace

int main(int argc, char *argv[]) {
    const bool dev_mode = get_dev_mode(argc, argv);
    try {
        using namespace fulltext_search_service;

        AppConfig config;
        constexpr const char *kDefaultConfigPath = "/etc/fulltext-search-service/config.yaml";

        if (auto path_opt = get_config_path(argc, argv)) {
            if (path_opt->empty()) {
                std::println(stderr, "Укажите путь к конфиг-файлу: --config=<файл>");
                return 1;
            }
            if (auto loaded = LoadConfig(*path_opt, dev_mode)) {
                config = std::move(*loaded);
            } else {
                Log(dev_mode, "[dev] не удалось загрузить config path={}", *path_opt);
                std::println(stderr, "Не удалось загрузить конфиг из: {}", *path_opt);
                return 1;
            }
        } else {
            if (auto loaded = LoadConfig(kDefaultConfigPath, dev_mode)) {
                config = std::move(*loaded);
            } else {
                Log(dev_mode, "[dev] не удалось загрузить config path={}", kDefaultConfigPath);
                std::println(stderr, "Не удалось загрузить конфиг из: {}", kDefaultConfigPath);
                return 1;
            }
        }

        if (dev_mode) {
            config.index.storage_path = "./data";
            config.dev_mode = true;
            Log(true, "[dev] config path={}", config.index.storage_path);
        }

        IndexRegistry registry;
        registry.SetBaseStoragePath(config.index.storage_path);
        registry.SetMaxWordLength(config.index.max_word_length);
        registry.SetDevMode(config.dev_mode);

        ApiServer api(registry, config.api, config.server, config.index, config.dev_mode);
        if (!api.listen(config.server.host, config.server.port)) {
            Log(config.dev_mode, "[dev] не удалось запустить listen");
            std::println(stderr, "Не удалось запустить http сервер.");
            return 1;
        }
        Log(config.dev_mode, "[dev] listen {}:{}", config.server.host, config.server.port);
    } catch (const std::exception &ex) {
        fulltext_search_service::Log(dev_mode, "[dev] exception {}", ex.what());
        std::println(stderr, "Ошибка: {}", ex.what());
        return 1;
    }

    return 0;
}
