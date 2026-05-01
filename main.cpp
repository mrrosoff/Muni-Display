#include "led-matrix.h"
#include "graphics.h"

#include "http.hpp"
#include "xbm.hpp"
#include "home_assistant.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <set>
#include <signal.h>
#include <string>
#include <unistd.h>
#include <vector>

using rgb_matrix::Canvas;
using rgb_matrix::Color;
using rgb_matrix::DrawLine;
using rgb_matrix::DrawText;
using rgb_matrix::Font;
using rgb_matrix::FrameCanvas;
using rgb_matrix::RGBMatrix;
using nlohmann::json;

// ---------- config ----------

static const char *AGENCY = "SF";
static const char *STOP_CODE = "15728";
static const std::set<std::string> STOP_LINES = {"K", "L", "M"};
static const long STOP_TTL_S = 60;

static const long WEATHER_TTL_S = 3600;
static const long WEATHER_RETRY_TTL_S = 120;

static const long LAUNDRY_TTL_S = 60;
static const long LAUNDRY_ACTIVE_TTL_S = 20;
static const long LAUNDRY_RETRY_TTL_S = 30;
static const int LAUNDRY_ROTATE_SECONDS = 30;

static const int NIGHT_START_MIN = 20 * 60 + 30;
static const int NIGHT_END_MIN = 7 * 60;
static const int DIM_START_MIN = 23 * 60;
static const int DIM_END_MIN = 5 * 60;
static const int DIM_BRIGHTNESS = 60;
static const int FULL_BRIGHTNESS = 100;

static const int CONNECT_TIMEOUT_S = 30;
static const int READ_TIMEOUT_S = 60;
static const long STOP_RETRY_TTL_S = 30;

// ---------- colors ----------

static const Color WHITE(255, 255, 255);
static const Color GREY(100, 100, 100);
static const Color DIM(40, 40, 40);
static const Color LABEL(200, 200, 200);
static const Color YELLOW(255, 200, 0);
static const Color AMBER(140, 120, 0);
static const Color RED(255, 80, 80);
static const Color ICON_COLOR(180, 200, 230);
static const Color PRECIP_BLUE(120, 170, 220);

static const Color RAIL_BLUE(70, 145, 205);
static const Color RAIL_PURPLE(155, 90, 175);
static const Color RAIL_GREEN(50, 165, 60);

static const Color WASHER_COLOR(80, 160, 220);
static const Color DRYER_COLOR(220, 130, 60);
static const Color DONE_GREEN(60, 220, 90);

struct LineDef {
    std::string label;
    Color color;
};
static const std::vector<LineDef> ROWS = {
    {"K", RAIL_BLUE},
    {"L", RAIL_PURPLE},
    {"M", RAIL_GREEN},
};

// ---------- signals ----------

static volatile bool interrupted = false;
static void on_signal(int) { interrupted = true; }

// ---------- logging ----------

static void log_msg(const char *fmt, ...) {
    char ts[16];
    struct timespec now;
    clock_gettime(CLOCK_REALTIME, &now);
    struct tm tm;
    localtime_r(&now.tv_sec, &tm);
    int ms = now.tv_nsec / 1000000;
    snprintf(ts, sizeof(ts), "%02d:%02d:%02d.%03d",
             tm.tm_hour, tm.tm_min, tm.tm_sec, ms);
    fprintf(stdout, "[%s] ", ts);
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stdout, fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
    fflush(stdout);
}

// ---------- time helpers ----------

static int now_minute_of_day() {
    time_t t = time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    return tm.tm_hour * 60 + tm.tm_min;
}

static bool is_night() {
    int m = now_minute_of_day();
    return m >= NIGHT_START_MIN || m < NIGHT_END_MIN;
}

static bool is_dim() {
    int m = now_minute_of_day();
    return m >= DIM_START_MIN || m < DIM_END_MIN;
}

static int desired_day_index() {
    time_t t = time(nullptr);
    struct tm tm;
    localtime_r(&t, &tm);
    return tm.tm_hour < 7 ? 0 : 1;
}

// ISO 8601 -> time_t (UTC). Accepts "Z" or "+HH:MM"/"-HH:MM" suffix.
static time_t parse_iso8601(const std::string &s) {
    struct tm tm = {};
    const char *p = strptime(s.c_str(), "%Y-%m-%dT%H:%M:%S", &tm);
    if (!p) return 0;
    int offset = 0;
    if (*p == '+' || *p == '-') {
        int sign = (*p == '+') ? 1 : -1;
        int hh = 0, mm = 0;
        sscanf(p + 1, "%d:%d", &hh, &mm);
        offset = sign * (hh * 3600 + mm * 60);
    }
    return timegm(&tm) - offset;
}

static double monotonic() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

// ---------- caches ----------

struct Departure {
    std::string line;
    int minutes;
};

struct StopCache {
    std::vector<Departure> departures;
    double last_fetch = 0.0;     // unix seconds
    bool cold = true;
    int consecutive_failures = 0;
};

struct WeatherData {
    int code = 0;
    int hi = 0;
    int lo = 0;
    int precip = 0;
    int wind_mph = 0;
    int day_index = -1;
};

struct WeatherCache {
    bool have = false;
    WeatherData data;
    double last_fetch = 0.0;
    int day_index = -1;
    int consecutive_failures = 0;
};

