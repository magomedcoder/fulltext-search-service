#pragma once

#include "types.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace fulltext_search_service {

    // Позволяет искать по string_view в map с ключами string (без копирования)
    struct TransparentStringHash {
        using is_transparent = void;

        size_t operator()(std::string_view sv) const {
            return std::hash<std::string_view>{}(sv);
        }
    };

    // Сравнение для поиска по string_view
    struct TransparentStringEqual {
        using is_transparent = void;

        bool operator()(std::string_view a, std::string_view b) const {
            return a == b;
        }
    };

    // Инвертированный индекс: хранение документов и частотный словарь (терм -> постинги)
    class InvertedIndex {
    public:
        InvertedIndex() = default;

        // Путь к каталогу для хранения индекса на диске
        void SetStoragePath(std::string path);

        void SetMaxWordLength(int value);

        void SetDevMode(bool dev);

        [[nodiscard]] const std::string &GetStoragePath() const noexcept { return storage_path_; }

        // Загружает индекс с диска
        [[nodiscard]] bool Load();

        // Сохраняет текущий индекс на диск
        // Вызывается автоматически после UpdateDocumentBase
        bool Save() const;

        // Полная замена базы документов
        // пересчёт индекса в пуле потоков и сохранение на диск
        void UpdateDocumentBase(std::vector<std::string> input_docs);

        // Список постов отсортирован по doc_id
        // Ссылка валидна до следующей модификации индекса
        [[nodiscard]] const std::vector<Entry> &GetWordCount(std::string_view word) const;

        // Текст документа по идентификатору
        // пустая строка при неверном doc_id
        [[nodiscard]] std::string GetDocument(size_t doc_id) const;

        [[nodiscard]] size_t GetDocumentCount() const noexcept {
            return docs_.size();
        }

        // Длина документа в терминах (для BM25)
        // 0 при неверном doc_id
        [[nodiscard]] size_t GetDocumentLength(size_t doc_id) const noexcept;

        // Средняя длина документа в терминах (для BM25)
        // 0 при пустой коллекции
        [[nodiscard]] double GetAverageDocumentLength() const noexcept;

    private:
        // Частотный словарь слово -> список (doc_id, количество вхождений)
        using Dict = std::unordered_map<std::string, std::vector<Entry>, TransparentStringHash, TransparentStringEqual>;

        std::string storage_path_;
        int max_word_length_ = 100;
        bool dev_mode_ = false;
        std::vector<std::string> docs_;
        std::vector<size_t> doc_lengths_;
        Dict freq_dictionary_;
    };

} // namespace fulltext_search_service
