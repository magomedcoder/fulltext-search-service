#include "stemmer.hpp"
#include <libstemmer.h>
#include <mutex>
#include <string>

namespace fulltext_search_service {

    struct Stemmer::Impl {
        sb_stemmer *stemmer = nullptr;
        mutable std::mutex mtx;

        ~Impl() {
            if (stemmer) {
                sb_stemmer_delete(stemmer);
                stemmer = nullptr;
            }
        }
    };

    static constexpr const char *kUtf8Encoding = "UTF_8";

    std::unique_ptr<Stemmer> Stemmer::create(const char *algorithm, const char *charenc) {
        if (!charenc) {
            charenc = kUtf8Encoding;
        }
        sb_stemmer *s = sb_stemmer_new(algorithm, charenc);
        if (!s) {
            return nullptr;
        }

        auto impl = std::make_unique<Impl>();
        impl->stemmer = s;
        return std::unique_ptr<Stemmer>(new Stemmer(std::move(impl)));
    }

    Stemmer::Stemmer(std::unique_ptr<Impl> impl) : impl_(std::move(impl)) {}

    Stemmer::~Stemmer() = default;

    std::string Stemmer::normalize(const std::string &word) const {
        if (word.empty() || !impl_ || !impl_->stemmer) {
            return word;
        }

        std::lock_guard lock(impl_->mtx);
        const sb_symbol *input = reinterpret_cast<const sb_symbol *>(word.data());
        int size = static_cast<int>(word.size());
        const sb_symbol *out = sb_stemmer_stem(impl_->stemmer, input, size);
        if (!out) {
            return word;
        }

        int len = sb_stemmer_length(impl_->stemmer);

        return std::string(reinterpret_cast<const char *>(out), static_cast<size_t>(len));
    }

} // namespace fulltext_search_service
