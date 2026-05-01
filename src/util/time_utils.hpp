#pragma once

#include <chrono>
#include <ctime>
#include <string>
#include <string_view>

namespace tu {

// Steady-clock seconds since some monotonic origin.
inline double monotonic() {
    using namespace std::chrono;
    return duration<double>(steady_clock::now().time_since_epoch()).count();
}

inline std::time_t now_unix() { return std::time(nullptr); }

inline int now_minute_of_day() {
    const auto t = now_unix();
    std::tm tm{};
    localtime_r(&t, &tm);
    return tm.tm_hour * 60 + tm.tm_min;
}

bool is_night();
bool is_dim();
int desired_day_index();

// Parse ISO 8601 ("2026-04-30T12:34:56Z" / "...+HH:MM"). Returns 0 on bad input.
std::time_t parse_iso8601(std::string_view s);

// "MAY 01" for today() + day_offset days.
std::string month_day_for(int day_offset);

}  // namespace tu
