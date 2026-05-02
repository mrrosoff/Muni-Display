#include "render/render.hpp"

#include "core/colors.hpp"
#include "core/config.hpp"
#include "data/caches.hpp"
#include "data/weather_codes.hpp"
#include "render/animations.hpp"
#include "render/draw.hpp"
#include "util/time_utils.hpp"

#include "graphics.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <mutex>
#include <string>
#include <string_view>

using namespace std;

namespace render {

namespace {

using rgb_matrix::Canvas;
using rgb_matrix::Color;

struct Line {
    string_view label;
    Color color;
};

const array<Line, 3> ROWS{{
    {"K", colors::RAIL_BLUE},
    {"L", colors::RAIL_PURPLE},
    {"M", colors::RAIL_GREEN},
}};

struct RowTimes {
    array<int, 2> minutes{};
    int count = 0;
    bool cold = true;
};

RowTimes line_times(string_view line) {
    RowTimes rt;
    rt.cold = caches::stop.cold;
    const int age_min = static_cast<int>((tu::now_unix() - caches::stop.last_fetch) / 60);
    for (const auto &d : caches::stop.departures) {
        if (d.line != line) continue;
        const int adj = d.minutes - age_min;
        if (adj < 1) continue;
        rt.minutes[rt.count++] = adj;
        if (rt.count >= 2) break;
    }
    return rt;
}

// Laundry remaining-time + completion fraction. remaining_min < 0 ⇒ unknown.
struct Metrics {
    int remaining_min;
    double frac;
};
Metrics laundry_metrics(const ApplianceState &s) {
    const double now = static_cast<double>(tu::now_unix());
    const double elapsed = (s.started_at > 0) ? max(now - s.started_at, 0.0) : 0.0;
    if (s.avg_min <= 0) return {-1, 0.0};
    const double total = static_cast<double>(s.avg_min) * 60;
    const double remaining = max(total - elapsed, 0.0);
    return {static_cast<int>(remaining / 60), min(elapsed / total, 1.0)};
}

void draw_check(Canvas *c, int x, int y, const Color &color) {
    static constexpr int pts[7][2] = {
        {0, 2}, {1, 3}, {2, 4}, {3, 3}, {4, 2}, {5, 1}, {6, 0}
    };
    for (const auto &p : pts) {
        c->SetPixel(x + p[0], y + p[1], color.r, color.g, color.b);
        c->SetPixel(x + p[0], y + p[1] + 1, color.r, color.g, color.b);
    }
}

void draw_drum_spin(Canvas *c, int icon_x, int icon_y, const Color &color, double phase) {
    const int cx = icon_x + 16, cy = icon_y + 19;
    constexpr double arc_len = 2.4;
    constexpr double r_min = 1.5, r_max = 4.0;
    const int rmax_int = static_cast<int>(ceil(r_max));
    for (int dy = -rmax_int; dy <= rmax_int; ++dy) {
        for (int dx = -rmax_int; dx <= rmax_int; ++dx) {
            const double d = hypot(static_cast<double>(dx), static_cast<double>(dy));
            if (d < r_min || d > r_max) continue;
            const double ang = atan2(static_cast<double>(dy), static_cast<double>(dx));
            double back = fmod(phase - ang, 2 * M_PI);
            if (back < 0) back += 2 * M_PI;
            if (back > arc_len) continue;
            const double falloff = 1.0 - (back / arc_len);
            const auto t = draw::tint(color, 0.15 + 0.85 * falloff);
            c->SetPixel(cx + dx, cy + dy, t.r, t.g, t.b);
        }
    }
}

struct ApplianceCtx {
    Canvas *canvas;
    const Fonts &fonts;
    const map<string, XbmIcon> &icons;
    string_view title;
    string_view icon_name;
    Color accent;
};

void draw_appliance_header(const ApplianceCtx &ctx) {
    draw::text_centered(ctx.canvas, ctx.fonts.title, 32, 6, colors::GREY, ctx.title);
    rgb_matrix::DrawLine(ctx.canvas, 0, 11, 63, 11, colors::DIM);
}

void draw_done(const ApplianceCtx &ctx) {
    const double breath = 0.78 + 0.22 * sin(tu::monotonic() * 2.0);
    draw_appliance_header(ctx);

    if (auto it = ctx.icons.find(string(ctx.icon_name)); it != ctx.icons.end()) {
        draw::icon(ctx.canvas, it->second, 16, 14, ctx.accent);
    }

    constexpr string_view text = "DONE";
    const int tw = draw::text_width(ctx.fonts.badge_1, text);
    constexpr int cw_w = 7, gap = 3;
    const int total = tw + gap + cw_w;
    const int x0 = 32 - total / 2;
    draw::text_top(
        ctx.canvas, ctx.fonts.badge_1, x0, 49, draw::tint(colors::WHITE, breath), text
    );
    draw_check(ctx.canvas, x0 + tw + gap, 52, draw::tint(colors::DONE_GREEN, breath));
}

void draw_appliance(const ApplianceCtx &ctx, const ApplianceState &state) {
    const auto m = laundry_metrics(state);
    if (m.remaining_min >= 0 && m.frac >= 1.0) {
        draw_done(ctx);
        return;
    }

    draw_appliance_header(ctx);

    if (auto it = ctx.icons.find(string(ctx.icon_name)); it != ctx.icons.end()) {
        draw::icon(ctx.canvas, it->second, 2, 14, ctx.accent);
        draw_drum_spin(ctx.canvas, 2, 14, ctx.accent, tu::monotonic() * 4.0);
    }

    constexpr int rx = 38;
    if (m.remaining_min >= 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", m.remaining_min);
        draw::text_top(ctx.canvas, ctx.fonts.badge_1, rx, 14, colors::YELLOW, buf);
        draw::text_top(ctx.canvas, ctx.fonts.dir, rx, 30, colors::GREY, "MIN");
        draw::text_top(ctx.canvas, ctx.fonts.dir, rx, 37, colors::GREY, "LEFT");
        const int pct = static_cast<int>(round(m.frac * 100));
        snprintf(buf, sizeof(buf), "%d%%", pct);
        draw::text_centered(ctx.canvas, ctx.fonts.row, 32, 51, colors::AMBER, buf);
        draw::progress_bar(ctx.canvas, {2, 58, 61, 60}, m.frac, ctx.accent);
    } else {
        draw::text_top(ctx.canvas, ctx.fonts.badge_1, rx, 18, colors::YELLOW, "ON");
        draw::text_centered(ctx.canvas, ctx.fonts.row, 32, 55, colors::LABEL, "RUNNING");
    }
}

}  // namespace

void muni(Canvas *canvas, const Fonts &fonts) {
    lock_guard lg(caches::mtx);
    canvas->Clear();
    draw::text_top(canvas, fonts.title, 2, 1, colors::GREY, "CASTRO");
    constexpr string_view dir_label = "East";
    const int dw = draw::text_width(fonts.dir, dir_label);
    draw::text_top(canvas, fonts.dir, 63 - dw, 2, colors::LABEL, dir_label);
    rgb_matrix::DrawLine(canvas, 0, 10, 63, 10, colors::DIM);

    struct R {
        Line line;
        RowTimes times;
    };
    array<R, 3> rows{};
    transform(ROWS.begin(), ROWS.end(), rows.begin(), [](const Line &l) {
        return R{l, line_times(l.label)};
    });
    sort(rows.begin(), rows.end(), [](const R &a, const R &b) {
        const bool ah = a.times.count > 0;
        const bool bh = b.times.count > 0;
        if (ah != bh) return ah;
        if (!ah) return false;
        return a.times.minutes[0] < b.times.minutes[0];
    });

    const auto has_times = [](const R &r) { return r.times.count > 0; };
    const auto is_cold = [](const R &r) { return r.times.cold; };
    const bool any_times = any_of(rows.begin(), rows.end(), has_times);
    const bool any_cold = any_of(rows.begin(), rows.end(), is_cold);

    if (!any_times) {
        const bool errored =
            any_cold && caches::stop.consecutive_failures >= cfg::FAIL_THRESHOLD;
        const string_view msg =
            errored ? "Error" : (any_cold ? "Loading..." : "No Trains");
        const Color &c = errored ? colors::RED : (any_cold ? colors::LABEL : colors::RED);
        draw::text_centered(canvas, fonts.row, 32, 38, c, msg);
        return;
    }

    for (size_t i = 0; i < rows.size(); ++i) {
        const auto &r = rows[i];
        const int y_top = 13 + static_cast<int>(i) * 17;
        constexpr int badge_size = 16;
        constexpr int x0 = 2;
        const int y0 = y_top;
        const int cx = x0 + badge_size / 2;
        const int cy = y0 + badge_size / 2;
        draw::rounded_square(canvas, x0, y0, badge_size, 8, r.line.color);
        const int dx = (r.line.label == "L") ? -1 : 0;
        draw::text_centered(
            canvas, fonts.badge_1, cx + dx, cy, colors::WHITE, r.line.label
        );

        int x = 20;
        if (r.times.count == 0) {
            draw::text_top(canvas, fonts.row, 20, y_top + 5, colors::DIM, "--");
        } else if (r.times.count == 1) {
            const auto s = to_string(r.times.minutes[0]);
            draw::text_top(canvas, fonts.row, x, y_top + 5, colors::YELLOW, s);
            x += draw::text_width(fonts.row, s);
            draw::text_top(canvas, fonts.row, x + 2, y_top + 5, colors::AMBER, "min");
        } else {
            const auto first = to_string(r.times.minutes[0]) + ",";
            const auto second = to_string(r.times.minutes[1]);
            draw::text_top(canvas, fonts.row, x, y_top + 5, colors::YELLOW, first);
            x += draw::text_width(fonts.row, first) + 1;
            draw::text_top(canvas, fonts.row, x, y_top + 5, colors::YELLOW, second);
            x += draw::text_width(fonts.row, second);
            draw::text_top(canvas, fonts.row, x + 2, y_top + 5, colors::AMBER, "min");
        }
    }
}

void weather(Canvas *canvas, const Fonts &fonts, const map<string, XbmIcon> &icons) {
    lock_guard lg(caches::mtx);
    canvas->Clear();
    const int day = caches::weather.have ? caches::weather.day_index : 0;
    const auto header = tu::month_day_for(day < 0 ? 0 : day);
    draw::text_centered(canvas, fonts.title, 32, 6, colors::GREY, header);
    rgb_matrix::DrawLine(canvas, 0, 11, 63, 11, colors::DIM);

    if (!caches::weather.have) {
        const bool errored = caches::weather.consecutive_failures >= cfg::FAIL_THRESHOLD;
        const string_view msg = errored ? "Error" : "Loading...";
        const Color &c = errored ? colors::RED : colors::LABEL;
        draw::text_centered(canvas, fonts.row, 32, 38, c, msg);
        return;
    }

    const int code = caches::weather.data.code;
    const auto icon_name = string(icon_for_code(code));
    auto px_it = icons.find(icon_name);
    if (px_it == icons.end()) px_it = icons.find("cloud");
    if (px_it != icons.end()) {
        draw::icon(canvas, px_it->second, 6, 17, colors::ICON);
    }
    anim::weather_icon(canvas, icon_name, code, tu::monotonic());

    auto word = string(word_for_code(code));
    tu::to_upper(word);

    constexpr int rx = 41;
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%d\xc2\xb0", caches::weather.data.hi);
    draw::text_top(canvas, fonts.title, rx, 17, colors::YELLOW, tmp);
    snprintf(tmp, sizeof(tmp), "%d\xc2\xb0", caches::weather.data.lo);
    draw::text_top(canvas, fonts.row, rx, 29, colors::LABEL, tmp);
    if (caches::weather.data.precip < 10) {
        snprintf(tmp, sizeof(tmp), "%dmph", caches::weather.data.wind_mph);
        draw::text_top(canvas, fonts.dir, rx, 40, colors::PRECIP_BLUE, tmp);
    } else {
        snprintf(tmp, sizeof(tmp), "%d%%", caches::weather.data.precip);
        draw::text_top(canvas, fonts.row, rx, 39, colors::PRECIP_BLUE, tmp);
    }

    draw::text_centered(canvas, fonts.row, 32, 55, colors::LABEL, word);
}

void laundry(Canvas *canvas, const Fonts &fonts, const map<string, XbmIcon> &icons) {
    lock_guard lg(caches::mtx);
    canvas->Clear();
    if (!caches::laundry.have) {
        draw::text_centered(canvas, fonts.row, 32, 32, colors::LABEL, "Loading...");
        return;
    }
    const ApplianceCtx washer_ctx{
        canvas, fonts, icons, "WASHER", "washer", colors::WASHER
    };
    const ApplianceCtx dryer_ctx{canvas, fonts, icons, "DRYER", "dryer", colors::DRYER};
    const bool washer_on = caches::laundry.data.washer.on;
    const bool dryer_on = caches::laundry.data.dryer.on;
    if (washer_on && dryer_on) {
        const bool show_washer =
            (static_cast<int>(tu::now_unix() / cfg::LAUNDRY_ROTATE.count()) % 2) == 0;
        if (show_washer)
            draw_appliance(washer_ctx, caches::laundry.data.washer);
        else
            draw_appliance(dryer_ctx, caches::laundry.data.dryer);
    } else if (washer_on) {
        draw_appliance(washer_ctx, caches::laundry.data.washer);
    } else if (dryer_on) {
        draw_appliance(dryer_ctx, caches::laundry.data.dryer);
    }
}

}  // namespace render
