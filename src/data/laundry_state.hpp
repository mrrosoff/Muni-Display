#pragma once

#include "data/caches.hpp"

namespace laundry {

// Folds a fresh HA reading into `c`, tracking the "washer done, dryer not yet
// started" state. Caller must hold caches::mtx when `c` is the shared cache.
void apply_reading(LaundryCache &c, const LaundryData &data, double now);

}  // namespace laundry
