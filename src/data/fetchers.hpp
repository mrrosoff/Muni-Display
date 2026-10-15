#pragma once

#include <ctime>

namespace fetch {

bool refresh_stop();
bool refresh_weather(const std::tm &date);
bool refresh_laundry();

// True when laundry data exists and either appliance is on.
bool laundry_active();

// Pick the most-overdue source and run a single fetch. Returns true if anything
// ran (may have failed). Blocks for up to ~60s on slow networks.
bool tick();

}  // namespace fetch
