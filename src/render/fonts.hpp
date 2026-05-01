#pragma once

#include "graphics.h"

struct Fonts {
    rgb_matrix::Font title;    // 6x10
    rgb_matrix::Font dir;      // 4x6 (small captions)
    rgb_matrix::Font row;      // 5x7 (body rows, percent, etc.)
    rgb_matrix::Font badge_1;  // 9x15B (single-char badges, big numbers)

    bool load();  // false if any LoadFont fails
};
