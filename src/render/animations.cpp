#include "render/animations.hpp"

#include <array>
#include <cmath>
#include <cstdint>

using namespace std;

namespace anim {

namespace {

using rgb_matrix::Canvas;

bool starts_with(string_view name, string_view prefix) {
    return name.substr(0, prefix.size()) == prefix;
}

void rain(Canvas *c, double t) {
    struct Drop {
        int x;
        double speed;
        double phase;
    };
    static constexpr array<Drop, 8> drops{{
        {9,  8.0, 0},
        {13, 7.0, 4},
        {17, 8.5, 7},
        {21, 7.5, 11},
        {25, 8.0, 2},
        {29, 7.0, 9},
        {33, 8.5, 5},
        {36, 7.5, 13},
    }};
    for (const auto &d : drops) {
        const double pos = fmod(t * d.speed + d.phase * 13, 18);
        const int y = 30 + static_cast<int>(pos);
        for (int dy = 0; dy < 3; ++dy) {
            const int yy = y - dy;
            if (yy < 30 || yy > 47) continue;
            const int b = 220 - dy * 80;
            c->SetPixel(d.x, yy, 110 * b / 255, 160 * b / 255, 220 * b / 255);
        }
    }
}

void snow(Canvas *c, double t) {
    struct Flake {
        double x0;
        double speed;
        double phase;
        double sway;
    };
    static constexpr array<Flake, 10> flakes{{
        {9,  2.0, 0,  1.0},
        {13, 2.5, 3,  0.8},
        {17, 2.2, 5,  1.2},
        {21, 1.8, 7,  0.9},
        {25, 2.1, 11, 1.0},
        {29, 1.7, 2,  0.8},
        {33, 2.4, 8,  1.0},
        {36, 2.0, 4,  0.7},
        {12, 1.6, 13, 0.9},
        {20, 2.3, 17, 1.1},
    }};
    for (const auto &f : flakes) {
        const double pos = fmod(t * f.speed + f.phase * 11, 18);
        const int y = 30 + static_cast<int>(pos);
        const int x = static_cast<int>(f.x0 + sin(t * 0.7 + f.phase) * f.sway);
        if (x < 6 || x > 37 || y < 30 || y > 47) continue;
        c->SetPixel(x, y, 220, 230, 240);
    }
}

void drizzle(Canvas *c, double t) {
    struct Drop {
        int x;
        double speed;
        double phase;
    };
    static constexpr array<Drop, 5> drops{{
        {10, 5.5, 0},
        {14, 6.0, 3},
        {18, 5.0, 7},
        {22, 6.5, 1},
        {26, 5.2, 5},
    }};
    for (const auto &d : drops) {
        const double pos = fmod(t * d.speed + d.phase * 11, 18);
        const int y = 30 + static_cast<int>(pos);
        for (int dy = 0; dy < 2; ++dy) {
            const int yy = y - dy;
            if (yy < 30 || yy > 47) continue;
            const int b = 220 - dy * 80;
            c->SetPixel(d.x, yy, 110 * b / 255, 160 * b / 255, 220 * b / 255);
        }
    }
}

void sun_sparkle(Canvas *c, double t) {
    struct Spark {
        int x;
        int y;
        double phase;
    };
    static constexpr array<Spark, 4> sparks{{
        {6, 17, 0},
        {37, 17, 1.7},
        {6, 48, 3.4},
        {37, 48, 5.1},
    }};
    for (const auto &s : sparks) {
        const double v = sin(t * 0.35 + s.phase);
        if (v < 0.6) continue;
        const int b = static_cast<int>((v - 0.6) / 0.4 * 200);
        c->SetPixel(s.x, s.y, 255 * b / 255, 220 * b / 255, 100 * b / 255);
    }
}

}  // namespace

void weather_icon(Canvas *c, string_view name, int /*code*/, double t) {
    if (starts_with(name, "rain0")) {
        drizzle(c, t);
        return;
    }
    if (starts_with(name, "rain")) {
        rain(c, t);
        return;
    }
    if (starts_with(name, "snow")) {
        snow(c, t);
        return;
    }
    if (name == "sun") {
        sun_sparkle(c, t);
        return;
    }
    // All other icons render as static (no animation).
}

}  // namespace anim