static const int FAIL_THRESHOLD = 5;

struct LaundryCache {
    bool have = false;
    LaundryData data;
    double last_fetch = 0.0;
    int consecutive_failures = 0;
};

static StopCache stop_cache;
static WeatherCache weather_cache;
static LaundryCache laundry_cache;
static bool ha_enabled = false;
static std::string ha_url;
static std::string ha_token;

// ---------- weather code -> icon/word ----------

static const std::map<int, std::string> CODE_ICON = {
    {0,"sun"},{1,"cloud_sun"},{2,"cloud_sun"},{3,"clouds"},
    {45,"cloud_wind"},{48,"cloud_wind"},
    {51,"rain0_sun"},{53,"rain0"},{55,"rain0"},{56,"rain0"},{57,"rain0"},
    {61,"rain1_sun"},{63,"rain1"},{65,"rain2"},{66,"rain1"},{67,"rain2"},
    {71,"snow_sun"},{73,"snow"},{75,"snow"},{77,"snow"},
    {80,"rain1_sun"},{81,"rain1"},{82,"rain2"},
    {85,"snow"},{86,"snow"},
    {95,"lightning"},{96,"rain_lightning"},{99,"rain_lightning"},
};

static const std::map<int, std::string> CODE_WORD = {
    {0,"Clear"},{1,"Clear"},{2,"Cloudy"},{3,"Cloudy"},
    {45,"Fog"},{48,"Fog"},
    {51,"Drizzle"},{53,"Drizzle"},{55,"Drizzle"},{56,"Drizzle"},{57,"Drizzle"},
    {61,"Rain"},{63,"Rain"},{65,"Rain"},{66,"Rain"},{67,"Rain"},
    {71,"Snow"},{73,"Snow"},{75,"Snow"},{77,"Snow"},
    {80,"Showers"},{81,"Showers"},{82,"Showers"},
    {85,"Snow"},{86,"Snow"},
    {95,"Storm"},{96,"Storm"},{99,"Storm"},
};

// ---------- fetchers ----------

static const char *MUNI_URL_TPL =
    "https://api.511.org/transit/StopMonitoring?"
    "api_key=%s&agency=%s&stopCode=%s&format=json";

static bool refresh_stop() {
    const char *api_key = getenv("MUNI_API_KEY");
    if (!api_key || !*api_key) {
        log_msg("stop: MUNI_API_KEY not set");
        return false;
    }
    char url[512];
    snprintf(url, sizeof(url), MUNI_URL_TPL, api_key, AGENCY, STOP_CODE);

    double t0 = monotonic();
    std::string body, err;
    if (!http::get(url, CONNECT_TIMEOUT_S, READ_TIMEOUT_S, &body, &err)) {
        stop_cache.consecutive_failures++;
        stop_cache.last_fetch = (double)time(nullptr) - (STOP_TTL_S - STOP_RETRY_TTL_S);
        log_msg("stop FAILED in %.2fs (#%d): %s",
                monotonic() - t0, stop_cache.consecutive_failures, err.c_str());
        return false;
    }

    // 511 returns UTF-8 BOM
    if (body.size() >= 3 &&
        (unsigned char)body[0] == 0xEF &&
        (unsigned char)body[1] == 0xBB &&
        (unsigned char)body[2] == 0xBF) {
        body.erase(0, 3);
    }

    try {
        json data = json::parse(body);
        const auto &delivery = data.at("ServiceDelivery");
        std::string ts = delivery.value("ResponseTimestamp", "");
        time_t now_server = ts.empty() ? time(nullptr) : parse_iso8601(ts);

        std::vector<Departure> deps;
        const auto &smd_it = delivery.find("StopMonitoringDelivery");
        if (smd_it != delivery.end()) {
            const auto &smd = *smd_it;
            const auto &visits_it = smd.find("MonitoredStopVisit");
            if (visits_it != smd.end() && visits_it->is_array()) {
                for (const auto &visit : *visits_it) {
                    const auto journey_it = visit.find("MonitoredVehicleJourney");
                    if (journey_it == visit.end()) continue;
                    std::string line = journey_it->value("LineRef", "");
                    if (!STOP_LINES.count(line)) continue;
                    const auto call_it = journey_it->find("MonitoredCall");
                    if (call_it == journey_it->end()) continue;
                    std::string arr_str = call_it->value("ExpectedArrivalTime", "");
                    if (arr_str.empty()) {
                        arr_str = call_it->value("AimedArrivalTime", "");
                    }
                    if (arr_str.empty()) continue;
                    time_t arrival = parse_iso8601(arr_str);
                    int minutes = (int)((arrival - now_server) / 60);
                    if (minutes < 1) continue;
                    deps.push_back({line, minutes});
                }
            }
        }
        std::sort(deps.begin(), deps.end(),
                  [](const Departure &a, const Departure &b) {
                      return a.minutes < b.minutes;
                  });

        stop_cache.departures = std::move(deps);
        stop_cache.last_fetch = (double)time(nullptr);
        stop_cache.cold = false;
        stop_cache.consecutive_failures = 0;
        log_msg("stop %s: %.2fs (%zu deps)", STOP_CODE,
                monotonic() - t0, stop_cache.departures.size());
        return true;
    } catch (const std::exception &e) {
        stop_cache.consecutive_failures++;
        stop_cache.last_fetch = (double)time(nullptr) - (STOP_TTL_S - STOP_RETRY_TTL_S);
        log_msg("stop parse FAILED in %.2fs (#%d): %s",
                monotonic() - t0, stop_cache.consecutive_failures, e.what());
        return false;
    }
}

