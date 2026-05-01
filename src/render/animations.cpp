#include "render/animations.hpp"

#include <array>
#include <cmath>
#include <cstdint>

namespace anim {

namespace {

using rgb_matrix::Canvas;

// Returns true if `name` starts with `prefix`.
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

void wind(Canvas *c, double t) {
    struct Streak { int y; double speed; double phase; int len; };
    static constexpr std::array<Streak, 4> streaks{{
        {22, 14, 0, 5}, {28, 11, 3, 4}, {34, 13, 7, 5}, {40, 12, 11, 4},
    }};
    for (const auto &s : streaks) {
        const double pos = std::fmod(t * s.speed + s.phase * 9, 50);
        const int x_head = -10 + static_cast<int>(pos);
        for (int i = 0; i < s.len; ++i) {
            const int x = x_head - i;
            if (x < 6 || x > 37) continue;
            int b = 200 - i * 30;
            if (b < 40) b = 40;
            c->SetPixel(x, s.y, 200*b/255, 220*b/255, 235*b/255);
        }
    }
}

void fog(Canvas *c, double t) {
    for (int row = 0; row < 3; ++row) {
        const int y = 22 + row * 8;
        const double phase = row * 1.7;
        for (int dx = 0; dx < 32; ++dx) {
            const double v = std::sin((dx + t * 4.0 + phase) * 0.4);
            if (v < 0.3) continue;
            const int b = static_cast<int>((v - 0.3) / 0.7 * 90);
            c->SetPixel(6 + dx, y, 130*b/255, 140*b/255, 150*b/255);
        }
    }
}

void lightning(Canvas *c, double t) {
    const double cycle = std::fmod(t, 6.0);
    if (cycle >= 0.15) return;
    const int b = static_cast<int>((1.0 - cycle / 0.15) * 200);
    for (int y = 17; y < 30; ++y) {
        for (int dx = 0; dx < 32; dx += 2) {
            c->SetPixel(6 + dx, y, 255*b/255, 245*b/255, 200*b/255);
        }
    }
}

void cloud_gust(Canvas *c, double t) {
    const double cycle = std::fmod(t, 10.0);
    if (cycle >= 2.0) return;
    static constexpr std::array<int, 2> ys{38, 44};
    for (int i = 0; i < 2; ++i) {
        const int x_head = 4 + static_cast<int>(cycle * 18) + i * 6;
        for (int k = 0; k < 4; ++k) {
            const int x = x_head - k;
            if (x < 6 || x > 37) continue;
            const double fade = std::sin((cycle / 2.0) * M_PI);
            int b = static_cast<int>((180 - k * 40) * fade);
            if (b < 0) b = 0;
            c->SetPixel(x, ys[i], 180*b/255, 195*b/255, 215*b/255);
        }
    }
}

void moon_stars(Canvas *c, double t) {
    struct Star { int x; int y; double phase; };
    static constexpr std::array<Star, 5> stars{{
        {4,18,0}, {38,22,2.1}, {7,46,4.2}, {35,44,1.0}, {32,17,3.3},
    }};
    for (const auto &s : stars) {
        const double v = std::sin(t * 0.7 + s.phase);
        if (v < 0.7) continue;
        const int b = static_cast<int>((v - 0.7) / 0.3 * 220);
        c->SetPixel(s.x, s.y, 230*b/255, 230*b/255, 255*b/255);
    }
}

}  // namespace

void weather_icon(Canvas *c, std::string_view name, int code, double t) {
    if (starts_with(name, "rain"))                    { rain(c, t); return; }
    if (starts_with(name, "snow"))                    { snow(c, t); return; }
    if (name == "sun")                                { sun_sparkle(c, t); return; }
    if (name == "wind")                               { wind(c, t); return; }
    if (code == 45 || code == 48)                     { fog(c, t); return; }
    if (name.find("lightning") != std::string_view::npos) {
        lightning(c, t); return;
    }
    if (name.find("cloud") != std::string_view::npos) { cloud_gust(c, t); return; }
    if (name == "moon")                               { moon_stars(c, t); return; }
}

}  // namespace anim
