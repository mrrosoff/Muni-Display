#pragma once

namespace fetch {

bool refresh_stop();
bool refresh_weather(int day_index);
bool refresh_laundry();

// True when laundry data exists and either appliance is on.
bool laundry_active();

// Pick the most-overdue source and run a single fetch. Returns true if anything
// ran (may have failed). Blocks for up to ~60s on slow networks.
bool tick();

}  // namespace fetch
