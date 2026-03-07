#include "inverted_index.hpp"
#include "tokenizer.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <stdexcept>

namespace fulltext_search_service {

    namespace {
        constexpr const char *kDocsFilename = "docs.dat";
        constexpr const char *kDictFilename = "dict.dat";
        const std::vector<Entry> kEmptyPostings;

        template<typename T>
        bool write_raw(std::ostream &out, const T &value) {
            return out.write(reinterpret_cast<const char *>(&value), sizeof(T)).good();
        }

        template<typename T>
        bool read_raw(std::istream &in, T &value) {
            return in.read(reinterpret_cast<char *>(&value), sizeof(T)).good();
        }

    } // namespace

    void InvertedIndex::SetStoragePath(std::string path) {
        if (path.empty()) {
            throw std::invalid_argument("Путь к хранилищу индекса не может быть пустым");
        }
        storage_path_ = std::move(path);
    }

    bool InvertedIndex::Load() {
        if (storage_path_.empty()) {
            return false;
        }

        namespace fs = std::filesystem;
        fs::path dir(storage_path_);
        fs::path docs_path = dir / kDocsFilename;
        fs::path dict_path = dir / kDictFilename;
        if (!fs::exists(docs_path) || !fs::exists(dict_path)) {
            return true;
        }

        std::ifstream docs_in(docs_path, std::ios::binary);
        std::ifstream dict_in(dict_path, std::ios::binary);
        if (!docs_in || !dict_in) {
            return false;
        }

        uint64_t num_docs = 0;
        if (!read_raw(docs_in, num_docs)) {
            return false;
        }

        docs_.clear();
        docs_.reserve(static_cast<size_t>(num_docs));
        for (uint64_t i = 0; i < num_docs; ++i) {
            uint64_t len = 0;
            if (!read_raw(docs_in, len)) {
                return false;
            }

            std::string doc(static_cast<size_t>(len), '\0');
            if (len && !docs_in.read(doc.data(), static_cast<std::streamsize>(len))) {
                return false;
            }

            docs_.push_back(std::move(doc));
        }

        uint64_t num_terms = 0;
        if (!read_raw(dict_in, num_terms)) {
            return false;
        }

        freq_dictionary_.clear();
        freq_dictionary_.reserve(static_cast<size_t>(num_terms));
        for (uint64_t t = 0; t < num_terms; ++t) {
            uint64_t word_len = 0;
            if (!read_raw(dict_in, word_len)) {
                return false;
            }

            std::string word(static_cast<size_t>(word_len), '\0');
            if (word_len && !dict_in.read(word.data(), static_cast<std::streamsize>(word_len))) {
                return false;
            }

            uint64_t num_postings = 0;
            if (!read_raw(dict_in, num_postings)) {
                return false;
            }

            std::vector<Entry> list;
            list.reserve(static_cast<size_t>(num_postings));
            for (uint64_t p = 0; p < num_postings; ++p) {
                uint64_t doc_id = 0, count = 0;
                if (!read_raw(dict_in, doc_id) || !read_raw(dict_in, count)) {
                    return false;
                }

                list.push_back({static_cast<size_t>(doc_id), static_cast<size_t>(count)});
            }
            freq_dictionary_[std::move(word)] = std::move(list);
        }
        return true;
    }

    bool InvertedIndex::Save() const {
        if (storage_path_.empty()) {
            return false;
        }

        namespace fs = std::filesystem;
        fs::path dir(storage_path_);
        if (!fs::exists(dir) && !fs::create_directories(dir)) {
            return false;
        }

        fs::path docs_path = dir / kDocsFilename;
        fs::path dict_path = dir / kDictFilename;
        std::ofstream docs_out(docs_path, std::ios::binary);
        std::ofstream dict_out(dict_path, std::ios::binary);
        if (!docs_out || !dict_out) {
            return false;
        }

        const uint64_t num_docs = static_cast<uint64_t>(docs_.size());
        if (!write_raw(docs_out, num_docs)) {
            return false;
        }

        for (const auto &doc: docs_) {
            const uint64_t len = static_cast<uint64_t>(doc.size());
            if (!write_raw(docs_out, len)) {
                return false;
            }

            if (len && !docs_out.write(doc.data(), static_cast<std::streamsize>(len))) {
                return false;
            }
        }

        const uint64_t num_terms = static_cast<uint64_t>(freq_dictionary_.size());
        if (!write_raw(dict_out, num_terms)) {
            return false;
        }

        for (const auto &[word, list]: freq_dictionary_) {
            const uint64_t word_len = static_cast<uint64_t>(word.size());
            if (!write_raw(dict_out, word_len)) {
                return false;
            }

            if (word_len && !dict_out.write(word.data(), static_cast<std::streamsize>(word_len))) {
                return false;
            }

            const uint64_t num_postings = static_cast<uint64_t>(list.size());
            if (!write_raw(dict_out, num_postings)) {
                return false;
            }

            for (const auto &e: list) {
                const uint64_t doc_id = static_cast<uint64_t>(e.doc_id);
                const uint64_t count = static_cast<uint64_t>(e.count);
                if (!write_raw(dict_out, doc_id) || !write_raw(dict_out, count)) {
                    return false;
                }
            }
        }
        return true;
    }

    void InvertedIndex::UpdateDocumentBase(std::vector<std::string> input_docs) {
        if (input_docs.empty()) {
            docs_.clear();
            freq_dictionary_.clear();
            Save();
            return;
        }
        docs_ = std::move(input_docs);
        freq_dictionary_.clear();

        for (size_t doc_id = 0; doc_id < docs_.size(); ++doc_id) {
            std::unordered_map<std::string, size_t> word_count;
            tokenize(docs_[doc_id], word_count);
            for (auto &[w, count]: word_count) {
                freq_dictionary_[std::move(w)].push_back({doc_id, count});
            }
        }

        for (auto &[word, list]: freq_dictionary_) {
            std::ranges::sort(list, {}, &Entry::doc_id);
        }

        Save();
    }

    const std::vector<Entry> &InvertedIndex::GetWordCount(std::string_view word) const {
        auto it = freq_dictionary_.find(word);
        if (it == freq_dictionary_.end()) {
            return kEmptyPostings;
        }

        return it->second;
    }

    std::string InvertedIndex::GetDocument(size_t doc_id) const {
        if (doc_id >= docs_.size()) {
            return {};
        }

        return docs_[doc_id];
    }

} // namespace fulltext_search_service