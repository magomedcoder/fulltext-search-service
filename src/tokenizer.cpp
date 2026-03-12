#include "tokenizer.hpp"
#include "stemmer.hpp"
#include "utils.hpp"
#include <ranges>

namespace fulltext_search_service {

    void tokenize(
            const std::string &text,
            std::unordered_map<std::string, size_t> &out,
            std::size_t max_word_length,
            const Stemmer *stemmer
    ) {
        out.reserve(out.size() + 32);
        for (auto word_range: text | std::views::split(' ')) {
            std::string word;
            word.reserve(32);
            for (char c: word_range) {
                word += c;
            }

            if (!word.empty() && word.size() <= max_word_length) {
                ToLowerUtf8(word);
                std::string key = stemmer ? stemmer->normalize(word) : word;
                if (!key.empty()) {
                    ++out[std::move(key)];
                }
            }
        }
    }

} // namespace fulltext_search_service
