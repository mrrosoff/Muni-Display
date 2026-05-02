#include "data/fetchers.hpp"

#include "data/caches.hpp"
#include "core/config.hpp"
#include "net/http.hpp"
#include "core/log.hpp"
#include "util/time_utils.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_set>

using nlohmann::json;
using logx::log;

namespace fetch {

namespace {

const std::unordered_set<std::string_view> STOP_LINES = {"K", "L", "M"};

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

void strip_utf8_bom(std::string &s) {
    if (s.size() >= 3 &&
        static_cast<unsigned char>(s[0]) == 0xEF &&
        static_cast<unsigned char>(s[1]) == 0xBB &&
        static_cast<unsigned char>(s[2]) == 0xBF) {
        s.erase(0, 3);
    }
}

// Caller must hold caches::mtx.
long laundry_ttl_now_locked() {
    using namespace cfg;
    if (caches::laundry.have &&
        (caches::laundry.data.washer.on || caches::laundry.data.dryer.on)) {
        return LAUNDRY_ACTIVE_TTL.count();
    }
    return LAUNDRY_TTL.count();
}

}  // namespace

bool laundry_active() {
    if (!caches::ha_enabled) return false;
    std::lock_guard lg(caches::mtx);
    if (!caches::laundry.have) return false;
    return caches::laundry.data.washer.on || caches::laundry.data.dryer.on;
}

bool refresh_stop() {
    const char *api_key = std::getenv("MUNI_API_KEY");
    if (!api_key || !*api_key) {
        log("stop: MUNI_API_KEY not set");
        return false;
    }
    char url[512];
    std::snprintf(url, sizeof(url), MUNI_URL_TPL,
                  api_key,
                  std::string(cfg::AGENCY).c_str(),
                  std::string(cfg::STOP_CODE).c_str());

    const double t0 = tu::monotonic();
    std::string body, err;
    if (!http::get(url, cfg::CONNECT_TIMEOUT_S, cfg::READ_TIMEOUT_S, &body, &err)) {
        std::lock_guard lg(caches::mtx);
        auto &c = caches::stop;
        c.consecutive_failures++;
        c.last_fetch = static_cast<double>(tu::now_unix())
                     - (cfg::STOP_TTL.count() - cfg::STOP_RETRY_TTL.count());
        log("stop FAILED in ", tu::monotonic() - t0,
            "s (#", c.consecutive_failures, "): ", err);
        return false;
    }
    strip_utf8_bom(body);

    try {
        const auto data = json::parse(body);
        const auto &delivery = data.at("ServiceDelivery");
        const auto ts = delivery.value("ResponseTimestamp", "");
        const auto now_server = ts.empty() ? tu::now_unix() : tu::parse_iso8601(ts);

        std::vector<Departure> deps;
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
                    if (minutes < 1) continue;
                    deps.push_back({line, minutes});
                }
            }
        }
        std::sort(deps.begin(), deps.end(),
                  [](const Departure &a, const Departure &b) {
                      return a.minutes < b.minutes;
                  });

        const auto count = deps.size();
        {
            std::lock_guard lg(caches::mtx);
            auto &c = caches::stop;
            c.departures = std::move(deps);
            c.last_fetch = static_cast<double>(tu::now_unix());
            c.cold = false;
            c.consecutive_failures = 0;
        }
        log("stop ", cfg::STOP_CODE, ": ", tu::monotonic() - t0, "s (", count, " deps)");
        return true;
    } catch (const std::exception &e) {
        std::lock_guard lg(caches::mtx);
        auto &c = caches::stop;
        c.consecutive_failures++;
        c.last_fetch = static_cast<double>(tu::now_unix())
                     - (cfg::STOP_TTL.count() - cfg::STOP_RETRY_TTL.count());
        log("stop parse FAILED in ", tu::monotonic() - t0,
            "s (#", c.consecutive_failures, "): ", e.what());
        return false;
    }
}

