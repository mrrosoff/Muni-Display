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

const string LAUNDRY_TEMPLATE =
    json{{"template",
          "{% set w = states.binary_sensor.washer_running %}"
          "{% set d = states.binary_sensor.dryer_running %}"
          "{{ {'washer': w.state if w else none,"
          " 'washer_since': w.last_changed.isoformat() if w else none,"
          " 'dryer': d.state if d else none,"
          " 'dryer_since': d.last_changed.isoformat() if d else none,"
          " 'washer_avg': states('sensor.washer_avg_run_minutes'),"
          " 'dryer_avg': states('sensor.dryer_avg_run_minutes')} | tojson }}"}}
        .dump();

int parse_avg_min(const json &v) {
    if (!v.is_string()) return 0;
    try {
        return static_cast<int>(lround(stod(v.get<string>())));
    } catch (...) {
        return 0;
    }
}

void fill_appliance(const json &data, const string &name, ApplianceState &out) {
    out.on = data.at(name).get<string>() == "on";
    out.started_at = out.on
                         ? static_cast<double>(
                               tu::parse_iso8601(data.at(name + "_since").get<string>())
                           )
                         : 0.0;
    out.avg_min = out.on ? parse_avg_min(data.at(name + "_avg")) : 0;
}

}  // namespace

HaClient::HaClient(
    http::Session &session,
    string base_url,
    string token,
    long connect_timeout_s,
    long read_timeout_s
)
    : session_(session),
      base_(strip_trailing_slash(move(base_url))),
      token_(move(token)),
      connect_timeout_s_(connect_timeout_s),
      read_timeout_s_(read_timeout_s) {}

bool HaClient::fetch_laundry(LaundryData *out, string *error) const {
    string body;
    if (!session_.post_json_bearer(
            base_ + "/api/template",
            token_,
            LAUNDRY_TEMPLATE,
            connect_timeout_s_,
            read_timeout_s_,
            &body,
            error
        )) {
        return false;
    }
    try {
        const auto data = json::parse(body);
        fill_appliance(data, "washer", out->washer);
        fill_appliance(data, "dryer", out->dryer);
    } catch (const exception &e) {
        if (error) *error = string("parse: ") + e.what();
        return false;
    }
    out->fetched_at = static_cast<double>(time(nullptr));
    return true;
}
