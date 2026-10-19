#pragma once

#include <array>
#include <climits>

namespace render {

// Up to the next two arrivals (minutes from now) for one line.
struct RowTimes {
    std::array<int, 2> minutes{};
    int count = 0;
    bool cold = true;
};

// Display order: lines with a train before lines without, then soonest first.
inline bool arrives_before(const RowTimes &a, const RowTimes &b) {
    const bool ah = a.count > 0;
    const bool bh = b.count > 0;
    if (ah != bh) return ah;
    if (!ah) return false;
    if (a.minutes[0] != b.minutes[0]) return a.minutes[0] < b.minutes[0];
    // Tied on first arrival (e.g. both rounded to 3 min). Break tie on the
    // second arrival so the line with a closer follow-up wins. A line with no
    // second arrival sorts after one that has one.
    const int as = a.count > 1 ? a.minutes[1] : INT_MAX;
    const int bs = b.count > 1 ? b.minutes[1] : INT_MAX;
    return as < bs;
}

}  // namespace render
