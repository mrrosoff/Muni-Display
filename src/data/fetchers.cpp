#include "data/fetchers.hpp"

#include "data/caches.hpp"
#include "core/config.hpp"
#include "net/home_assistant.hpp"
#include "net/http.hpp"
#include "core/log.hpp"
#include "util/time_utils.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

using namespace std;
using logx::log;
using nlohmann::json;

namespace fetch {

namespace {

const unordered_set<string_view> STOP_LINES = {"K", "L", "M"};

// Single persistent curl handle for all fetcher-thread requests. Reuses TCP
// keep-alive, TLS session, and DNS cache across calls — the TLS handshake
// dominates on a Pi Zero, so this is the big win.
http::Session &fetch_session() {
    static http::Session s;
    return s;
}

constexpr const char *MUNI_URL_TPL =
    "https://api.511.org/transit/StopMonitoring?"
    "api_key=%s&agency=%s&stopCode=%s&format=json";

constexpr const char *WEATHER_URL =
    "https://api.open-meteo.com/v1/forecast?"
    "latitude=37.7605&longitude=-122.4356&"
    "daily=weather_code,temperature_2m_max,temperature_2m_min,"
    "precipitation_probability_max,wind_speed_10m_max,sunrise,sunset&"
    "temperature_unit=fahrenheit&"
    "wind_speed_unit=mph&"
    "timezone=America%2FLos_Angeles&"
    "forecast_days=2";

void strip_utf8_bom(string &s) {
    if (s.size() >= 3 && static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF) {
        s.erase(0, 3);
    }
}

bool same_local_day(double unix_a, double unix_b) {
    time_t ta = static_cast<time_t>(unix_a);
    time_t tb = static_cast<time_t>(unix_b);
    struct tm tma{}, tmb{};
    localtime_r(&ta, &tma);
    localtime_r(&tb, &tmb);
    return tma.tm_year == tmb.tm_year && tma.tm_yday == tmb.tm_yday;
}

// Caller must hold caches::mtx.
long laundry_ttl_now_locked() {
    using namespace cfg;
    if (caches::laundry.have &&
        (caches::laundry.data.washer.on || caches::laundry.data.dryer.on)) {
        return LAUNDRY_ACTIVE_TTL.count();
    }
    if (caches::laundry.washer_done_at > 0) {
        return LAUNDRY_ACTIVE_TTL.count();
    }
    return LAUNDRY_TTL.count();
}

// Bumps last_fetch backward so the next overdue check waits RETRY_TTL, not the
// full TTL, before retrying.
void schedule_retry(double &last_fetch, long full_ttl, long retry_ttl) {
    last_fetch = static_cast<double>(tu::now_unix()) - (full_ttl - retry_ttl);
}

// Server responded with a non-2xx status, or we got a body we couldn't parse
// — connection is fine, no reason to recycle. Anything else (timeout, RST,
// TLS error, "Connection died") is a transport-level fault and the curl
// handle's pooled connections may be wedged; recycle so the next poll dials
// fresh.
bool is_transport_error(const string &err) {
    return err.rfind("HTTP ", 0) != 0 && err.rfind("parse:", 0) != 0;
}

}  // namespace

bool laundry_active() {
    if (!caches::ha_enabled) return false;
    lock_guard lg(caches::mtx);
    if (!caches::laundry.have) return false;
    if (caches::laundry.data.washer.on || caches::laundry.data.dryer.on) return true;
    // Keep showing laundry screen while washer is done but dryer hasn't started,
    // until day rolls over (user went to bed).
    const double wda = caches::laundry.washer_done_at;
    if (wda > 0 && same_local_day(wda, static_cast<double>(tu::now_unix()))) return true;
    return false;
}

bool refresh_stop() {
    const char *api_key = getenv("MUNI_API_KEY");
    if (!api_key || !*api_key) {
        log("stop: MUNI_API_KEY not set");
        return false;
    }
    char url[512];
    snprintf(
        url,
        sizeof(url),
        MUNI_URL_TPL,
        api_key,
        string(cfg::AGENCY).c_str(),
        string(cfg::STOP_CODE).c_str()
    );

    const double t0 = tu::monotonic();
    string body, err;
    if (!fetch_session().get(
            url, cfg::CONNECT_TIMEOUT_S, cfg::READ_TIMEOUT_S, &body, &err
        )) {
        if (is_transport_error(err)) fetch_session().reset();
        lock_guard lg(caches::mtx);
        auto &c = caches::stop;
        c.consecutive_failures++;
        // Tag 5xx as an upstream-server issue so the UI can surface it.
        // Network failures from our side stay un-tagged.
        c.upstream_5xx = err.rfind("HTTP 5", 0) == 0;
        schedule_retry(c.last_fetch, cfg::STOP_TTL.count(), cfg::STOP_RETRY_TTL.count());
        log("stop FAILED in ",
            tu::monotonic() - t0,
            "s (#",
            c.consecutive_failures,
            "): ",
            err);
        return false;
    }
    strip_utf8_bom(body);

    try {
        const auto data = json::parse(body);
        const auto &delivery = data.at("ServiceDelivery");
        const auto ts = delivery.value("ResponseTimestamp", "");
        const auto now_server = ts.empty() ? tu::now_unix() : tu::parse_iso8601(ts);

        vector<Departure> deps;
        if (auto smd_it = delivery.find("StopMonitoringDelivery");
            smd_it != delivery.end()) {
            if (auto visits_it = smd_it->find("MonitoredStopVisit");
                visits_it != smd_it->end() && visits_it->is_array()) {
                for (const auto &visit : *visits_it) {
                    auto journey_it = visit.find("MonitoredVehicleJourney");
                    if (journey_it == visit.end()) continue;
                    const auto line = journey_it->value("LineRef", "");
                    if (!STOP_LINES.count(line)) continue;
                    auto call_it = journey_it->find("MonitoredCall");
                    if (call_it == journey_it->end()) continue;
                    auto arr = call_it->value("ExpectedArrivalTime", "");
                    if (arr.empty()) arr = call_it->value("AimedArrivalTime", "");
                    if (arr.empty()) continue;
                    const auto arrival = tu::parse_iso8601(arr);
                    const int minutes = static_cast<int>((arrival - now_server) / 60);
                    if (minutes < 0) continue;
                    deps.push_back({line, minutes});
                }
            }
        }
        sort(deps.begin(), deps.end(), [](const Departure &a, const Departure &b) {
            return a.minutes < b.minutes;
        });

        const auto count = deps.size();
        {
            lock_guard lg(caches::mtx);
            auto &c = caches::stop;
            c.departures = move(deps);
            c.last_fetch = static_cast<double>(tu::now_unix());
            c.cold = false;
            c.consecutive_failures = 0;
            c.upstream_5xx = false;
        }
        log("stop ", cfg::STOP_CODE, ": ", tu::monotonic() - t0, "s (", count, " deps)");
        return true;
    } catch (const exception &e) {
        lock_guard lg(caches::mtx);
        auto &c = caches::stop;
        c.consecutive_failures++;
        schedule_retry(c.last_fetch, cfg::STOP_TTL.count(), cfg::STOP_RETRY_TTL.count());
        log("stop parse FAILED in ",
            tu::monotonic() - t0,
            "s (#",
            c.consecutive_failures,
            "): ",
            e.what());
        return false;
    }
}

bool refresh_weather(int day_index) {
    const double t0 = tu::monotonic();
    string body, err;
    if (!fetch_session().get(
            WEATHER_URL, cfg::CONNECT_TIMEOUT_S, cfg::READ_TIMEOUT_S, &body, &err
        )) {
        if (is_transport_error(err)) fetch_session().reset();
        lock_guard lg(caches::mtx);
        auto &c = caches::weather;
        c.consecutive_failures++;
        schedule_retry(
            c.last_fetch, cfg::WEATHER_TTL.count(), cfg::WEATHER_RETRY_TTL.count()
        );
        log("weather FAILED in ",
            tu::monotonic() - t0,
            "s (#",
            c.consecutive_failures,
            "): ",
            err);
        return false;
    }
    try {
        const auto data = json::parse(body);
        const auto &d = data.at("daily");
        WeatherData wd;
        wd.code = d.at("weather_code").at(day_index).get<int>();
        wd.hi = static_cast<int>(
            round(d.at("temperature_2m_max").at(day_index).get<double>())
        );
        wd.lo = static_cast<int>(
            round(d.at("temperature_2m_min").at(day_index).get<double>())
        );
        const auto &precip = d.at("precipitation_probability_max").at(day_index);
        wd.precip = precip.is_null() ? 0 : static_cast<int>(precip.get<double>());
        const auto &wind = d.at("wind_speed_10m_max").at(day_index);
        wd.wind_mph = wind.is_null() ? 0 : static_cast<int>(round(wind.get<double>()));
        wd.day_index = day_index;

        {
            lock_guard lg(caches::mtx);
            auto &c = caches::weather;
            c.have = true;
            c.data = wd;
            c.day_index = day_index;
            c.last_fetch = static_cast<double>(tu::now_unix());
            c.consecutive_failures = 0;
        }
        log("weather day=", day_index, ": ", tu::monotonic() - t0, "s");
        return true;
    } catch (const exception &e) {
        lock_guard lg(caches::mtx);
        auto &c = caches::weather;
        c.consecutive_failures++;
        schedule_retry(
            c.last_fetch, cfg::WEATHER_TTL.count(), cfg::WEATHER_RETRY_TTL.count()
        );
        log("weather parse FAILED in ",
            tu::monotonic() - t0,
            "s (#",
            c.consecutive_failures,
            "): ",
            e.what());
        return false;
    }
}

bool refresh_laundry() {
    if (!caches::ha_enabled) return false;
    const double t0 = tu::monotonic();
    LaundryData data;
    string err;
    const HaClient client(
        fetch_session(),
        caches::ha_url,
        caches::ha_token,
        cfg::CONNECT_TIMEOUT_S,
        cfg::READ_TIMEOUT_S
    );
    if (!client.fetch_laundry(&data, &err)) {
        if (is_transport_error(err)) fetch_session().reset();
        lock_guard lg(caches::mtx);
        auto &c = caches::laundry;
        c.consecutive_failures++;
        schedule_retry(
            c.last_fetch, cfg::LAUNDRY_TTL.count(), cfg::LAUNDRY_RETRY_TTL.count()
        );
        log("laundry FAILED in ",
            tu::monotonic() - t0,
            "s (#",
            c.consecutive_failures,
            "): ",
            err);
        return false;
    }
    {
        lock_guard lg(caches::mtx);
        auto &c = caches::laundry;
        const bool was_washer_on = c.have && c.data.washer.on;
        const bool was_dryer_on = c.have && c.data.dryer.on;
        // Dryer starting clears the pending-transfer state.
        if (!was_dryer_on && data.dryer.on) c.washer_done_at = 0.0;
        // Washer finishing while dryer is off starts the pending-transfer state.
        if (was_washer_on && !data.washer.on && !data.dryer.on) {
            c.washer_done_at = static_cast<double>(tu::now_unix());
        }
        // Washer turning back on (unlikely but defensive) clears state.
        if (data.washer.on) c.washer_done_at = 0.0;
        c.have = true;
        c.data = data;
        c.last_fetch = static_cast<double>(tu::now_unix());
        c.consecutive_failures = 0;
    }
    log("laundry: ",
        tu::monotonic() - t0,
        "s (washer=",
        static_cast<int>(data.washer.on),
        " dryer=",
        static_cast<int>(data.dryer.on),
        ")");
    return true;
}

bool tick() {
    const double now = static_cast<double>(tu::now_unix());
    const bool night = tu::is_night();
    const int desired_day = tu::desired_day_index();

    // Snapshot under lock; release before deciding which source to fetch.
    double stop_lf, weather_lf, laundry_lf;
    bool stops_warm, weather_warm;
    int weather_day;
    long laundry_ttl_eff;
    {
        lock_guard lg(caches::mtx);
        stop_lf = caches::stop.last_fetch;
        stops_warm = !caches::stop.cold;
        weather_warm = caches::weather.have;
        weather_lf = caches::weather.last_fetch;
        weather_day = caches::weather.day_index;
        laundry_lf = caches::laundry.last_fetch;
        laundry_ttl_eff = laundry_ttl_now_locked();
    }

    const bool day_changed = weather_warm && weather_day != desired_day;
    const bool weather_allowed = night || day_changed;
    const bool primary_warm = night ? weather_warm : stops_warm;
    const bool ha_allowed = caches::ha_enabled && primary_warm;

    // Track the most-overdue eligible source; ties broken by listing order.
    double best_overdue = 0;
    function<bool()> best_action;
    const auto consider = [&](bool eligible, double overdue, function<bool()> run) {
        if (!eligible || overdue < 0 || overdue <= best_overdue) return;
        best_overdue = overdue;
        best_action = move(run);
    };

    consider(true, now - stop_lf - cfg::STOP_TTL.count(), refresh_stop);
    consider(weather_allowed, now - weather_lf - cfg::WEATHER_TTL.count(), [desired_day] {
        return refresh_weather(desired_day);
    });
    consider(ha_allowed, now - laundry_lf - laundry_ttl_eff, refresh_laundry);

    if (!best_action) return false;
    best_action();
    return true;
}

}  // namespace fetch
