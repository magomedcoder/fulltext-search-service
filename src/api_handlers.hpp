#pragma once

#include "config.hpp"
#include "index_registry.hpp"
#include "search.hpp"
#include <httplib.h>

namespace fulltext_search_service {

    void handleSearch(
            IndexRegistry &registry,
            const ApiConfigSection &api,
            const IndexConfig &index_config,
            const httplib::Request &req,
            httplib::Response &res,
            bool dev_mode = false
    );

    void handleGetDocuments(
            IndexRegistry &registry,
            const ApiConfigSection &api,
            const httplib::Request &req,
            httplib::Response &res,
            bool dev_mode = false
    );

    void handlePostDocuments(
            IndexRegistry &registry,
            const httplib::Request &req,
            httplib::Response &res,
            bool dev_mode = false
    );

    void handleListCollections(
            IndexRegistry &registry,
            const httplib::Request &req,
            httplib::Response &res,
            bool dev_mode = false
    );

    void handleGetCollection(
            IndexRegistry &registry,
            const httplib::Request &req,
            httplib::Response &res,
            bool dev_mode = false
    );

    void handlePostCollection(
            IndexRegistry &registry,
            const httplib::Request &req,
            httplib::Response &res,
            bool dev_mode = false
    );

    void handleDeleteCollection(
            IndexRegistry &registry,
            const httplib::Request &req,
            httplib::Response &res,
            bool dev_mode = false
    );

} // namespace fulltext_search_service
