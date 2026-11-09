#include "net/home_assistant.hpp"

#include "net/http.hpp"

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
          "{{ {'washer': states('sensor.washer_status'),"
          " 'washer_start': as_timestamp(w.last_changed) if w else none,"
          " 'washer_expected': states('input_number.washer_expected_min'),"
          " 'dryer': states('sensor.dryer_status'),"
          " 'dryer_start': state_attr('input_datetime.dryer_cycle_start', 'timestamp'),"
          " 'dryer_expected': states('input_number.dryer_expected_min')} | tojson }}"}}
        .dump();

ApplianceStatus parse_status(const string &s) {
    if (s == "running") return ApplianceStatus::Running;
    if (s == "done") return ApplianceStatus::Done;
    return ApplianceStatus::Idle;
}

int parse_minutes(const json &v) {
    if (!v.is_string()) return 0;
    try {
        return static_cast<int>(lround(stod(v.get<string>())));
    } catch (...) {
        return 0;
    }
}

void fill_appliance(const json &data, const string &name, ApplianceState &out) {
    out.status = parse_status(data.at(name).get<string>());
    const auto &start = data.at(name + "_start");
    out.started_at = start.is_number() ? start.get<double>() : 0.0;
    out.expected_min = parse_minutes(data.at(name + "_expected"));
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
