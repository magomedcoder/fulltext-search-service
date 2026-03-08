#pragma once

#include <charconv>
#include <nlohmann/json.hpp>
#include <httplib.h>

namespace fulltext_search_service {

    constexpr const char *kJsonContentType = "application/json";

    inline void sendJson(httplib::Response &res, int status, const nlohmann::json &body) {
        res.status = status;
        res.set_header("Content-Type", kJsonContentType);
        res.set_content(body.dump(), kJsonContentType);
    }

    inline int parseQueryInt(const httplib::Request &req, const char *name, int default_val, int min_val, int max_val) {
        if (!req.has_param(name)) {
            return default_val;
        }

        int value = default_val;
        auto sv = req.get_param_value(name);
        if (std::from_chars(sv.data(), sv.data() + sv.size(), value).ec != std::errc{}) {
            return default_val;
        }

        return std::clamp(value, min_val, max_val);
    }

} // namespace fulltext_search_service
