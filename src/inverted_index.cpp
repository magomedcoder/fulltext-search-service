#include "inverted_index.hpp"
#include "utils.hpp"
#include "tokenizer.hpp"
#include <algorithm>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ranges>
#include <stdexcept>
#include <thread>

namespace fulltext_search_service {

    namespace {
        constexpr const char *kDocsFilename = "docs.dat";
        constexpr const char *kDictFilename = "dict.dat";
        constexpr const char *kDocLengthsFilename = "doc_lengths.dat";
        constexpr const char *kSchemeFilename = "scheme.dat";
        constexpr uint8_t kFieldTypeInt = 0;
        constexpr uint8_t kFieldTypeString = 1;

        // Пустой вектор для возврата из GetWordCount при отсутствии слова (короче избегаем аллокаций)
        const std::vector<Entry> kEmptyPostings;

        // Запись значения типа T в бинарный поток (без сериализации, только сырые байты)
        template<typename T>
        bool write_raw(std::ostream &out, const T &value) {
            return out.write(reinterpret_cast<const char *>(&value), sizeof(T)).good();
        }

        // Чтение значения типа T из бинарного потока
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

    void InvertedIndex::SetMaxWordLength(int value) {
        if (value > 0) {
            max_word_length_ = value;
        }
    }

    void InvertedIndex::SetDevMode(bool dev) {
        dev_mode_ = dev;
    }

    void InvertedIndex::SetCollection(Collection collection) {
        collection_ = std::move(collection);
    }

    std::string InvertedIndex::buildSearchableText(const nlohmann::json &content) const {
        if (!content.is_object()) {
            return {};
        }

        std::string result;
        for (const auto &field: collection_.fields) {
            if (field.type != "string") {
                continue;
            }

            auto it = content.find(field.name);
            if (it == content.end() || !it->is_string()) {
                continue;
            }

            const std::string &s = it->get<std::string>();
            if (!result.empty()) {
                result += ' ';
            }

            result += s;
        }

        return result;
    }

    bool InvertedIndex::SaveCollection() const {
        if (storage_path_.empty()) {
            return true;
        }

        namespace fs = std::filesystem;
        fs::path scheme_path = fs::path(storage_path_) / kSchemeFilename;
        if (collection_.fields.empty()) {
            if (fs::exists(scheme_path)) {
                std::error_code ec;
                fs::remove(scheme_path, ec);
            }
            return true;
        }

        fs::path dir(storage_path_);
        if (!fs::exists(dir) && !fs::create_directories(dir)) {
            return false;
        }

        std::ofstream scheme_out(scheme_path, std::ios::binary);
        if (!scheme_out) {
            return false;
        }

        const uint64_t num_fields = static_cast<uint64_t>(collection_.fields.size());
        if (!write_raw(scheme_out, num_fields)) {
            return false;
        }

        for (const auto &f : collection_.fields) {
            const uint8_t type_byte = (f.type == "int") ? kFieldTypeInt : kFieldTypeString;
            if (!write_raw(scheme_out, type_byte)) {
                return false;
            }

            const uint64_t name_len = static_cast<uint64_t>(f.name.size());
            if (!write_raw(scheme_out, name_len) ||
                (name_len && !scheme_out.write(f.name.data(), static_cast<std::streamsize>(name_len)))) {
                return false;
            }
        }

        return true;
    }

