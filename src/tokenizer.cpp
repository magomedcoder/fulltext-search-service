#include "tokenizer.hpp"
#include <ranges>

namespace fulltext_search_service {

    void tokenize(const std::string &text, std::unordered_map<std::string, size_t> &out) {
        out.reserve(out.size() + 32);
        for (auto word_range: text | std::views::split(' ')) {
            std::string word;
            word.reserve(32);
            for (char c: word_range) {
                word += c;
            }
            if (!word.empty() && word.size() <= kMaxWordLength) {
                ++out[std::move(word)];
            }
        }
    }

} // namespace fulltext_search_service
