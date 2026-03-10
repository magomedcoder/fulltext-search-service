#include "config.hpp"
#include "utils.hpp"
#include <fstream>
#include <yaml-cpp/yaml.h>

namespace fulltext_search_service {

    namespace {

        // https://habr.com/ru/articles/987074/
        AppConfig from_yaml(const YAML::Node &root) {
            AppConfig cfg;
            if (root["host"]) {
                std::string h = root["host"].as<std::string>();
                if (!h.empty()) {
                    cfg.server.host = std::move(h);
                }
            }
            if (root["port"]) {
                int p = root["port"].as<int>();
                if (p > 0 && p < 65536) {
                    cfg.server.port = p;
                }
            }
            if (root["keep_alive_max_count"]) {
                int v = root["keep_alive_max_count"].as<int>();
                if (v > 0) {
                    cfg.server.keep_alive_max_count = v;
                }
            }
            if (root["max_request_body_bytes"]) {
                int v = root["max_request_body_bytes"].as<int>();
                if (v > 0) {
                    cfg.server.max_request_body_bytes = static_cast<size_t>(v);
                }
            }
            if (root["storage_path"]) {
                std::string path = root["storage_path"].as<std::string>();
                if (!path.empty()) {
                    cfg.index.storage_path = std::move(path);
                }
            }
            if (root["index_max_word_length"]) {
                int v = root["index_max_word_length"].as<int>();
                if (v > 0) {
                    cfg.index.max_word_length = v;
                }
            }
            if (root["api_default_limit"]) {
                int v = root["api_default_limit"].as<int>();
                if (v > 0) {
                    cfg.api.default_limit = v;
                }
            }
            if (root["api_max_limit"]) {
                int v = root["api_max_limit"].as<int>();
                if (v > 0) {
                    cfg.api.max_limit = v;
                }
            }
            if (root["api_max_offset"]) {
                int v = root["api_max_offset"].as<int>();
                if (v >= 0) {
                    cfg.api.max_offset = v;
                }
            }
            if (root["api_max_responses"]) {
                int m = root["api_max_responses"].as<int>();
                if (m > 0) {
                    cfg.api.max_responses = m;
                }
            }
            if (root["rate_limit_requests_per_minute"]) {
                int v = root["rate_limit_requests_per_minute"].as<int>();
                if (v >= 0) {
                    cfg.api.rate_limit_requests_per_minute = v;
                }
            }

            return cfg;
        }

    } // namespace

    std::optional <AppConfig> LoadConfig(const std::string &config_path, bool dev_mode) {
        try {
            YAML::Node root = YAML::LoadFile(config_path);
            if (!root.IsMap()) {
                return std::nullopt;
            }

            auto cfg = from_yaml(root);
            Log(dev_mode, "[dev] config загружен path={} host={} port={}", config_path, cfg.server.host, cfg.server.port);
            return cfg;
        } catch (const YAML::Exception &) {
            return std::nullopt;
        } catch (const std::exception &) {
            return std::nullopt;
        }
    }

    AppConfig DefaultConfig() {
        return AppConfig{};
    }

} // namespace fulltext_search_service
