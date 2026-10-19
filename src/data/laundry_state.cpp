#include "data/laundry_state.hpp"

#include "util/time_utils.hpp"

namespace laundry {

namespace {

// An HA restart stamps every entity's last_changed at once; a real wash ends
// well after the dryer last changed. Anything closer than this is ambiguous.
constexpr double MIN_CHANGE_GAP_S = 60.0;

}  // namespace

void apply_reading(LaundryCache &c, const LaundryData &data, double now) {
    if (!c.have) {
        // First reading since startup: recover a pending transfer from HA's
        // last_changed times so a restart doesn't drop the DONE screen.
        if (!data.washer.on && !data.dryer.on &&
            data.washer.changed_at - data.dryer.changed_at > MIN_CHANGE_GAP_S &&
            tu::same_local_day(data.washer.changed_at, now)) {
            c.washer_done_at = data.washer.changed_at;
        }
    } else {
        // Dryer starting clears the pending-transfer state.
        if (!c.data.dryer.on && data.dryer.on) c.washer_done_at = 0.0;
        // Washer finishing while dryer is off starts the pending-transfer state.
        if (c.data.washer.on && !data.washer.on && !data.dryer.on) c.washer_done_at = now;
    }
    // Washer turning back on (unlikely but defensive) clears state.
    if (data.washer.on) c.washer_done_at = 0.0;
    c.have = true;
    c.data = data;
}

}  // namespace laundry
