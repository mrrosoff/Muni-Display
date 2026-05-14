#include "render/special_dates.hpp"

#include "util/seasons.hpp"

namespace specdate {

namespace {

int thanksgiving_day(int year) {
    // 4th Thursday of November. Find weekday of Nov 1 and add offset.
    std::tm tmv{};
    tmv.tm_year = year - 1900;
    tmv.tm_mon = 10;   // November
    tmv.tm_mday = 1;
    tmv.tm_hour = 12;
    timegm(&tmv);      // normalize, sets tm_wday
    // tm_wday: 0=Sun, 1=Mon, ..., 4=Thu
    const int first_thu = 1 + ((4 - tmv.tm_wday + 7) % 7);
    return first_thu + 21;  // 4th Thursday
}

bool md_eq(const std::tm &d, int m, int day) {
    return d.tm_mon + 1 == m && d.tm_mday == day;
}

}  // namespace

Match for_date(const std::tm &d) {
    const int m = d.tm_mon + 1;
    const int day = d.tm_mday;
    const int year = d.tm_year + 1900;
    const int wday = d.tm_wday;  // 0=Sun..6=Sat

    // Fixed-date matches first (specific > general).
    if (md_eq(d, 1, 1))   return { "NEW YEAR",   &icons::PARTY,     true };
    if (md_eq(d, 2, 14))  return { "V-DAY",      &icons::HEART,     true };
    if (md_eq(d, 3, 14))  return { "PI DAY",     nullptr,           true };
    if (md_eq(d, 4, 1))   return { "APR FOOLS",  nullptr,           true };
    if (md_eq(d, 4, 22))  return { "EARTH DAY",  &icons::GLOBE,     true };
    if (md_eq(d, 5, 4))   return { "MAY 4TH",    &icons::SWORD,     true };
    if (md_eq(d, 5, 16))  return { "MAX",        &icons::CAKE,      true };
    if (md_eq(d, 7, 4))   return { "JULY 4TH",   &icons::STAR,      true };
    if (md_eq(d, 8, 5))   return { "JOSETTE",    &icons::CAKE,      true };
    if (md_eq(d, 10, 31)) return { "HALLOWEEN",  &icons::PUMPKIN,   true };
    if (md_eq(d, 12, 24)) return { "XMAS EVE",   &icons::TREE,      true };
    if (md_eq(d, 12, 25)) return { "CHRISTMAS",  &icons::TREE,      true };
    if (md_eq(d, 12, 31)) return { "NYE",        &icons::PARTY,     true };

    // Thanksgiving: 4th Thursday of November.
    if (m == 11 && day == thanksgiving_day(year))
        return { "THANKS", &icons::TURKEY, true };

    // Friday the 13th, any month.
    if (day == 13 && wday == 5)
        return { "FRI 13", &icons::CAT_BLACK, true };

    // Equinoxes & solstices.
    const auto sp = seasons::spring_equinox(year);
    const auto su = seasons::summer_solstice(year);
    const auto fa = seasons::fall_equinox(year);
    const auto wi = seasons::winter_solstice(year);
    if (m == sp.month && day == sp.day) return { "SPRING", &icons::SPROUT,    true };
    if (m == su.month && day == su.day) return { "SUMMER", &icons::SUN,       true };
    if (m == fa.month && day == fa.day) return { "FALL",   &icons::LEAF,      true };
    if (m == wi.month && day == wi.day) return { "WINTER", &icons::SNOWFLAKE, true };

    return { "", nullptr, false };
}

}  // namespace specdate