static const char *WEATHER_URL =
    "https://api.open-meteo.com/v1/forecast?"
    "latitude=37.7605&longitude=-122.4356&"
    "daily=weather_code,temperature_2m_max,temperature_2m_min,"
    "precipitation_probability_max,wind_speed_10m_max,sunrise,sunset&"
    "temperature_unit=fahrenheit&"
    "wind_speed_unit=mph&"
    "timezone=America%2FLos_Angeles&"
    "forecast_days=2";

static bool refresh_weather(int day_index) {
    double t0 = monotonic();
    std::string body, err;
    if (!http::get(WEATHER_URL, CONNECT_TIMEOUT_S, READ_TIMEOUT_S, &body, &err)) {
        weather_cache.consecutive_failures++;
        weather_cache.last_fetch = time(nullptr) - (WEATHER_TTL_S - WEATHER_RETRY_TTL_S);
        log_msg("weather FAILED in %.2fs (#%d): %s",
                monotonic() - t0, weather_cache.consecutive_failures, err.c_str());
        return false;
    }
    try {
        json data = json::parse(body);
        const auto &d = data.at("daily");
        WeatherData wd;
        wd.code = d.at("weather_code").at(day_index).get<int>();
        double hi = d.at("temperature_2m_max").at(day_index).get<double>();
        double lo = d.at("temperature_2m_min").at(day_index).get<double>();
        wd.hi = (int)std::round(hi);
        wd.lo = (int)std::round(lo);
        const auto &precip = d.at("precipitation_probability_max").at(day_index);
        wd.precip = precip.is_null() ? 0 : (int)precip.get<double>();
        const auto &wind = d.at("wind_speed_10m_max").at(day_index);
        wd.wind_mph = wind.is_null() ? 0 : (int)std::round(wind.get<double>());
        wd.day_index = day_index;

        weather_cache.have = true;
        weather_cache.data = wd;
        weather_cache.day_index = day_index;
        weather_cache.last_fetch = (double)time(nullptr);
        weather_cache.consecutive_failures = 0;
        log_msg("weather day=%d: %.2fs", day_index, monotonic() - t0);
        return true;
    } catch (const std::exception &e) {
        weather_cache.consecutive_failures++;
        weather_cache.last_fetch = time(nullptr) - (WEATHER_TTL_S - WEATHER_RETRY_TTL_S);
        log_msg("weather parse FAILED in %.2fs (#%d): %s",
                monotonic() - t0, weather_cache.consecutive_failures, e.what());
        return false;
    }
}

static bool refresh_laundry() {
    if (!ha_enabled) return false;
    double t0 = monotonic();
    LaundryData data;
    std::string err;
    if (!ha_fetch_laundry(ha_url, ha_token, CONNECT_TIMEOUT_S, READ_TIMEOUT_S,
                          &data, &err)) {
        laundry_cache.consecutive_failures++;
        laundry_cache.last_fetch = (double)time(nullptr) - (LAUNDRY_TTL_S - LAUNDRY_RETRY_TTL_S);
        log_msg("laundry FAILED in %.2fs (#%d): %s",
                monotonic() - t0, laundry_cache.consecutive_failures, err.c_str());
        return false;
    }
    laundry_cache.have = true;
    laundry_cache.data = data;
    laundry_cache.last_fetch = (double)time(nullptr);
    laundry_cache.consecutive_failures = 0;
    log_msg("laundry: %.2fs (washer=%d dryer=%d)",
            monotonic() - t0,
            (int)data.washer.on, (int)data.dryer.on);
    return true;
}

static long laundry_ttl_now() {
    if (laundry_cache.have &&
        (laundry_cache.data.washer.on || laundry_cache.data.dryer.on)) {
        return LAUNDRY_ACTIVE_TTL_S;
    }
    return LAUNDRY_TTL_S;
}

static bool laundry_active() {
    if (!ha_enabled || !laundry_cache.have) return false;
    return laundry_cache.data.washer.on || laundry_cache.data.dryer.on;
}

// Pick the most-overdue fetch and run it. Returns true if anything ran.
static bool tick_fetcher() {
    double now = (double)time(nullptr);
    bool night = is_night();
    int desired_day = desired_day_index();

    double stop_overdue = now - stop_cache.last_fetch - STOP_TTL_S;
    bool stops_warm = !stop_cache.cold;
    bool weather_warm = weather_cache.have;
    double w_overdue = now - weather_cache.last_fetch - WEATHER_TTL_S;
    bool day_changed = weather_warm && weather_cache.day_index != desired_day;
    bool weather_allowed = night || day_changed;

    bool primary_warm = night ? weather_warm : stops_warm;
    bool ha_allowed = ha_enabled && primary_warm;
    double l_overdue = ha_enabled
        ? (now - laundry_cache.last_fetch - laundry_ttl_now())
        : -1.0e9;

    enum Kind { K_NONE, K_STOP, K_WEATHER, K_LAUNDRY };
    Kind pick = K_NONE;
    double best = 0;
    if (stop_overdue >= 0) { pick = K_STOP; best = stop_overdue; }
    if (weather_allowed && w_overdue >= 0 && w_overdue > best) {
        pick = K_WEATHER; best = w_overdue;
    }
    if (ha_allowed && l_overdue >= 0 && l_overdue > best) {
        pick = K_LAUNDRY; best = l_overdue;
    }

    if (pick == K_STOP) { refresh_stop(); return true; }
    if (pick == K_WEATHER) { refresh_weather(desired_day); return true; }
    if (pick == K_LAUNDRY) { refresh_laundry(); return true; }
    return false;
}