    bool InvertedIndex::Load() {
        if (storage_path_.empty()) {
            Log(dev_mode_, "[dev] inverted_index::Load: путь к хранилищу не задан");
            return false;
        }

        Log(dev_mode_, "[dev] inverted_index::Load: path={}", storage_path_);

        namespace fs = std::filesystem;
        fs::path dir(storage_path_);
        fs::path docs_path = dir / kDocsFilename;
        fs::path dict_path = dir / kDictFilename;

        // Загрузка коллекции из scheme.dat
        fs::path scheme_path = dir / kSchemeFilename;
        if (fs::exists(scheme_path)) {
            std::ifstream scheme_in(scheme_path, std::ios::binary);
            if (scheme_in) {
                uint64_t num_fields = 0;
                if (read_raw(scheme_in, num_fields)) {
                    collection_.fields.clear();
                    collection_.fields.reserve(static_cast<size_t>(num_fields));
                    for (uint64_t i = 0; i < num_fields; ++i) {
                        uint8_t type_byte = 0;
                        uint64_t name_len = 0;
                        if (!read_raw(scheme_in, type_byte) || !read_raw(scheme_in, name_len)) {
                            break;
                        }

                        std::string name(static_cast<size_t>(name_len), '\0');
                        if (name_len && !scheme_in.read(name.data(), static_cast<std::streamsize>(name_len))) {
                            break;
                        }
                        collection_.fields.push_back({std::move(name), type_byte == kFieldTypeInt ? "int" : "string"});
                    }
                    Log(dev_mode_, "[dev] inverted_index::Load: загружена коллекция, полей={}", collection_.fields.size());
                }
            }
        }

        if (collection_.fields.empty() || !fs::exists(docs_path) || !fs::exists(dict_path)) {
            Log(dev_mode_, "[dev] inverted_index::Load: коллекция пуста или файлы индекса отсутствуют, пустой индекс");
            return true;
        }

        std::ifstream docs_in(docs_path, std::ios::binary);
        std::ifstream dict_in(dict_path, std::ios::binary);
        if (!docs_in || !dict_in) {
            Log(dev_mode_, "[dev] inverted_index::Load: не удалось открыть файлы");
            return false;
        }

        docs_.clear();
        if (!collection_.fields.empty()) {
            uint64_t num_docs = 0;
            if (!read_raw(docs_in, num_docs)) {
                Log(dev_mode_, "[dev] inverted_index::Load: ошибка чтения num_docs из docs.dat");
                return false;
            }

            docs_.reserve(static_cast<size_t>(num_docs));
            for (uint64_t d = 0; d < num_docs; ++d) {
                nlohmann::json doc = nlohmann::json::object();
                for (const auto &field : collection_.fields) {
                    if (field.type == "int") {
                        int64_t val = 0;
                        if (!read_raw(docs_in, val)) {
                            Log(dev_mode_, "[dev] inverted_index::Load: ошибка чтения int поля {} doc {}", field.name, d);
                            return false;
                        }
                        doc[field.name] = val;
                    } else {
                        uint64_t str_len = 0;
                        if (!read_raw(docs_in, str_len)) {
                            return false;
                        }

                        std::string s(static_cast<size_t>(str_len), '\0');
                        if (str_len && !docs_in.read(s.data(), static_cast<std::streamsize>(str_len))) {
                            return false;
                        }
                        doc[field.name] = std::move(s);
                    }
                }
                docs_.push_back(std::move(doc));
            }
            Log(dev_mode_, "[dev] inverted_index::Load: загружено {} документов из docs.dat", docs_.size());
        }

        // doc_lengths.dat (опционально если нет вычислим из словаря после загрузки dict)
        doc_lengths_.clear();
        bool doc_lengths_loaded = false;
        fs::path doc_lengths_path = dir / kDocLengthsFilename;
        if (fs::exists(doc_lengths_path)) {
            std::ifstream len_in(doc_lengths_path, std::ios::binary);
            if (len_in) {
                uint64_t num_docs_len = 0;
                if (read_raw(len_in, num_docs_len) && num_docs_len == docs_.size()) {
                    doc_lengths_.reserve(static_cast<size_t>(num_docs_len));
                    for (uint64_t i = 0; i < num_docs_len; ++i) {
                        uint64_t len = 0;
                        if (!read_raw(len_in, len)) {
                            break;
                        }
                        doc_lengths_.push_back(static_cast<size_t>(len));
                    }

                    if (doc_lengths_.size() == docs_.size()) {
                        doc_lengths_loaded = true;
                        Log(dev_mode_, "[dev] inverted_index::Load: загружены длины документов из doc_lengths.dat");
                    }
                }
            }
        }

        if (!doc_lengths_loaded) {
            doc_lengths_.assign(docs_.size(), 0);
        }

        // dict.dat - читаем формат [uint64 num_terms] [для каждого термина: uint64 word_len, word_len байт, uint64 num_postings, для каждого постинга: uint64 doc_id, uint64 count]
        Log(dev_mode_, "[dev] inverted_index::Load: dict.dat - формат uint64 num_terms, для каждого термина uint64 word_len, word_len байт, uint64 num_postings, затем (uint64 doc_id, uint64 count) * num_postings");
        uint64_t num_terms = 0;
        if (!read_raw(dict_in, num_terms)) {
            Log(dev_mode_, "[dev] inverted_index::Load: ошибка чтения num_terms из dict.dat");
            return false;
        }
        Log(dev_mode_, "[dev] inverted_index::Load: прочитали num_terms={}", num_terms);

        freq_dictionary_.clear();
        freq_dictionary_.reserve(static_cast<size_t>(num_terms));
        for (uint64_t t = 0; t < num_terms; ++t) {
            uint64_t word_len = 0;
            if (!read_raw(dict_in, word_len)) {
                Log(dev_mode_, "[dev] inverted_index::Load: ошибка чтения word_len для термина #{}", t);
                return false;
            }

            std::string word(static_cast<size_t>(word_len), '\0');
            if (word_len && !dict_in.read(word.data(), static_cast<std::streamsize>(word_len))) {
                Log(dev_mode_, "[dev] inverted_index::Load: ошибка чтения слова для термина #{}", t);
                return false;
            }

            uint64_t num_postings = 0;
            if (!read_raw(dict_in, num_postings)) {
                Log(dev_mode_, "[dev] inverted_index::Load: ошибка чтения num_postings для термина #{}", t);
                return false;
            }

            std::vector<Entry> list;
            list.reserve(static_cast<size_t>(num_postings));
            for (uint64_t p = 0; p < num_postings; ++p) {
                uint64_t doc_id = 0, count = 0;
                if (!read_raw(dict_in, doc_id) || !read_raw(dict_in, count)) {
                    Log(dev_mode_, "[dev] inverted_index::Load: ошибка чтения постинга (термин #{} posting #{})", t, p);
                    return false;
                }

                list.push_back({static_cast<size_t>(doc_id), static_cast<size_t>(count)});
            }
            freq_dictionary_[std::move(word)] = std::move(list);
        }
        Log(dev_mode_, "[dev] inverted_index::Load: этап dict.dat завершён. Итого: {} docs, {} терминов", docs_.size(), freq_dictionary_.size());

        // Если длины документов не загрузились из файла вычисляем из инвертированного индекса
        if (!doc_lengths_loaded) {
            for (const auto &[word, list]: freq_dictionary_) {
                for (const auto &e: list) {
                    if (e.doc_id < doc_lengths_.size()) {
                        doc_lengths_[e.doc_id] += e.count;
                    }
                }
            }

            Log(dev_mode_, "[dev] inverted_index::Load: длины документов вычислены из словаря");
        }

        return true;
    }

