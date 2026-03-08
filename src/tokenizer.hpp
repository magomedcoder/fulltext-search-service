#pragma once

#include <cstddef>
#include <string>
#include <unordered_map>

namespace fulltext_search_service {

    constexpr std::size_t kMaxWordLength = 100;

    void tokenize(const std::string &text, std::unordered_map<std::string, size_t> &out);

} // namespace fulltext_search_service