// ---------- rendering ----------

struct Fonts {
    Font title;
    Font dir;
    Font row;
    Font badge_1;
};

static int text_width(const Font &f, const std::string &s) {
    int w = 0;
    for (unsigned char c : s) w += f.CharacterWidth((uint32_t)c);
    return w;
}

static void draw_text_top(Canvas *c, const Font &f, int x, int top_y,
                          const Color &color, const std::string &text) {
    DrawText(c, f, x, top_y + f.baseline(), color, text.c_str());
}

static void draw_text_centered(Canvas *c, const Font &f, int cx, int cy,
                               const Color &color, const std::string &text) {
    int w = text_width(f, text);
    int top = cy - f.height() / 2;
    DrawText(c, f, cx - w / 2, top + f.baseline(), color, text.c_str());
}

static void fill_rounded_square(Canvas *canvas, int x0, int y0, int size,
                                int radius, const Color &c) {
    float left = radius - 0.5f;
    float right = (float)size - radius - 0.5f;
    float top = radius - 0.5f;
    float bot = (float)size - radius - 0.5f;
    float rr = (float)radius * radius;
    for (int dy = 0; dy < size; ++dy) {
        float cy = (dy < radius) ? top
                                  : (dy >= size - radius ? bot : (float)dy);
        for (int dx = 0; dx < size; ++dx) {
            float cx = (dx < radius) ? left
                                      : (dx >= size - radius ? right : (float)dx);
            float ex = dx - cx;
            float ey = dy - cy;
            if (ex * ex + ey * ey < rr) {
                canvas->SetPixel(x0 + dx, y0 + dy, c.r, c.g, c.b);
            }
        }
    }
}

static void draw_icon(Canvas *c, const XbmIcon &icon, int x0, int y0,
                      const Color &color) {
    for (int y = 0; y < icon.height; ++y) {
        for (int x = 0; x < icon.width; ++x) {
            if (icon.pixels[y * icon.width + x]) {
                c->SetPixel(x0 + x, y0 + y, color.r, color.g, color.b);
            }
        }
    }
}

static void fill_rect(Canvas *c, int x0, int y0, int x1, int y1,
                      const Color &color) {
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            c->SetPixel(x, y, color.r, color.g, color.b);
        }
    }
}

static void draw_progress_bar(Canvas *c, int x0, int y0, int x1, int y1,
                              double frac, const Color &color) {
    fill_rect(c, x0, y0, x1, y1, DIM);
    if (frac <= 0) return;
    if (frac > 1.0) frac = 1.0;
    int fill_x = x0 + (int)std::round((x1 - x0) * frac);
    if (fill_x >= x0) fill_rect(c, x0, y0, fill_x, y1, color);
}

static Color tint(const Color &c, double factor) {
    if (factor < 0) factor = 0;
    if (factor > 1) factor = 1;
    return Color((uint8_t)(c.r * factor),
                 (uint8_t)(c.g * factor),
                 (uint8_t)(c.b * factor));
}

// row_state: returns up to 2 elapsed-adjusted minutes, plus cold flag.
struct RowTimes {
    std::vector<int> minutes;
    bool cold;
};
static RowTimes row_state(const std::string &line) {
    RowTimes rt;
    rt.cold = stop_cache.cold;
    int age_min = (int)((time(nullptr) - stop_cache.last_fetch) / 60);
    for (const auto &d : stop_cache.departures) {
        if (d.line != line) continue;
        int adj = d.minutes - age_min;
        if (adj >= 1) rt.minutes.push_back(adj);
        if ((int)rt.minutes.size() >= 2) break;
    }
    return rt;
}

