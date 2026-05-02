#include "util/xbm.hpp"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

using namespace std;

bool load_xbm(const string &path, XbmIcon *out) {
    ifstream f(path);
    if (!f) return false;
    stringstream ss;
    ss << f.rdbuf();
    const string text = ss.str();

    const auto find_define = [&](const char *suffix, int *value) {
        const string needle = string("_") + suffix + " ";
        const auto pos = text.find(needle);
        if (pos == string::npos) return false;
        *value = atoi(text.c_str() + pos + needle.size());
        return true;
    };

    if (!find_define("width", &out->width) || !find_define("height", &out->height)) {
        return false;
    }

    vector<unsigned char> bytes;
    bytes.reserve(((out->width + 7) / 8) * out->height);
    size_t pos = 0;
    while ((pos = text.find("0x", pos)) != string::npos) {
        char *end = nullptr;
        const unsigned long v = strtoul(text.c_str() + pos, &end, 16);
        bytes.push_back(static_cast<unsigned char>(v & 0xff));
        pos = static_cast<size_t>(end - text.c_str());
    }

    const int row_bytes = (out->width + 7) / 8;
    out->pixels.assign(out->width * out->height, false);
    for (int y = 0; y < out->height; ++y) {
        for (int x = 0; x < out->width; ++x) {
            const unsigned char b = bytes[y * row_bytes + x / 8];
            if (b & (1 << (x % 8))) out->pixels[y * out->width + x] = true;
        }
    }
    return true;
}
