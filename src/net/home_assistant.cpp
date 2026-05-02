#include "net/home_assistant.hpp"

#include "net/http.hpp"
#include "util/time_utils.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <ctime>
#include <exception>
#include <string>
#include <string_view>
#include <utility>

using namespace std;
using nlohmann::json;

namespace {

string strip_trailing_slash(string s) {
    while (!s.empty() && s.back() == '/') s.pop_back();
    return s;
}

int parse_avg_min(const json &state) {
    if (!state.contains("state")) return 0;
    const auto &s = state["state"];
    if (!s.is_string()) return 0;
    try {
        return static_cast<int>(lround(stod(s.get<string>())));
    } catch (...) {
        return 0;
    }
}

void fill_appliance(const json &run, const json *avg, ApplianceState &out) {
    out.on = run.value("state", "") == "on";
    out.started_at =
        out.on ? static_cast<double>(tu::parse_iso8601(run.value("last_changed", "")))
               : 0.0;
    out.avg_min = avg ? parse_avg_min(*avg) : 0;
}

}  // namespace

HaClient::HaClient(
    string base_url,
    string token,
    long connect_timeout_s,
    long read_timeout_s
)
    : base_(strip_trailing_slash(move(base_url))),
      token_(move(token)),
      connect_timeout_s_(connect_timeout_s),
      read_timeout_s_(read_timeout_s) {}

bool HaClient::fetch_laundry(LaundryData *out, string *error) const {
    const auto fetch_state = [&](string_view entity_id, json *dst, string *err) {
        const string url = base_ + "/api/states/" + string(entity_id);
        string body;
        if (!http::get_bearer(
                url, token_, connect_timeout_s_, read_timeout_s_, &body, err
            )) {
            return false;
        }
        try {
            *dst = json::parse(body);
            return true;
        } catch (const exception &e) {
            if (err) *err = string("parse: ") + e.what();
            return false;
        }
    };

    json washer_run, dryer_run, washer_avg, dryer_avg;
    if (!fetch_state("binary_sensor.washer_running", &washer_run, error)) return false;
    if (!fetch_state("binary_sensor.dryer_running", &dryer_run, error)) return false;

    // Averages are optional; soft-fail.
    const bool have_w =
        fetch_state("sensor.washer_avg_run_minutes", &washer_avg, nullptr);
    const bool have_d = fetch_state("sensor.dryer_avg_run_minutes", &dryer_avg, nullptr);

    fill_appliance(washer_run, have_w ? &washer_avg : nullptr, out->washer);
    fill_appliance(dryer_run, have_d ? &dryer_avg : nullptr, out->dryer);
    out->fetched_at = static_cast<double>(time(nullptr));
    return true;
}