static void render_muni(Canvas *canvas, const Fonts &fonts) {
    canvas->Clear();
    draw_text_top(canvas, fonts.title, 2, 1, GREY, "CASTRO");
    const std::string dir_label = "East";
    int dw = text_width(fonts.dir, dir_label);
    draw_text_top(canvas, fonts.dir, 63 - dw, 2, LABEL, dir_label);
    DrawLine(canvas, 0, 10, 63, 10, DIM);

    struct R { LineDef def; RowTimes times; };
    std::vector<R> rows;
    for (const auto &ld : ROWS) rows.push_back({ld, row_state(ld.label)});
    // Sort: rows with times first, then by smallest minute.
    std::sort(rows.begin(), rows.end(), [](const R &a, const R &b) {
        bool ah = !a.times.minutes.empty();
        bool bh = !b.times.minutes.empty();
        if (ah != bh) return ah;
        if (!ah) return false;
        return a.times.minutes.front() < b.times.minutes.front();
    });

    bool any_times = false, any_cold = false;
    for (const auto &r : rows) {
        if (!r.times.minutes.empty()) any_times = true;
        if (r.times.cold) any_cold = true;
    }

    if (!any_times) {
        bool errored = any_cold && stop_cache.consecutive_failures >= FAIL_THRESHOLD;
        const char *msg = errored ? "Error" : (any_cold ? "Loading..." : "No Trains");
        const Color &c = errored ? RED : (any_cold ? LABEL : RED);
        draw_text_centered(canvas, fonts.row, 32, 38, c, msg);
        return;
    }

    for (size_t i = 0; i < rows.size() && i < 3; ++i) {
        const auto &r = rows[i];
        int y_top = 13 + (int)i * 17;
        int badge_size = 16;
        int x0 = 2, y0 = y_top;
        int cx = x0 + badge_size / 2;
        int cy = y0 + badge_size / 2;
        fill_rounded_square(canvas, x0, y0, badge_size, 8, r.def.color);
        // Single-char badge: badge_1 font (9x15B), with L offset -1.
        int dx = (r.def.label == "L") ? -1 : 0;
        draw_text_centered(canvas, fonts.badge_1, cx + dx, cy, WHITE, r.def.label);

        int x = 20;
        if (r.times.minutes.empty()) {
            draw_text_top(canvas, fonts.row, 20, y_top + 5, DIM, "--");
        } else if (r.times.minutes.size() == 1) {
            std::string s = std::to_string(r.times.minutes[0]);
            draw_text_top(canvas, fonts.row, x, y_top + 5, YELLOW, s);
            x += text_width(fonts.row, s);
            draw_text_top(canvas, fonts.row, x + 2, y_top + 5, AMBER, "min");
        } else {
            std::string first = std::to_string(r.times.minutes[0]) + ",";
            std::string second = std::to_string(r.times.minutes[1]);
            draw_text_top(canvas, fonts.row, x, y_top + 5, YELLOW, first);
            x += text_width(fonts.row, first) + 1;
            draw_text_top(canvas, fonts.row, x, y_top + 5, YELLOW, second);
            x += text_width(fonts.row, second);
            draw_text_top(canvas, fonts.row, x + 2, y_top + 5, AMBER, "min");
        }
    }
}

// Returns (remaining_minutes, fraction). remaining_minutes < 0 means avg unknown.
static void laundry_metrics(const ApplianceState &s,
                            int *remaining_m, double *frac) {
    double now = (double)time(nullptr);
    double elapsed_s = (s.started_at > 0) ? std::max(now - s.started_at, 0.0) : 0.0;
    if (s.avg_min > 0) {
        double remaining_s = std::max((double)s.avg_min * 60 - elapsed_s, 0.0);
        *remaining_m = (int)(remaining_s / 60);
        double f = elapsed_s / ((double)s.avg_min * 60);
        if (f > 1.0) f = 1.0;
        *frac = f;
    } else {
        *remaining_m = -1;
        *frac = 0.0;
    }
}

static void draw_check(Canvas *c, int x, int y, const Color &color) {
    static const int pts[7][2] = {{0,2},{1,3},{2,4},{3,3},{4,2},{5,1},{6,0}};
    for (int i = 0; i < 7; ++i) {
        c->SetPixel(x + pts[i][0], y + pts[i][1], color.r, color.g, color.b);
        c->SetPixel(x + pts[i][0], y + pts[i][1] + 1, color.r, color.g, color.b);
    }
}

static void draw_drum_spin(Canvas *c, int icon_x, int icon_y,
                           const Color &color, double phase) {
    int cx = icon_x + 16, cy = icon_y + 19;
    const double arc_len = 2.4;
    const double r_min = 1.5, r_max = 4.0;
    int rmax_int = (int)std::ceil(r_max);
    for (int dy = -rmax_int; dy <= rmax_int; ++dy) {
        for (int dx = -rmax_int; dx <= rmax_int; ++dx) {
            double d = std::hypot((double)dx, (double)dy);
            if (d < r_min || d > r_max) continue;
            double ang = std::atan2((double)dy, (double)dx);
            double back = std::fmod(phase - ang, 2 * M_PI);
            if (back < 0) back += 2 * M_PI;
            if (back > arc_len) continue;
            double falloff = 1.0 - (back / arc_len);
            double factor = 0.15 + 0.85 * falloff;
            Color t = tint(color, factor);
            c->SetPixel(cx + dx, cy + dy, t.r, t.g, t.b);
        }
    }
}

static void draw_done_state(Canvas *canvas, const Fonts &fonts,
                            const std::map<std::string, XbmIcon> &icons,
                            const std::string &title,
                            const std::string &icon_name,
                            const Color &accent) {
    double breath = 0.78 + 0.22 * std::sin(monotonic() * 2.0);
    draw_text_centered(canvas, fonts.title, 32, 6, GREY, title);
    DrawLine(canvas, 0, 11, 63, 11, DIM);

    auto it = icons.find(icon_name);
    if (it != icons.end()) draw_icon(canvas, it->second, 16, 14, accent);

    const std::string text = "DONE";
    int tw = text_width(fonts.badge_1, text);
    int cw_w = 7, gap = 3;
    int total = tw + gap + cw_w;
    int x0 = 32 - total / 2;
    draw_text_top(canvas, fonts.badge_1, x0, 49, tint(WHITE, breath), text);
    draw_check(canvas, x0 + tw + gap, 52, tint(DONE_GREEN, breath));
}

