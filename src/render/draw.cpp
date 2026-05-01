#include "render/draw.hpp"
#include "core/colors.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace draw {

int text_width(const Font &f, std::string_view s) {
    int w = 0;
    for (unsigned char c : s) w += f.CharacterWidth(static_cast<uint32_t>(c));
    return w;
}

void text_top(Canvas *c, const Font &f, int x, int top_y,
              const Color &color, std::string_view text) {
    rgb_matrix::DrawText(c, f, x, top_y + f.baseline(), color,
                         std::string(text).c_str());
}

void text_centered(Canvas *c, const Font &f, int cx, int cy,
                   const Color &color, std::string_view text) {
    const int w = text_width(f, text);
    const int top = cy - f.height() / 2;
    rgb_matrix::DrawText(c, f, cx - w / 2, top + f.baseline(), color,
                         std::string(text).c_str());
}

void rounded_square(Canvas *c, int x0, int y0, int size, int radius,
                    const Color &color) {
    const float left = radius - 0.5f;
    const float right = static_cast<float>(size) - radius - 0.5f;
    const float top = radius - 0.5f;
    const float bot = static_cast<float>(size) - radius - 0.5f;
    const float rr = static_cast<float>(radius) * radius;
    for (int dy = 0; dy < size; ++dy) {
        const float cy = (dy < radius) ? top
                       : (dy >= size - radius ? bot : static_cast<float>(dy));
        for (int dx = 0; dx < size; ++dx) {
            const float cx = (dx < radius) ? left
                           : (dx >= size - radius ? right : static_cast<float>(dx));
            const float ex = dx - cx;
            const float ey = dy - cy;
            if (ex * ex + ey * ey < rr) {
                c->SetPixel(x0 + dx, y0 + dy, color.r, color.g, color.b);
            }
        }
    }
}

void icon(Canvas *c, const XbmIcon &ic, int x0, int y0, const Color &color) {
    for (int y = 0; y < ic.height; ++y) {
        for (int x = 0; x < ic.width; ++x) {
            if (ic.pixels[y * ic.width + x]) {
                c->SetPixel(x0 + x, y0 + y, color.r, color.g, color.b);
            }
        }
    }
}

void rect(Canvas *c, int x0, int y0, int x1, int y1, const Color &color) {
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            c->SetPixel(x, y, color.r, color.g, color.b);
        }
    }
}

void progress_bar(Canvas *c, int x0, int y0, int x1, int y1,
                  double frac, const Color &color) {
    rect(c, x0, y0, x1, y1, colors::DIM);
    if (frac <= 0) return;
    frac = std::min(frac, 1.0);
    const int fill_x = x0 + static_cast<int>(std::round((x1 - x0) * frac));
    if (fill_x >= x0) rect(c, x0, y0, fill_x, y1, color);
}

Color tint(const Color &c, double factor) {
    factor = std::clamp(factor, 0.0, 1.0);
    return Color(static_cast<uint8_t>(c.r * factor),
                 static_cast<uint8_t>(c.g * factor),
                 static_cast<uint8_t>(c.b * factor));
}

}  // namespace draw
