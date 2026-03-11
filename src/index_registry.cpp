#include "index_registry.hpp"
#include "utils.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <utility>

namespace fulltext_search_service {

    namespace {
        constexpr const char *kSchemeFilename = "scheme.dat";
    }

    bool IndexRegistry::isValidName(std::string_view name) {
        if (name.empty() || name.size() > 256) {
            return false;
        }

        if (name == "." || name == "..") {
            return false;
        }

        return std::all_of(name.begin(), name.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-';
        });
    }

    void IndexRegistry::SetBaseStoragePath(std::string path) {
        if (path.empty()) {
            throw std::invalid_argument("Путь к хранилищу не может быть пустым");
        }

        base_storage_path_ = std::move(path);
    }

    void IndexRegistry::SetMaxWordLength(int value) {
        if (value > 0) {
            max_word_length_ = value;
        }
    }

    void IndexRegistry::SetDevMode(bool dev) {
        dev_mode_ = dev;
    }

    bool IndexRegistry::CreateCollection(const std::string &name, Collection collection) {
        if (!isValidName(name)) {
            return false;
        }

        if (collection.fields.empty()) {
            return false;
        }

        std::lock_guard lock(mutex_);
        namespace fs = std::filesystem;
        fs::path dir = fs::path(base_storage_path_) / name;
        if (!fs::exists(dir) && !fs::create_directories(dir)) {
            Log(dev_mode_, "[dev] index_registry: не удалось создать каталог {}", dir.string());
            return false;
        }

        auto index = std::make_unique<InvertedIndex>();
        index->SetStoragePath(dir.string());
        index->SetMaxWordLength(max_word_length_);
        index->SetDevMode(dev_mode_);
        index->SetCollection(std::move(collection));
        if (!index->SaveCollection()) {
            Log(dev_mode_, "[dev] index_registry: не удалось сохранить коллекцию для {}", name);
            return false;
        }

        indexes_[name] = std::move(index);
        Log(dev_mode_, "[dev] index_registry: создана коллекция {}", name);
        return true;
    }

    InvertedIndex *IndexRegistry::GetOrLoadIndex(const std::string &name) {
        if (!isValidName(name)) {
            return nullptr;
        }

        std::lock_guard lock(mutex_);
        auto it = indexes_.find(name);
        if (it != indexes_.end()) {
            return it->second.get();
        }

        namespace fs = std::filesystem;
        fs::path dir = fs::path(base_storage_path_) / name;
        fs::path scheme_path = dir / kSchemeFilename;
        if (!fs::exists(scheme_path)) {
            return nullptr;
        }

        auto index = std::make_unique<InvertedIndex>();
        index->SetStoragePath(dir.string());
        index->SetMaxWordLength(max_word_length_);
        index->SetDevMode(dev_mode_);
        if (!index->Load()) {
            Log(dev_mode_, "[dev] index_registry: не удалось загрузить индекс {}", name);
            return nullptr;
        }

        if (!index->HasCollection()) {
            return nullptr;
        }

        InvertedIndex *ptr = index.get();
        indexes_[name] = std::move(index);

        return ptr;
    }

    bool IndexRegistry::HasCollection(const std::string &name) const {
        if (!isValidName(name)) {
            return false;
        }

        std::lock_guard lock(mutex_);
        if (indexes_.find(name) != indexes_.end()) {
            return true;
        }

        namespace fs = std::filesystem;
        fs::path scheme_path = fs::path(base_storage_path_) / name / kSchemeFilename;

        return fs::exists(scheme_path);
    }

    bool IndexRegistry::DeleteCollection(const std::string &name) {
        if (!isValidName(name)) {
            return false;
        }

        std::lock_guard lock(mutex_);
        indexes_.erase(name);
        namespace fs = std::filesystem;
        fs::path dir = fs::path(base_storage_path_) / name;
        std::error_code ec;
        if (fs::exists(dir)) {
            fs::remove_all(dir, ec);
            if (ec) {
                Log(dev_mode_, "[dev] index_registry: не удалось удалить каталог {}: {}", name, ec.message());
                return false;
            }
        }

        Log(dev_mode_, "[dev] index_registry: удалена коллекция {}", name);

        return true;
    }

    std::vector <IndexRegistry::CollectionInfo> IndexRegistry::ListCollections() const {
        std::vector <CollectionInfo> result;
        namespace fs = std::filesystem;
        fs::path base(base_storage_path_);
        if (!fs::exists(base) || !fs::is_directory(base)) {
            return result;
        }

        std::error_code ec;
        for (auto it = fs::directory_iterator(base, ec); it != fs::directory_iterator(); it.increment(ec)) {
            if (ec) {
                continue;
            }

            if (!it->is_directory()) {
                continue;
            }

            std::string name = it->path().filename().string();
            if (!isValidName(name)) {
                continue;
            }

            fs::path scheme_path = it->path() / kSchemeFilename;
            if (!fs::exists(scheme_path)) {
                continue;
            }

            CollectionInfo info;
            info.name = name;
            result.push_back(std::move(info));
        }
        return result;
    }

} // namespace fulltext_search_service