static void draw_single_appliance(Canvas *canvas, const Fonts &fonts,
                                  const std::map<std::string, XbmIcon> &icons,
                                  const std::string &title,
                                  const std::string &icon_name,
                                  const Color &accent,
                                  const ApplianceState &state) {
    int remaining_m;
    double frac;
    laundry_metrics(state, &remaining_m, &frac);

    if (remaining_m >= 0 && frac >= 1.0) {
        draw_done_state(canvas, fonts, icons, title, icon_name, accent);
        return;
    }

    draw_text_centered(canvas, fonts.title, 32, 6, GREY, title);
    DrawLine(canvas, 0, 11, 63, 11, DIM);

    auto it = icons.find(icon_name);
    if (it != icons.end()) {
        draw_icon(canvas, it->second, 2, 14, accent);
        draw_drum_spin(canvas, 2, 14, accent, monotonic() * 4.0);
    }

    int rx = 38;
    if (remaining_m >= 0) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d", remaining_m);
        draw_text_top(canvas, fonts.badge_1, rx, 14, YELLOW, buf);
        draw_text_top(canvas, fonts.dir, rx, 30, GREY, "MIN");
        draw_text_top(canvas, fonts.dir, rx, 37, GREY, "LEFT");
        int pct = (int)std::round(frac * 100);
        snprintf(buf, sizeof(buf), "%d%%", pct);
        draw_text_centered(canvas, fonts.row, 32, 51, AMBER, buf);
        draw_progress_bar(canvas, 2, 58, 61, 60, frac, accent);
    } else {
        draw_text_top(canvas, fonts.badge_1, rx, 18, YELLOW, "ON");
        draw_text_centered(canvas, fonts.row, 32, 55, LABEL, "RUNNING");
    }
}

static void render_laundry(Canvas *canvas, const Fonts &fonts,
                           const std::map<std::string, XbmIcon> &icons) {
    canvas->Clear();
    if (!laundry_cache.have) {
        draw_text_centered(canvas, fonts.row, 32, 32, LABEL, "Loading...");
        return;
    }
    bool washer_on = laundry_cache.data.washer.on;
    bool dryer_on = laundry_cache.data.dryer.on;
    if (washer_on && dryer_on) {
        int which = ((int)(time(nullptr) / LAUNDRY_ROTATE_SECONDS)) % 2;
        if (which == 0) {
            draw_single_appliance(canvas, fonts, icons, "WASHER", "washer",
                                  WASHER_COLOR, laundry_cache.data.washer);
        } else {
            draw_single_appliance(canvas, fonts, icons, "DRYER", "dryer",
                                  DRYER_COLOR, laundry_cache.data.dryer);
        }
    } else if (washer_on) {
        draw_single_appliance(canvas, fonts, icons, "WASHER", "washer",
                              WASHER_COLOR, laundry_cache.data.washer);
    } else if (dryer_on) {
        draw_single_appliance(canvas, fonts, icons, "DRYER", "dryer",
                              DRYER_COLOR, laundry_cache.data.dryer);
    }
}

static std::string month_day_for(int day_offset) {
    time_t t = time(nullptr) + day_offset * 86400;
    struct tm tm;
    localtime_r(&t, &tm);
    char buf[16];
    strftime(buf, sizeof(buf), "%b %d", &tm);
    for (char *p = buf; *p; ++p) *p = (char)std::toupper((unsigned char)*p);
    return buf;
}

