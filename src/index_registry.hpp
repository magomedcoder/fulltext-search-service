#pragma once

#include "inverted_index.hpp"
#include "types.hpp"
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace fulltext_search_service {

    // Реестр индексов по имени коллекции
    // Каждая коллекция хранится в подкаталоге base_path/name
    class IndexRegistry {
    public:
        IndexRegistry() = default;

        void SetBaseStoragePath(std::string path);
        void SetMaxWordLength(int value);
        void SetDevMode(bool dev);

        [[nodiscard]] const std::string &GetBaseStoragePath() const noexcept {
            return base_storage_path_;
        }

        // Создаёт коллекцию с заданными полями
        // Возвращает false если name невалиден или ошибка сохранения
        bool CreateCollection(const std::string &name, Collection collection);

        // Возвращает индекс по имени (загружает с диска при первом обращении)
        // nullptr если коллекции нет
        InvertedIndex *GetOrLoadIndex(const std::string &name);

        // Есть ли коллекция (в памяти или на диске по scheme.dat)
        [[nodiscard]] bool HasCollection(const std::string &name) const;

        // Удаляет коллекцию из реестра и с диска
        bool DeleteCollection(const std::string &name);

        // Элемент списка коллекций: имя
        struct CollectionInfo {
            std::string name;
        };
        // Список всех коллекций (сканирует каталог хранилища)
        [[nodiscard]] std::vector<CollectionInfo> ListCollections() const;

    private:
        static bool isValidName(std::string_view name);

        std::string base_storage_path_;
        int max_word_length_ = 100;
        bool dev_mode_ = false;
        mutable std::mutex mutex_;
        std::unordered_map<std::string, std::unique_ptr<InvertedIndex>> indexes_;
    };

} // namespace fulltext_search_service
