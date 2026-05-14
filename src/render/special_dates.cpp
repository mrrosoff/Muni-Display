#include "render/special_dates.hpp"

namespace specdate {

namespace {

int thanksgiving_day(int year) {
    // 4th Thursday of November.
    std::tm tmv{};
    tmv.tm_year = year - 1900;
    tmv.tm_mon = 10;   // November
    tmv.tm_mday = 1;
    tmv.tm_hour = 12;
    timegm(&tmv);      // normalize, sets tm_wday
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
    const int wday = d.tm_wday;

    if (md_eq(d, 1, 1))   return { "NEW YEARS DAY",  &icons::PARTY,     true };
    if (md_eq(d, 2, 14))  return { "VALENTINES",     &icons::HEART,     true };
    if (md_eq(d, 3, 14))  return { "PI DAY",         nullptr,           true };
    if (md_eq(d, 4, 22))  return { "EARTH DAY",      &icons::GLOBE,     true };
    if (md_eq(d, 5, 16))  return { "MAX'S BDAY",     &icons::CAKE,      true };
    if (md_eq(d, 7, 4))   return { "JULY 4TH",       &icons::FLAG_US,   true };
    if (md_eq(d, 8, 5))   return { "JOSETTE'S BDAY", &icons::CAKE,      true };
    if (md_eq(d, 10, 31)) return { "HALLOWEEN",      &icons::PUMPKIN,   true };
    if (md_eq(d, 12, 24)) return { "CHRISTMAS EVE",  &icons::TREE,      true };
    if (md_eq(d, 12, 25)) return { "CHRISTMAS",      &icons::TREE,      true };
    if (md_eq(d, 12, 31)) return { "NEW YEARS EVE",  &icons::PARTY,     true };

    if (m == 11 && day == thanksgiving_day(year))
        return { "THANKSGIVING", &icons::TURKEY, true };

    if (day == 13 && wday == 5)
        return { "FRIDAY 13TH", &icons::CAT_BLACK, true };

    return { "", nullptr, false };
}

}  // namespace specdate
