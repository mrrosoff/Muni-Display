#pragma once

#include "render/special_icons.hpp"

#include <ctime>
#include <string>

namespace specdate {

struct Match {
    std::string text;                       // header text (uppercase, fits 64px)
    const icons::SpecialIcon *icon;         // optional icon (nullptr if text-only)
    bool found;
};

// Returns the matching special-date treatment for the given local date, or
// {.., .., false} if it's an ordinary day. Handles fixed dates, Friday the 13th,
// equinoxes/solstices, and Thanksgiving (4th Thursday of November).
Match for_date(const std::tm &date);

}  // namespace specdate
