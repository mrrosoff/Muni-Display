#pragma once

#include <string>

struct ApplianceState {
    bool on = false;
    double started_at = 0.0;  // unix seconds; 0 if not on
    int avg_min = 0;          // 0 if unknown
};

struct LaundryData {
    ApplianceState washer;
    ApplianceState dryer;
    double fetched_at = 0.0;
};

// Thin wrapper around the HA REST API: bundles base_url + token + timeouts so
// callers don't have to thread them through every request.
class HaClient {
public:
    HaClient(
        std::string base_url,
        std::string token,
        long connect_timeout_s,
        long read_timeout_s
    );

    bool fetch_laundry(LaundryData *out, std::string *error) const;

private:
    std::string base_;
    std::string token_;
    long connect_timeout_s_;
    long read_timeout_s_;
};
