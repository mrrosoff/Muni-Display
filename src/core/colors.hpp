#pragma once

#include "graphics.h"

namespace colors {

using rgb_matrix::Color;

inline const Color WHITE        {255, 255, 255};
inline const Color GREY         {100, 100, 100};
inline const Color DIM          { 40,  40,  40};
inline const Color LABEL        {200, 200, 200};
inline const Color YELLOW       {255, 200,   0};
inline const Color AMBER        {140, 120,   0};
inline const Color RED          {255,  80,  80};
inline const Color ICON         {180, 200, 230};
inline const Color PRECIP_BLUE  {120, 170, 220};

inline const Color RAIL_BLUE    { 70, 145, 205};
inline const Color RAIL_PURPLE  {155,  90, 175};
inline const Color RAIL_GREEN   { 50, 165,  60};

inline const Color WASHER       { 80, 160, 220};
inline const Color DRYER        {220, 130,  60};
inline const Color DONE_GREEN   { 60, 220,  90};

}  // namespace colors
