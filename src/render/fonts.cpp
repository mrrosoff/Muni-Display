#include "render/fonts.hpp"

bool Fonts::load() {
    return title.LoadFont("fonts/6x10.bdf")
        && dir.LoadFont("fonts/4x6.bdf")
        && row.LoadFont("fonts/5x7.bdf")
        && badge_1.LoadFont("fonts/9x15B.bdf");
}
