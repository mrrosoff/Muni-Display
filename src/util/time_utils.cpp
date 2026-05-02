#include "util/time_utils.hpp"
#include "core/config.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

using namespace std;

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
    tm tmv{};
    localtime_r(&t, &tmv);
    return tmv.tm_hour < 7 ? 0 : 1;
}

time_t parse_iso8601(string_view s) {
    tm tmv{};
    char buf[32];
    const auto n = min(s.size(), sizeof(buf) - 1);
    memcpy(buf, s.data(), n);
    buf[n] = '\0';
    const char *p = strptime(buf, "%Y-%m-%dT%H:%M:%S", &tmv);
    if (!p) return 0;
    if (*p == '.') {
        ++p;
        while (*p >= '0' && *p <= '9') ++p;
    }
    int offset = 0;
    if (*p == '+' || *p == '-') {
        const int sign = (*p == '+') ? 1 : -1;
        int hh = 0, mm = 0;
        sscanf(p + 1, "%d:%d", &hh, &mm);
        offset = sign * (hh * 3600 + mm * 60);
    }
    return timegm(&tmv) - offset;
}

string month_day_for(int day_offset) {
    const auto t = now_unix() + day_offset * 86400;
    tm tmv{};
    localtime_r(&t, &tmv);
    char buf[16];
    strftime(buf, sizeof(buf), "%b %d", &tmv);
    string s(buf);
    to_upper(s);
    return s;
}

void to_upper(string &s) {
    transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return toupper(c); });
}

}  // namespace tu