    bool InvertedIndex::Save() const {
        if (storage_path_.empty()) {
            Log(dev_mode_, "[dev] inverted_index::Save: путь к хранилищу не задан");
            return false;
        }

        Log(dev_mode_, "[dev] inverted_index::Save: path={}", storage_path_);

        namespace fs = std::filesystem;
        fs::path dir(storage_path_);
        if (!fs::exists(dir) && !fs::create_directories(dir)) {
            Log(dev_mode_, "[dev] inverted_index::Save: не удалось создать каталог {}", dir.string());
            return false;
        }

        fs::path docs_path = dir / kDocsFilename;
        fs::path dict_path = dir / kDictFilename;
        std::ofstream dict_out(dict_path, std::ios::binary);
        if (!dict_out) {
            Log(dev_mode_, "[dev] inverted_index::Save: не удалось открыть dict для записи");
            return false;
        }

        SaveCollection();

        // docs.dat - бинарный формат: uint64 num_docs, для каждого doc по полям схемы: int -> int64_t, string -> uint64_t len + bytes
        std::ofstream docs_out(docs_path, std::ios::binary);
        if (!docs_out) {
            Log(dev_mode_, "[dev] inverted_index::Save: не удалось открыть docs.dat");
            return false;
        }
        const uint64_t num_docs = static_cast<uint64_t>(docs_.size());
        if (!write_raw(docs_out, num_docs)) {
            return false;
        }
        for (const auto &doc : docs_) {
            for (const auto &field : collection_.fields) {
                auto it = doc.find(field.name);
                if (it == doc.end()) {
                    continue;
                }

                if (field.type == "int") {
                    int64_t val = it->get<int64_t>();
                    if (!write_raw(docs_out, val)) {
                        return false;
                    }
                } else {
                    const std::string &s = it->get<std::string>();
                    const uint64_t len = static_cast<uint64_t>(s.size());
                    if (!write_raw(docs_out, len) || (len && !docs_out.write(s.data(), static_cast<std::streamsize>(len)))) {
                        return false;
                    }
                }
            }
        }
        Log(dev_mode_, "[dev] inverted_index::Save: записано {} документов в docs.dat", docs_.size());

        // dict.dat - сохраняем в формате [uint64 num_terms] [для каждого термина uint64 word_len, word_len байт, uint64 num_postings, (uint64 doc_id, uint64 count) * num_postings]
        Log(dev_mode_, "[dev] inverted_index::Save: dict.dat - формат записи: uint64 num_terms, для каждого термина: uint64 word_len, word_len байт, uint64 num_postings, затем (uint64 doc_id, uint64 count) * num_postings");
        const uint64_t num_terms = static_cast<uint64_t>(freq_dictionary_.size());
        if (!write_raw(dict_out, num_terms)) {
            return false;
        }
        Log(dev_mode_, "[dev] inverted_index::Save: записали num_terms={}", num_terms);

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
        Log(dev_mode_, "[dev] inverted_index::Save: этап dict.dat завершён. Итого сохранено - {} docs, {} терминов", docs_.size(), freq_dictionary_.size());

        // doc_lengths.dat - формат: uint64 num_docs, затем для каждого doc uint64 длина_в_терминах
        // опционально - при отсутствии длины вычисляются из словаря
        fs::path doc_lengths_path = dir / kDocLengthsFilename;
        std::ofstream len_out(doc_lengths_path, std::ios::binary);
        if (len_out) {
            const uint64_t num_docs = static_cast<uint64_t>(doc_lengths_.size());
            if (write_raw(len_out, num_docs)) {
                for (size_t len: doc_lengths_) {
                    const uint64_t u = static_cast<uint64_t>(len);
                    if (!write_raw(len_out, u)) break;
                }
            }
        }

        return true;
    }

