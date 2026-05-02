#pragma once

#include "net/home_assistant.hpp"

#include <mutex>
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

// Caches are accessed by both the render thread (read) and the fetcher
// thread (write). Hold `mtx` for any access to the cache structs below.
// The HTTP/JSON work in fetchers happens with the lock released; only the
// brief read-snapshots and result commits take the lock.
inline std::mutex mtx;

inline StopCache stop;
inline WeatherCache weather;
inline LaundryCache laundry;

// Set once at startup, immutable thereafter — no lock needed.
inline bool ha_enabled = false;
inline std::string ha_url;
inline std::string ha_token;

}  // namespace caches
