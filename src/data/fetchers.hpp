#pragma once

#include <ctime>

namespace fetch {

bool refresh_stop();
bool refresh_weather(const std::tm &date);
bool refresh_laundry();

// True when laundry data exists and either appliance is on.
bool laundry_active();

// Run at most one fetch; return true if anything ran (may have failed).
bool tick_stops();
bool tick();

}  // namespace fetch
