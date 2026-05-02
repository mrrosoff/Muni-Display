#pragma once

#include "graphics.h"
#include "led-matrix.h"
#include "util/xbm.hpp"

#include <string_view>

namespace draw {

using rgb_matrix::Canvas;
using rgb_matrix::Color;
using rgb_matrix::Font;

struct Rect {
    int x0, y0, x1, y1;
};

int text_width(const Font &f, std::string_view s);

void text_top(
    Canvas *c,
    const Font &f,
    int x,
    int top_y,
    const Color &color,
    std::string_view text
);

void text_centered(
    Canvas *c,
    const Font &f,
    int cx,
    int cy,
    const Color &color,
    std::string_view text
);

void rounded_square(Canvas *c, int x0, int y0, int size, int radius, const Color &color);

void icon(Canvas *c, const XbmIcon &icon, int x0, int y0, const Color &color);

void rect(Canvas *c, Rect r, const Color &color);

void progress_bar(Canvas *c, Rect r, double frac, const Color &color);

Color tint(const Color &c, double factor);

}  // namespace draw
