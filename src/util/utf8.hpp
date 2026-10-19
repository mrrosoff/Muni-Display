#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace utf8 {

// Decodes the code point starting at s[*i] and advances *i past it. Malformed
// sequences decode as the raw lead byte.
inline uint32_t next(std::string_view s, size_t *i) {
    const auto b0 = static_cast<unsigned char>(s[(*i)++]);
    const int extra = b0 >= 0xF8   ? 0
                      : b0 >= 0xF0 ? 3
                      : b0 >= 0xE0 ? 2
                      : b0 >= 0xC0 ? 1
                                   : 0;
    uint32_t cp = extra ? (b0 & (0x3F >> extra)) : b0;
    for (int k = 0; k < extra; ++k) {
        if (*i >= s.size() || (static_cast<unsigned char>(s[*i]) & 0xC0) != 0x80)
            return b0;
        cp = (cp << 6) | (static_cast<unsigned char>(s[(*i)++]) & 0x3F);
    }
    return cp;
}

}  // namespace utf8
