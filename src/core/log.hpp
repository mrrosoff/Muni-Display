#pragma once

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace logx {

inline void timestamp_prefix(std::ostream &os) {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto t = system_clock::to_time_t(now);
    const auto ms = duration_cast<milliseconds>(now.time_since_epoch()).count() % 1000;
    std::tm tm{};
    localtime_r(&t, &tm);
    os << '[' << std::put_time(&tm, "%H:%M:%S") << '.' << std::setw(3)
       << std::setfill('0') << ms << "] ";
}

// Concatenation-style log: log("stop ", code, ": ", elapsed_s, "s")
template <typename... Args>
void log(Args &&...args) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    timestamp_prefix(oss);
    (oss << ... << std::forward<Args>(args));
    oss << '\n';
    std::cout << oss.str() << std::flush;
}

}  // namespace logx
