#include "net/home_assistant.hpp"
#include "net/http.hpp"

#include <nlohmann/json.hpp>
#include <ctime>
#include <cstring>

using nlohmann::json;

// ISO 8601 (with optional ms and tz) -> unix seconds.
static double parse_iso8601(const std::string &s) {
    if (s.empty()) return 0.0;
    struct tm tm = {};
    const char *p = strptime(s.c_str(), "%Y-%m-%dT%H:%M:%S", &tm);
    if (!p) return 0.0;
    // Optional fractional seconds
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') ++p;
    }
    int offset = 0;
    if (*p == '+' || *p == '-') {
        int sign = (*p == '+') ? 1 : -1;
        int hh = 0, mm = 0;
        sscanf(p + 1, "%d:%d", &hh, &mm);
        offset = sign * (hh * 3600 + mm * 60);
    }
    return (double)(timegm(&tm) - offset);
}

static int parse_avg_min(const json &state) {
    if (!state.contains("state")) return 0;
    const auto &s = state["state"];
    if (!s.is_string()) return 0;
    try {
        double v = std::stod(s.get<std::string>());
        return (int)(v + 0.5);
    } catch (...) {
        return 0;
    }
}

static bool fetch_state(const std::string &base_url, const std::string &token,
                        const std::string &entity_id,
                        long connect_timeout_s, long read_timeout_s,
                        json *out, std::string *error) {
    // Strip trailing slash from base.
    std::string base = base_url;
    while (!base.empty() && base.back() == '/') base.pop_back();
    std::string url = base + "/api/states/" + entity_id;
    std::string body;
    if (!http::get_bearer(url, token, connect_timeout_s, read_timeout_s,
                          &body, error)) {
        return false;
    }
    try {
        *out = json::parse(body);
        return true;
    } catch (const std::exception &e) {
        if (error) *error = std::string("parse: ") + e.what();
        return false;
    }
}

bool ha_fetch_laundry(const std::string &base_url,
                      const std::string &token,
                      long connect_timeout_s,
                      long read_timeout_s,
                      LaundryData *out,
                      std::string *error) {
    json washer_run, dryer_run, washer_avg, dryer_avg;
    if (!fetch_state(base_url, token, "binary_sensor.washer_running",
                     connect_timeout_s, read_timeout_s,
                     &washer_run, error)) return false;
    if (!fetch_state(base_url, token, "binary_sensor.dryer_running",
                     connect_timeout_s, read_timeout_s,
                     &dryer_run, error)) return false;
    // Averages are optional; soft-fail.
    bool have_washer_avg = fetch_state(base_url, token,
                                       "sensor.washer_avg_run_minutes",
                                       connect_timeout_s, read_timeout_s,
                                       &washer_avg, nullptr);
    bool have_dryer_avg = fetch_state(base_url, token,
                                      "sensor.dryer_avg_run_minutes",
                                      connect_timeout_s, read_timeout_s,
                                      &dryer_avg, nullptr);

    auto fill = [&](const json &run, const json &avg, bool have_avg,
                    ApplianceState *s) {
        s->on = run.value("state", "") == "on";
        s->started_at = s->on ? parse_iso8601(run.value("last_changed", "")) : 0.0;
        s->avg_min = have_avg ? parse_avg_min(avg) : 0;
    };

    fill(washer_run, washer_avg, have_washer_avg, &out->washer);
    fill(dryer_run, dryer_avg, have_dryer_avg, &out->dryer);
    out->fetched_at = (double)time(nullptr);
    return true;
}
