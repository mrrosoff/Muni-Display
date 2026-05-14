#include "util/seasons.hpp"

#include <cmath>
#include <ctime>

namespace seasons {

namespace {

// Convert Julian Day (UTC) to local-time month/day.
MonthDay jd_to_md(double jd) {
    // JD epoch: noon UTC 4713 BC; Unix epoch JD = 2440587.5
    const time_t t = static_cast<time_t>((jd - 2440587.5) * 86400.0);
    std::tm tmv{};
    localtime_r(&t, &tmv);
    return { tmv.tm_mon + 1, tmv.tm_mday };
}

// Meeus 27.A polynomials for mean equinoxes/solstices, with Y = (year-2000)/1000.
double jde0(int year, double a, double b, double c, double d, double e) {
    const double Y = (year - 2000) / 1000.0;
    return a + b*Y + c*Y*Y + d*Y*Y*Y + e*Y*Y*Y*Y;
}

}  // namespace

MonthDay spring_equinox(int year) {
    return jd_to_md(jde0(year, 2451623.80984, 365242.37404, 0.05169, -0.00411, -0.00057));
}
MonthDay summer_solstice(int year) {
    return jd_to_md(jde0(year, 2451716.56767, 365241.62603, 0.00325, 0.00888, -0.00030));
}
MonthDay fall_equinox(int year) {
    return jd_to_md(jde0(year, 2451810.21715, 365242.01767, -0.11575, 0.00337, 0.00078));
}
MonthDay winter_solstice(int year) {
    return jd_to_md(jde0(year, 2451900.05952, 365242.74049, -0.06223, -0.00823, 0.00032));
}

}  // namespace seasons