    void InvertedIndex::UpdateDocumentBase(std::vector<DocumentInput> input_docs) {
        if (input_docs.empty()) {
            Log(dev_mode_, "[dev] inverted_index::UpdateDocumentBase: очистка базы документов");
            docs_.clear();
            freq_dictionary_.clear();
            doc_lengths_.clear();
            Save();
            return;
        }
        docs_.clear();
        docs_.reserve(input_docs.size());
        std::vector<std::string> searchable_texts;
        searchable_texts.reserve(input_docs.size());
        for (auto &rec: input_docs) {
            docs_.push_back(std::move(rec.content));
            searchable_texts.push_back(buildSearchableText(docs_.back()));
        }

        const size_t num_docs = docs_.size();
        doc_lengths_.resize(num_docs, 0);
        Log(dev_mode_, "[dev] inverted_index::UpdateDocumentBase: вход - вектор из {} документов", num_docs);

        // Число потоков = min(документы, ядра)
        // токенизация - tokenizer::tokenize()
        const unsigned num_workers = std::min(
                static_cast<unsigned>(num_docs),
                std::max(1u, std::thread::hardware_concurrency())
        );
        Log(dev_mode_, "[dev] inverted_index::UpdateDocumentBase: потоков = {}", num_workers);
        std::vector<Dict> per_thread_dicts(num_workers);
        std::vector<std::jthread> workers;
        workers.reserve(num_workers);

        // Каждый поток обрабатывает свою долю документов (doc_id % num_workers == t)
        for (unsigned t = 0; t < num_workers; ++t) {
            workers.emplace_back([this, &searchable_texts, &per_thread_dicts, num_workers, t] {
                Dict &local = per_thread_dicts[t];
                auto indices = std::views::iota(static_cast<size_t>(t), docs_.size()) |
                               std::views::stride(static_cast<size_t>(num_workers));
                for (size_t doc_id: indices) {
                    std::unordered_map<std::string, size_t> word_count;
                    tokenize(searchable_texts[doc_id], word_count, static_cast<std::size_t>(max_word_length_));
                    size_t doc_len = 0;
                    for (auto &[w, count]: word_count) {
                        doc_len += count;
                        local[std::move(w)].push_back({doc_id, count});
                    }
                    doc_lengths_[doc_id] = doc_len;
                }
            });
        }

        workers.clear();

        Log(dev_mode_, "[dev] inverted_index::UpdateDocumentBase: слияние - объединяем per-thread словари (термин -> vector<Entry>) в один - Entry = (doc_id, count)");

        // Слияние локальных словарей потоков в один freq_dictionary_
        // Один документ обрабатывается ровно одним потоком (stride)
        // дубликатов doc_id в постингах нет
        Dict new_dict;
        for (Dict &local: per_thread_dicts) {
            for (auto &[word, list]: local) {
                auto &target = new_dict[word];
                target.insert(target.end(), std::make_move_iterator(list.begin()), std::make_move_iterator(list.end()));
            }
        }

        // Сортируем постинги по doc_id, чтобы GetWordCount возвращал готовый порядок без копирования
        Log(dev_mode_, "[dev] inverted_index::UpdateDocumentBase: сортировка постингов по doc_id внутри каждого термина");
        for (auto &[word, list]: new_dict) {
            std::ranges::sort(list, {}, &Entry::doc_id);
        }

        freq_dictionary_ = std::move(new_dict);
        Log(dev_mode_, "[dev] inverted_index::UpdateDocumentBase: индекс в памяти map<термин, vector<(doc_id, count)>>, {} терминов", freq_dictionary_.size());

        Save();
        Log(dev_mode_, "[dev] inverted_index::UpdateDocumentBase: готово");
    }

    const std::vector<Entry> &InvertedIndex::GetWordCount(std::string_view word) const {
        // Поиск по string_view без копирования ключа
        auto it = freq_dictionary_.find(word);
        if (it == freq_dictionary_.end()) {
            return kEmptyPostings;
        }

        return it->second;
    }

    const nlohmann::json &InvertedIndex::GetDocument(size_t doc_id) const {
        static const nlohmann::json kEmptyJson = nlohmann::json::object();
        if (doc_id >= docs_.size()) {
            return kEmptyJson;
        }

        return docs_[doc_id];
    }

    size_t InvertedIndex::GetDocumentLength(size_t doc_id) const noexcept {
        if (doc_id >= doc_lengths_.size()) {
            return 0;
        }
        return doc_lengths_[doc_id];
    }

    double InvertedIndex::GetAverageDocumentLength() const noexcept {
        if (doc_lengths_.empty()) {
            return 0.0;
        }

        double sum = 0;
        for (size_t len: doc_lengths_) {
            sum += static_cast<double>(len);
        }

        return sum / static_cast<double>(doc_lengths_.size());
    }

} // namespace fulltext_search_service
