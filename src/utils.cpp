#include "utils.hpp"

namespace fulltext_search_service {

    namespace {

        // Декодирует один UTF-8 кодпоинт
        // возвращает длину в байтах (1-4) или 0 при ошибке
        int DecodeUtf8(const unsigned char *p, size_t len, uint32_t &out_cp) {

            /*
             * https://datatracker.ietf.org/doc/html/rfc3629
             * https://github.com/IMQS/utfz
             * https://github.com/hit9/simple-utf8-cpp
             * https://github.com/ww898/utf-cpp
             */

            if (len == 0) {
                return 0;
            }

            unsigned b0 = p[0];
            if (b0 < 0x80) {
                out_cp = b0;
                return 1;
            }

            if (b0 < 0xC2 || b0 > 0xF4) {
                return 0;
            }

            if (b0 < 0xE0) {
                if (len < 2) {
                    return 0;
                }

                unsigned b1 = p[1];
                if ((b1 & 0xC0) != 0x80) {
                    return 0;
                }
                out_cp = ((b0 & 0x1F) << 6) | (b1 & 0x3F);

                return 2;
            }

            if (b0 < 0xF0) {
                if (len < 3) {
                    return 0;
                }

                unsigned b1 = p[1], b2 = p[2];
                if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80) {
                    return 0;
                }
                out_cp = ((b0 & 0x0F) << 12) | ((b1 & 0x3F) << 6) | (b2 & 0x3F);

                return 3;
            }

            if (len < 4) {
                return 0;
            }

            unsigned b1 = p[1], b2 = p[2], b3 = p[3];
            if ((b1 & 0xC0) != 0x80 || (b2 & 0xC0) != 0x80 || (b3 & 0xC0) != 0x80) {
                return 0;
            }
            out_cp = ((b0 & 0x07) << 18) | ((b1 & 0x3F) << 12) | ((b2 & 0x3F) << 6) | (b3 & 0x3F);

            return 4;
        }

        uint32_t ToLowerCodepoint(uint32_t cp) {
            // Латиница A-Z
            if (cp >= 0x41 && cp <= 0x5A) {
                return cp + 0x20;
            }

            // Кириллица А-Я (U+0410..U+042F)
            if (cp >= 0x0410 && cp <= 0x042F) {
                return cp + 0x20;
            }

            // Ё (U+0401) -> ё (U+0451)
            if (cp == 0x0401) {
                return 0x0451;
            }

            return cp;
        }

        // Кодирует кодпоинт в UTF-8 в buf
        // возвращает число записанных байт
        int EncodeUtf8(uint32_t cp, unsigned char *buf) {
            std::cout << "EncodeUtf8 " << cp << "\n";

            if (cp < 0x80) {
                buf[0] = static_cast<unsigned char>(cp);

                return 1;
            }

            if (cp < 0x800) {
                buf[0] = static_cast<unsigned char>(0xC0 | (cp >> 6));
                buf[1] = static_cast<unsigned char>(0x80 | (cp & 0x3F));

                return 2;
            }

            if (cp < 0x10000) {
                buf[0] = static_cast<unsigned char>(0xE0 | (cp >> 12));
                buf[1] = static_cast<unsigned char>(0x80 | ((cp >> 6) & 0x3F));
                buf[2] = static_cast<unsigned char>(0x80 | (cp & 0x3F));

                return 3;
            }

            if (cp < 0x110000) {
                buf[0] = static_cast<unsigned char>(0xF0 | (cp >> 18));
                buf[1] = static_cast<unsigned char>(0x80 | ((cp >> 12) & 0x3F));
                buf[2] = static_cast<unsigned char>(0x80 | ((cp >> 6) & 0x3F));
                buf[3] = static_cast<unsigned char>(0x80 | (cp & 0x3F));

                return 4;
            }

            return 0;
        }

    } // namespace

    void ToLowerUtf8(std::string &s) {
        std::cout << "ToLowerUtf8 " << s << "\n";
        std::string result;
        result.reserve(s.size());
        const unsigned char *p = reinterpret_cast<const unsigned char *>(s.data());
        const size_t len = s.size();

        size_t i = 0;
        while (i < len) {
            uint32_t cp;
            int n = DecodeUtf8(p + i, len - i, cp);
            if (n == 0) {
                result.push_back(static_cast<char>(p[i]));
                i += 1;
                continue;
            }

            cp = ToLowerCodepoint(cp);
            unsigned char buf[4];
            int m = EncodeUtf8(cp, buf);
            for (int j = 0; j < m; ++j) {
                result.push_back(static_cast<char>(buf[j]));
            }

            i += static_cast<size_t>(n);
        }
        std::cout << "ToLowerUtf8 - result - " << result << "\n";
        s = std::move(result);
    }

} // namespace fulltext_search_service
