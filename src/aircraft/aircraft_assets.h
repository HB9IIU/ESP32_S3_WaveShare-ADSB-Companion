#pragma once

#include <Arduino.h>
#include "aircraft_types.h"

namespace Aircraft {

struct MaskSet {
    const uint8_t* masks;
    uint16_t width;
    uint16_t height;
    uint16_t stride;
    uint16_t bytesPerMask;
};

const MaskSet& maskSetFor(Category category);
const uint8_t* maskForHeading(const MaskSet& set, int headingDeg);

}  // namespace Aircraft
