#pragma once

#include "types.hpp"
#include <string>
#include <string_view>
#include <vector>
#include <unordered_map>

namespace fulltext_search_service {

    struct TransparentStringHash {
        using is_transparent = void;

        size_t operator()(std::string_view sv) const {
            return std::hash<std::string_view>{}(sv);
        }
    };

    struct TransparentStringEqual {
        using is_transparent = void;

        bool operator()(std::string_view a, std::string_view b) const {
            return a == b;
        }
    };

    class InvertedIndex {
    public:
        InvertedIndex() = default;

        void SetStoragePath(std::string path);

        [[nodiscard]] const std::string &GetStoragePath() const noexcept { return storage_path_; }

        [[nodiscard]] bool Load();

        bool Save() const;

        void UpdateDocumentBase(std::vector<std::string> input_docs);

        [[nodiscard]] const std::vector<Entry> &GetWordCount(std::string_view word) const;

        [[nodiscard]] std::string GetDocument(size_t doc_id) const;

        [[nodiscard]] size_t GetDocumentCount() const noexcept { return docs_.size(); }

    private:
        using Dict = std::unordered_map<std::string, std::vector<Entry>, TransparentStringHash, TransparentStringEqual>;

        std::string storage_path_;
        std::vector<std::string> docs_;
        Dict freq_dictionary_;
    };

} // namespace fulltext_search_service