// Particle overlay over the weather icon area (x=6..37, y=17..48).
// Time-based, no persistent state. Most animations are subtle/occasional;
// only rain and snow are continuous since they read as actual weather.
static void animate_weather_icon(Canvas *c, const std::string &icon_name,
                                 int code, double t) {
    auto starts_with = [&](const char *p) {
        return icon_name.rfind(p, 0) == 0;
    };

    // Continuous animations: rain & snow.
    if (starts_with("rain")) {
        static const struct { int x; double speed; double phase; } drops[] = {
            {10,22,0}, {14,18,4}, {18,24,7}, {22,20,11},
            {26,19,2}, {30,23,9}, {34,17,5}, {38,21,13}
        };
        for (const auto &d : drops) {
            double pos = std::fmod(t * d.speed + d.phase * 13, 22);
            int y = 30 + (int)pos;
            for (int dy = 0; dy < 3; ++dy) {
                int yy = y - dy;
                if (yy < 30 || yy > 50) continue;
                int b = 220 - dy * 80;
                c->SetPixel(d.x, yy, 110*b/255, 160*b/255, 220*b/255);
            }
        }
    } else if (starts_with("snow")) {
        static const struct {
            double x0; double speed; double phase; double sway;
        } flakes[] = {
            {8,4,0,1.5}, {12,5,3,1.0}, {16,4.5,5,2.0}, {20,3.8,7,1.2},
            {24,4.2,11,1.8}, {28,3.5,2,1.0}, {32,5.2,8,1.5},
            {36,4.0,4,2.0}, {40,3.2,13,1.0}, {14,4.8,17,1.7}
        };
        for (const auto &f : flakes) {
            double pos = std::fmod(t * f.speed + f.phase * 11, 22);
            int y = 30 + (int)pos;
            int x = (int)(f.x0 + std::sin(t * 1.5 + f.phase) * f.sway);
            if (x < 6 || x > 37 || y < 30 || y > 50) continue;
            c->SetPixel(x, y, 220, 230, 240);
        }
    }
    // Occasional sun sparkles — quiet most of the time.
    else if (icon_name == "sun") {
        static const struct { int x; int y; double phase; } sparks[] = {
            {3,14,0}, {38,14,1.7}, {3,47,3.4}, {38,47,5.1},
        };
        for (const auto &s : sparks) {
            double v = std::sin(t * 0.8 + s.phase);
            if (v < 0.6) continue;  // most of the cycle: dark
            int b = (int)((v - 0.6) / 0.4 * 200);
            c->SetPixel(s.x, s.y, 255*b/255, 220*b/255, 100*b/255);
        }
    }
    // Wind: gentle horizontal streaks drifting across.
    else if (icon_name == "wind") {
        static const struct { int y; double speed; double phase; int len; } streaks[] = {
            {22, 14, 0, 5},
            {28, 11, 3, 4},
            {34, 13, 7, 5},
            {40, 12, 11, 4},
        };
        for (const auto &s : streaks) {
            double pos = std::fmod(t * s.speed + s.phase * 9, 50);
            int x_head = -10 + (int)pos;
            for (int i = 0; i < s.len; ++i) {
                int x = x_head - i;
                if (x < 6 || x > 37) continue;
                int b = 200 - i * 30;
                if (b < 40) b = 40;
                c->SetPixel(x, s.y, 200*b/255, 220*b/255, 235*b/255);
            }
        }
    }
    // Fog (codes 45/48): slow horizontal wisps, ghostly.
    else if (code == 45 || code == 48) {
        for (int row = 0; row < 3; ++row) {
            int y = 22 + row * 8;
            double phase = row * 1.7;
            for (int dx = 0; dx < 32; ++dx) {
                double v = std::sin((dx + t * 4.0 + phase) * 0.4);
                if (v < 0.3) continue;
                int b = (int)((v - 0.3) / 0.7 * 90);
                c->SetPixel(6 + dx, y, 130*b/255, 140*b/255, 150*b/255);
            }
        }
    }
    // Lightning: occasional bright flash overlaid on icon area.
    else if (icon_name.find("lightning") != std::string::npos) {
        // ~6s cycle; flash for ~150ms.
        double cycle = std::fmod(t, 6.0);
        if (cycle < 0.15) {
            int b = (int)((1.0 - cycle / 0.15) * 200);
            for (int y = 17; y < 30; ++y) {
                for (int dx = 0; dx < 32; dx += 2) {
                    c->SetPixel(6 + dx, y, 255*b/255, 245*b/255, 200*b/255);
                }
            }
        }
    }
    // Cloud or partly-cloudy: occasional brief gust.
    else if (icon_name.find("cloud") != std::string::npos) {
        // ~10s cycle; gust visible for ~2s.
        double cycle = std::fmod(t, 10.0);
        if (cycle < 2.0) {
            // Two short streaks drifting right.
            static const int ys[2] = {38, 44};
            for (int i = 0; i < 2; ++i) {
                int x_head = 4 + (int)(cycle * 18) + i * 6;
                int len = 4;
                for (int k = 0; k < len; ++k) {
                    int x = x_head - k;
                    if (x < 6 || x > 37) continue;
                    double fade = std::sin((cycle / 2.0) * M_PI);  // ease in/out
                    int b = (int)((180 - k * 40) * fade);
                    if (b < 0) b = 0;
                    c->SetPixel(x, ys[i], 180*b/255, 195*b/255, 215*b/255);
                }
            }
        }
    }
    // Moon: tiny stars that twinkle on/off.
    else if (icon_name == "moon") {
        static const struct { int x; int y; double phase; } stars[] = {
            {4,18,0}, {38,22,2.1}, {7,46,4.2}, {35,44,1.0}, {32,17,3.3}
        };
        for (const auto &s : stars) {
            double v = std::sin(t * 0.7 + s.phase);
            if (v < 0.7) continue;
            int b = (int)((v - 0.7) / 0.3 * 220);
            c->SetPixel(s.x, s.y, 230*b/255, 230*b/255, 255*b/255);
        }
    }
}

static void render_weather(Canvas *canvas, const Fonts &fonts,
                           const std::map<std::string, XbmIcon> &icons) {
    canvas->Clear();
    int day = weather_cache.have ? weather_cache.day_index : 0;
    std::string header = month_day_for(day < 0 ? 0 : day);
    draw_text_centered(canvas, fonts.title, 32, 6, GREY, header);
    DrawLine(canvas, 0, 11, 63, 11, DIM);

    if (!weather_cache.have) {
        bool errored = weather_cache.consecutive_failures >= FAIL_THRESHOLD;
        const char *msg = errored ? "Error" : "Loading...";
        const Color &c = errored ? RED : LABEL;
        draw_text_centered(canvas, fonts.row, 32, 38, c, msg);
        return;
    }

    int code = weather_cache.data.code;
    auto icon_it = CODE_ICON.find(code);
    std::string icon_name = icon_it != CODE_ICON.end() ? icon_it->second : "cloud";
    auto px_it = icons.find(icon_name);
    if (px_it == icons.end()) px_it = icons.find("cloud");
    if (px_it != icons.end()) {
        draw_icon(canvas, px_it->second, 6, 17, ICON_COLOR);
    }
    animate_weather_icon(canvas, icon_name, code, monotonic());

    auto word_it = CODE_WORD.find(code);
    std::string word = word_it != CODE_WORD.end() ? word_it->second : "";

    int rx = 41;
    char tmp[16];
    snprintf(tmp, sizeof(tmp), "%d\xc2\xb0", weather_cache.data.hi);
    draw_text_top(canvas, fonts.title, rx, 17, YELLOW, tmp);
    snprintf(tmp, sizeof(tmp), "%d\xc2\xb0", weather_cache.data.lo);
    draw_text_top(canvas, fonts.row, rx, 29, LABEL, tmp);
    if (weather_cache.data.precip < 10) {
        snprintf(tmp, sizeof(tmp), "%dmph", weather_cache.data.wind_mph);
        draw_text_top(canvas, fonts.dir, rx, 40, PRECIP_BLUE, tmp);
    } else {
        snprintf(tmp, sizeof(tmp), "%d%%", weather_cache.data.precip);
        draw_text_top(canvas, fonts.row, rx, 39, PRECIP_BLUE, tmp);
    }

    for (char &c : word) c = (char)std::toupper((unsigned char)c);
    draw_text_centered(canvas, fonts.row, 32, 55, LABEL, word);
}

