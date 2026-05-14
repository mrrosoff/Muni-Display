#pragma once

namespace seasons {

// Returns (month, day) in local time for the named astronomical event in a given year.
// Uses Meeus polynomial approximation; accurate to within a few hours for years 1000-3000,
// which is well inside the same calendar day for our purposes.
struct MonthDay { int month; int day; };

MonthDay spring_equinox(int year);
MonthDay summer_solstice(int year);
MonthDay fall_equinox(int year);
MonthDay winter_solstice(int year);

}  // namespace seasons
