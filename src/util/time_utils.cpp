#include "util/time_utils.hpp"
#include "core/config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace tu {

bool is_night() {
    const int m = now_minute_of_day();
    return m >= cfg::NIGHT_START_MIN || m < cfg::NIGHT_END_MIN;
}

bool is_dim() {
    const int m = now_minute_of_day();
    return m >= cfg::DIM_START_MIN || m < cfg::DIM_END_MIN;
}

int desired_day_index() {
    const auto t = now_unix();
    std::tm tm{};
    localtime_r(&t, &tm);
    return tm.tm_hour < 7 ? 0 : 1;
}

std::time_t parse_iso8601(std::string_view s) {
    std::tm tm{};
    // strptime needs a c string. Copy a small buffer.
    char buf[32];
    const std::size_t n = std::min(s.size(), sizeof(buf) - 1);
    std::memcpy(buf, s.data(), n);
    buf[n] = '\0';
    const char *p = strptime(buf, "%Y-%m-%dT%H:%M:%S", &tm);
    if (!p) return 0;
    int offset = 0;
    if (*p == '+' || *p == '-') {
        const int sign = (*p == '+') ? 1 : -1;
        int hh = 0, mm = 0;
        std::sscanf(p + 1, "%d:%d", &hh, &mm);
        offset = sign * (hh * 3600 + mm * 60);
    }
    return timegm(&tm) - offset;
}

std::string month_day_for(int day_offset) {
    const auto t = now_unix() + day_offset * 86400;
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof(buf), "%b %d", &tm);
    std::string s(buf);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::toupper(c); });
    return s;
}

}  // namespace tu
