#include "render/animations.hpp"

#include <array>
#include <cmath>
#include <cstdint>

namespace anim {

namespace {

using rgb_matrix::Canvas;

bool starts_with(std::string_view name, std::string_view prefix) {
    return name.substr(0, prefix.size()) == prefix;
}

void rain(Canvas *c, double t) {
    struct Drop { int x; double speed; double phase; };
    static constexpr std::array<Drop, 8> drops{{
        {10,22,0}, {14,18,4}, {18,24,7}, {22,20,11},
        {26,19,2}, {30,23,9}, {34,17,5}, {38,21,13},
    }};
    for (const auto &d : drops) {
        const double pos = std::fmod(t * d.speed + d.phase * 13, 22);
        const int y = 30 + static_cast<int>(pos);
        for (int dy = 0; dy < 3; ++dy) {
            const int yy = y - dy;
            if (yy < 30 || yy > 50) continue;
            const int b = 220 - dy * 80;
            c->SetPixel(d.x, yy, 110*b/255, 160*b/255, 220*b/255);
        }
    }
}

void snow(Canvas *c, double t) {
    struct Flake { double x0; double speed; double phase; double sway; };
    static constexpr std::array<Flake, 10> flakes{{
        {8,4,0,1.5}, {12,5,3,1.0}, {16,4.5,5,2.0}, {20,3.8,7,1.2},
        {24,4.2,11,1.8}, {28,3.5,2,1.0}, {32,5.2,8,1.5},
        {36,4.0,4,2.0}, {40,3.2,13,1.0}, {14,4.8,17,1.7},
    }};
    for (const auto &f : flakes) {
        const double pos = std::fmod(t * f.speed + f.phase * 11, 22);
        const int y = 30 + static_cast<int>(pos);
        const int x = static_cast<int>(f.x0 + std::sin(t * 1.5 + f.phase) * f.sway);
        if (x < 6 || x > 37 || y < 30 || y > 50) continue;
        c->SetPixel(x, y, 220, 230, 240);
    }
}

void sun_sparkle(Canvas *c, double t) {
    struct Spark { int x; int y; double phase; };
    static constexpr std::array<Spark, 4> sparks{{
        {3,14,0}, {38,14,1.7}, {3,47,3.4}, {38,47,5.1},
    }};
    for (const auto &s : sparks) {
        const double v = std::sin(t * 0.8 + s.phase);
        if (v < 0.6) continue;
        const int b = static_cast<int>((v - 0.6) / 0.4 * 200);
        c->SetPixel(s.x, s.y, 255*b/255, 220*b/255, 100*b/255);
    }
}

}  // namespace

void weather_icon(Canvas *c, std::string_view name, int /*code*/, double t) {
    if (starts_with(name, "rain")) { rain(c, t); return; }
    if (starts_with(name, "snow")) { snow(c, t); return; }
    if (name == "sun")             { sun_sparkle(c, t); return; }
    // All other icons render as static (no animation).
}

}  // namespace anim
