#include "aircraft_assets.h"

#include <pgmspace.h>

#ifndef AIRCRAFT_SYMBOL_SIZE
#define AIRCRAFT_SYMBOL_SIZE 32
#endif

#if AIRCRAFT_SYMBOL_SIZE == 32
#include "A032_360.h"
#include "A132_360.h"
#include "A232_360.h"
#include "A332_360.h"
#include "A432_360.h"
#include "A532_360.h"
#include "A632_360.h"
#include "A732_360.h"
#include "B032_360.h"
#include "B132_360.h"
#include "B232_360.h"
#include "NA32_360.h"
#elif AIRCRAFT_SYMBOL_SIZE == 40
#include "assets40/A040_360.h"
#include "assets40/A140_360.h"
#include "assets40/A240_360.h"
#include "assets40/A340_360.h"
#include "assets40/A440_360.h"
#include "assets40/A540_360.h"
#include "assets40/A640_360.h"
#include "assets40/A740_360.h"
#include "assets40/B040_360.h"
#include "assets40/B140_360.h"
#include "assets40/B240_360.h"
#include "assets40/NA40_360.h"
#else
#error "AIRCRAFT_SYMBOL_SIZE must be 32 or 40"
#endif

namespace Aircraft {
namespace {

#define AIRCRAFT_MASK_SET(prefix) \
    {prefix##_masks, prefix##_w, prefix##_h, prefix##_stride, prefix##_bytes_per_mask}

#if AIRCRAFT_SYMBOL_SIZE == 32
const MaskSet kA0 = AIRCRAFT_MASK_SET(A032);
const MaskSet kA1 = AIRCRAFT_MASK_SET(A132);
const MaskSet kA2 = AIRCRAFT_MASK_SET(A232);
const MaskSet kA3 = AIRCRAFT_MASK_SET(A332);
const MaskSet kA4 = AIRCRAFT_MASK_SET(A432);
const MaskSet kA5 = AIRCRAFT_MASK_SET(A532);
const MaskSet kA6 = AIRCRAFT_MASK_SET(A632);
const MaskSet kA7 = AIRCRAFT_MASK_SET(A732);
const MaskSet kB0 = AIRCRAFT_MASK_SET(B032);
const MaskSet kB1 = AIRCRAFT_MASK_SET(B132);
const MaskSet kB2 = AIRCRAFT_MASK_SET(B232);
const MaskSet kUnknown = AIRCRAFT_MASK_SET(NA32);
#else
const MaskSet kA0 = AIRCRAFT_MASK_SET(A040);
const MaskSet kA1 = AIRCRAFT_MASK_SET(A140);
const MaskSet kA2 = AIRCRAFT_MASK_SET(A240);
const MaskSet kA3 = AIRCRAFT_MASK_SET(A340);
const MaskSet kA4 = AIRCRAFT_MASK_SET(A440);
const MaskSet kA5 = AIRCRAFT_MASK_SET(A540);
const MaskSet kA6 = AIRCRAFT_MASK_SET(A640);
const MaskSet kA7 = AIRCRAFT_MASK_SET(A740);
const MaskSet kB0 = AIRCRAFT_MASK_SET(B040);
const MaskSet kB1 = AIRCRAFT_MASK_SET(B140);
const MaskSet kB2 = AIRCRAFT_MASK_SET(B240);
const MaskSet kUnknown = AIRCRAFT_MASK_SET(NA40);
#endif

#undef AIRCRAFT_MASK_SET

}  // namespace

const MaskSet& maskSetFor(Category category) {
    switch (category) {
        case Category::CategoryA0: return kA0;
        case Category::CategoryA1: return kA1;
        case Category::CategoryA2: return kA2;
        case Category::CategoryA3: return kA3;
        case Category::CategoryA4: return kA4;
        case Category::CategoryA5: return kA5;
        case Category::CategoryA6: return kA6;
        case Category::CategoryA7: return kA7;
        case Category::CategoryB0: return kB0;
        case Category::CategoryB1: return kB1;
        case Category::CategoryB2: return kB2;
        default: return kUnknown;
    }
}

const uint8_t* maskForHeading(const MaskSet& set, int headingDeg) {
    headingDeg %= 360;
    if (headingDeg < 0) headingDeg += 360;

    // The imported masks rotate counter-clockwise, while aircraft headings
    // increase clockwise from north.
    const int spriteHeading = (360 - headingDeg) % 360;
    return set.masks +
        static_cast<size_t>(spriteHeading) * set.bytesPerMask;
}

}  // namespace Aircraft
