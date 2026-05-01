#pragma once

#include "net/home_assistant.hpp"

#include <string>
#include <vector>

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

struct LaundryCache {
    bool have = false;
    LaundryData data;
    double last_fetch = 0.0;
    int consecutive_failures = 0;
};

namespace caches {

// Single-threaded program: globals are intentional.
inline StopCache stop;
inline WeatherCache weather;
inline LaundryCache laundry;

// Set once at startup.
inline bool ha_enabled = false;
inline std::string ha_url;
inline std::string ha_token;

}  // namespace caches
