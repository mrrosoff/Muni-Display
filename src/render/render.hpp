#pragma once

#include "render/fonts.hpp"
#include "util/xbm.hpp"

#include "led-matrix.h"

#include <map>
#include <string>

namespace render {

void muni(rgb_matrix::Canvas *canvas, const Fonts &fonts);

void weather(
    rgb_matrix::Canvas *canvas,
    const Fonts &fonts,
    const std::map<std::string, XbmIcon> &icons
);

void laundry(
    rgb_matrix::Canvas *canvas,
    const Fonts &fonts,
    const std::map<std::string, XbmIcon> &icons
);

}  // namespace render