// ---------- boot grace ----------

static void boot_grace() {
    FILE *f = fopen("/proc/uptime", "r");
    if (!f) return;
    double up = 0;
    if (fscanf(f, "%lf", &up) != 1) up = 0;
    fclose(f);
    double delay = 90.0 - up;
    if (delay > 0) {
        log_msg("boot grace: sleeping %.0fs before matrix init", delay);
        usleep((useconds_t)(delay * 1e6));
    } else {
        log_msg("boot grace: uptime %.0fs already past, no sleep", up);
    }
}

// ---------- main ----------

int main(int, char**) {
    boot_grace();

    http::global_init();

    if (const char *u = getenv("HA_URL")) ha_url = u;
    if (const char *t = getenv("HA_TOKEN")) ha_token = t;
    ha_enabled = !ha_url.empty() && !ha_token.empty();
    log_msg("home assistant: %s", ha_enabled ? "enabled" : "disabled");

    Fonts fonts;
    if (!fonts.title.LoadFont("fonts/6x10.bdf") ||
        !fonts.dir.LoadFont("fonts/4x6.bdf") ||
        !fonts.row.LoadFont("fonts/5x7.bdf") ||
        !fonts.badge_1.LoadFont("fonts/9x15B.bdf")) {
        fprintf(stderr, "font load failed\n");
        return 1;
    }

    std::map<std::string, XbmIcon> icons;
    for (const auto &kv : CODE_ICON) {
        const std::string &name = kv.second;
        if (icons.count(name)) continue;
        XbmIcon ic;
        std::string p = "icons/" + name + ".xbm";
        if (!load_xbm(p, &ic)) {
            log_msg("icon FAILED: %s", p.c_str());
            continue;
        }
        icons[name] = std::move(ic);
    }
    log_msg("loaded %zu weather icons", icons.size());

    if (ha_enabled) {
        XbmIcon ic;
        if (load_xbm("icons/washer.xbm", &ic)) icons["washer"] = std::move(ic);
        else log_msg("icon FAILED: icons/washer.xbm");
        if (load_xbm("icons/dryer.xbm", &ic)) icons["dryer"] = std::move(ic);
        else log_msg("icon FAILED: icons/dryer.xbm");
    }

    RGBMatrix::Options options;
    options.rows = 64;
    options.cols = 64;
    options.chain_length = 1;
    options.parallel = 1;
    options.hardware_mapping = "regular";
    options.brightness = FULL_BRIGHTNESS;
    options.pwm_dither_bits = 1;

    rgb_matrix::RuntimeOptions runtime;
    runtime.gpio_slowdown = 1;

    RGBMatrix *matrix = RGBMatrix::CreateFromOptions(options, runtime);
    if (!matrix) {
        fprintf(stderr, "matrix init failed\n");
        return 1;
    }
    signal(SIGTERM, on_signal);
    signal(SIGINT, on_signal);

    FrameCanvas *canvas = matrix->CreateFrameCanvas();
    log_msg("matrix ready, entering loop");

    double last_render = 0;
    int frame = 0;
    while (!interrupted) {
        // Render first so the Loading state shows immediately, then fetch.
        matrix->SetBrightness(is_dim() ? DIM_BRIGHTNESS : FULL_BRIGHTNESS);
        bool laundry = laundry_active();
        bool night = is_night();
        if (laundry) {
            render_laundry(canvas, fonts, icons);
        } else if (night) {
            render_weather(canvas, fonts, icons);
        } else {
            render_muni(canvas, fonts);
        }
        canvas = matrix->SwapOnVSync(canvas);
        log_msg("render frame=%d %s",
                frame++,
                laundry ? "laundry" : (night ? "weather" : "muni"));

        // One fetch tick if something is overdue (blocks on slow networks).
        tick_fetcher();

        // Pace render: laundry & night animate at ~5fps; day = 10s.
        double wait = (laundry || night) ? 0.2 : 10.0;
        last_render = monotonic();
        while (!interrupted && (monotonic() - last_render) < wait) {
            usleep(200000);  // 200ms — let signals interrupt promptly
        }
    }

    matrix->Clear();
    delete matrix;
    http::global_cleanup();
    return 0;
}
