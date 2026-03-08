#pragma once

#include "inverted_index.hpp"
#include "search.hpp"
#include <httplib.h>

namespace fulltext_search_service {

    struct ApiConfig {
        static constexpr int kDefaultLimit = 10;
        static constexpr int kMaxLimit = 1000;
        static constexpr int kMaxOffset = 10000;
        static constexpr int kDefaultMaxResponses = 5;
    };

    void handleSearch(
            InvertedIndex &index,
            Search &search,
            int max_responses,
            const httplib::Request &req,
            httplib::Response &res
    );

    void handleGetDocuments(InvertedIndex &index, const httplib::Request &req, httplib::Response &res);

    void handlePostDocuments(InvertedIndex &index, const httplib::Request &req, httplib::Response &res);

} // namespace fulltext_search_service
