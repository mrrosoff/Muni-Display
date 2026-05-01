#pragma once

#include <string>

struct ApplianceState {
    bool on = false;
    double started_at = 0.0;   // unix seconds; 0 if not on
    int avg_min = 0;            // 0 if unknown
};

struct LaundryData {
    ApplianceState washer;
    ApplianceState dryer;
    double fetched_at = 0.0;
};

bool ha_fetch_laundry(const std::string &base_url,
                      const std::string &token,
                      long connect_timeout_s,
                      long read_timeout_s,
                      LaundryData *out,
                      std::string *error);
