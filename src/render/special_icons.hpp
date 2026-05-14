#pragma once

#include <cstdint>

namespace icons {

// Small RGBA pixel sprite. pixels[i] = 0xAARRGGBB where alpha=0xFF means
// opaque, alpha=0x00 means transparent (skip).
struct SpecialIcon {
    int width;
    int height;
    const uint32_t *pixels;
};

extern const SpecialIcon HEART;
extern const SpecialIcon CAKE;
extern const SpecialIcon TURKEY;
extern const SpecialIcon CAT_BLACK;
extern const SpecialIcon PUMPKIN;
extern const SpecialIcon TREE;
extern const SpecialIcon GLOBE;
extern const SpecialIcon FLAG_US;
extern const SpecialIcon PARTY;

}  // namespace icons