bool refresh_weather(int day_index) {
    const double t0 = tu::monotonic();
    std::string body, err;
    if (!http::get(WEATHER_URL, cfg::CONNECT_TIMEOUT_S, cfg::READ_TIMEOUT_S,
                   &body, &err)) {
        std::lock_guard lg(caches::mtx);
        auto &c = caches::weather;
        c.consecutive_failures++;
        c.last_fetch = static_cast<double>(tu::now_unix())
                     - (cfg::WEATHER_TTL.count() - cfg::WEATHER_RETRY_TTL.count());
        log("weather FAILED in ", tu::monotonic() - t0,
            "s (#", c.consecutive_failures, "): ", err);
        return false;
    }
    try {
        const auto data = json::parse(body);
        const auto &d = data.at("daily");
        WeatherData wd;
        wd.code = d.at("weather_code").at(day_index).get<int>();
        wd.hi = static_cast<int>(std::round(
            d.at("temperature_2m_max").at(day_index).get<double>()));
        wd.lo = static_cast<int>(std::round(
            d.at("temperature_2m_min").at(day_index).get<double>()));
        const auto &precip = d.at("precipitation_probability_max").at(day_index);
        wd.precip = precip.is_null() ? 0 : static_cast<int>(precip.get<double>());
        const auto &wind = d.at("wind_speed_10m_max").at(day_index);
        wd.wind_mph = wind.is_null()
            ? 0 : static_cast<int>(std::round(wind.get<double>()));
        wd.day_index = day_index;

        {
            std::lock_guard lg(caches::mtx);
            auto &c = caches::weather;
            c.have = true;
            c.data = wd;
            c.day_index = day_index;
            c.last_fetch = static_cast<double>(tu::now_unix());
            c.consecutive_failures = 0;
        }
        log("weather day=", day_index, ": ", tu::monotonic() - t0, "s");
        return true;
    } catch (const std::exception &e) {
        std::lock_guard lg(caches::mtx);
        auto &c = caches::weather;
        c.consecutive_failures++;
        c.last_fetch = static_cast<double>(tu::now_unix())
                     - (cfg::WEATHER_TTL.count() - cfg::WEATHER_RETRY_TTL.count());
        log("weather parse FAILED in ", tu::monotonic() - t0,
            "s (#", c.consecutive_failures, "): ", e.what());
        return false;
    }
}

bool refresh_laundry() {
    if (!caches::ha_enabled) return false;
    const double t0 = tu::monotonic();
    LaundryData data;
    std::string err;
    if (!ha_fetch_laundry(caches::ha_url, caches::ha_token,
                          cfg::CONNECT_TIMEOUT_S, cfg::READ_TIMEOUT_S,
                          &data, &err)) {
        std::lock_guard lg(caches::mtx);
        auto &c = caches::laundry;
        c.consecutive_failures++;
        c.last_fetch = static_cast<double>(tu::now_unix())
                     - (cfg::LAUNDRY_TTL.count() - cfg::LAUNDRY_RETRY_TTL.count());
        log("laundry FAILED in ", tu::monotonic() - t0,
            "s (#", c.consecutive_failures, "): ", err);
        return false;
    }
    {
        std::lock_guard lg(caches::mtx);
        auto &c = caches::laundry;
        c.have = true;
        c.data = data;
        c.last_fetch = static_cast<double>(tu::now_unix());
        c.consecutive_failures = 0;
    }
    log("laundry: ", tu::monotonic() - t0,
        "s (washer=", static_cast<int>(data.washer.on),
        " dryer=", static_cast<int>(data.dryer.on), ")");
    return true;
}

bool tick() {
    const double now = static_cast<double>(tu::now_unix());
    const bool night = tu::is_night();
    const int desired_day = tu::desired_day_index();

    // Snapshot last_fetch values + state under lock; release before deciding.
    double stop_lf, weather_lf, laundry_lf;
    bool stops_warm, weather_warm;
    int weather_day;
    long laundry_ttl_eff;
    {
        std::lock_guard lg(caches::mtx);
        stop_lf = caches::stop.last_fetch;
        stops_warm = !caches::stop.cold;
        weather_warm = caches::weather.have;
        weather_lf = caches::weather.last_fetch;
        weather_day = caches::weather.day_index;
        laundry_lf = caches::laundry.last_fetch;
        laundry_ttl_eff = laundry_ttl_now_locked();
    }

    const double stop_overdue = now - stop_lf - cfg::STOP_TTL.count();
    const double w_overdue = now - weather_lf - cfg::WEATHER_TTL.count();
    const bool day_changed = weather_warm && weather_day != desired_day;
    const bool weather_allowed = night || day_changed;
    const bool primary_warm = night ? weather_warm : stops_warm;
    const bool ha_allowed = caches::ha_enabled && primary_warm;
    const double l_overdue = caches::ha_enabled
        ? (now - laundry_lf - laundry_ttl_eff)
        : -1.0e9;

    enum class Pick { None, Stop, Weather, Laundry };
    Pick pick = Pick::None;
    double best = 0;
    if (stop_overdue >= 0) { pick = Pick::Stop; best = stop_overdue; }
    if (weather_allowed && w_overdue >= 0 && w_overdue > best) {
        pick = Pick::Weather; best = w_overdue;
    }
    if (ha_allowed && l_overdue >= 0 && l_overdue > best) {
        pick = Pick::Laundry; best = l_overdue;
    }

    switch (pick) {
        case Pick::Stop:    refresh_stop();              return true;
        case Pick::Weather: refresh_weather(desired_day); return true;
        case Pick::Laundry: refresh_laundry();           return true;
        case Pick::None:    return false;
    }
    return false;
}

}  // namespace fetch
