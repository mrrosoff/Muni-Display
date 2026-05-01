#include "xbm.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

bool load_xbm(const std::string &path, XbmIcon *out) {
    std::ifstream f(path);
    if (!f) return false;
    std::stringstream ss;
    ss << f.rdbuf();
    const std::string text = ss.str();

    auto find_define = [&](const char *suffix, int *value) -> bool {
        const std::string needle = std::string("_") + suffix + " ";
        size_t pos = text.find(needle);
        if (pos == std::string::npos) return false;
        pos += needle.size();
        *value = std::atoi(text.c_str() + pos);
        return true;
    };

    if (!find_define("width", &out->width) ||
        !find_define("height", &out->height)) {
        return false;
    }

    std::vector<unsigned char> bytes;
    bytes.reserve(((out->width + 7) / 8) * out->height);
    size_t pos = 0;
    while ((pos = text.find("0x", pos)) != std::string::npos) {
        char *end = nullptr;
        unsigned long v = std::strtoul(text.c_str() + pos, &end, 16);
        bytes.push_back(static_cast<unsigned char>(v & 0xff));
        pos = (size_t)(end - text.c_str());
    }

    const int row_bytes = (out->width + 7) / 8;
    out->pixels.assign(out->width * out->height, false);
    for (int y = 0; y < out->height; ++y) {
        for (int x = 0; x < out->width; ++x) {
            unsigned char b = bytes[y * row_bytes + x / 8];
            if (b & (1 << (x % 8))) {
                out->pixels[y * out->width + x] = true;
            }
        }
    }
    return true;
}
