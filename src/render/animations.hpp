#pragma once

#include "led-matrix.h"

#include <string_view>

namespace anim {

// Particle/effect overlay over the weather icon area (x=6..37, y=17..48).
// Time-based, no persistent state.
void weather_icon(rgb_matrix::Canvas *canvas,
                  std::string_view icon_name,
                  int weather_code,
                  double t);

}  // namespace anim
