#pragma once

#include <string>
#include <vector>

struct XbmIcon {
    int width = 0;
    int height = 0;
    std::vector<bool> pixels;  // row-major: pixels[y*width + x]
};

bool load_xbm(const std::string &path, XbmIcon *out);
