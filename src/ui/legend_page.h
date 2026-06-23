#pragma once
#include <Arduino.h>
#include "HB9IIUdisplayInit.h"

// Native 70x70 legend masks (heading 0, one per category — 7560 bytes total)
#include "aircraft/assets_legend/A0leg.h"
#include "aircraft/assets_legend/A1leg.h"
#include "aircraft/assets_legend/A2leg.h"
#include "aircraft/assets_legend/A3leg.h"
#include "aircraft/assets_legend/A4leg.h"
#include "aircraft/assets_legend/A5leg.h"
#include "aircraft/assets_legend/A6leg.h"
#include "aircraft/assets_legend/A7leg.h"
#include "aircraft/assets_legend/B0leg.h"
#include "aircraft/assets_legend/B1leg.h"
#include "aircraft/assets_legend/B2leg.h"
#include "aircraft/assets_legend/NAleg.h"

// Full-screen aircraft legend.  Call show(); it blocks until the user taps,
// then returns.  The caller is responsible for restoring the map.

namespace LegendPage {

// ── Layout ────────────────────────────────────────────────────────────────────
constexpr int32_t kTitleH    = 52;
constexpr int32_t kFooterH   = 38;
constexpr int32_t kBodyH     = 480 - kTitleH - kFooterH;  // 390
constexpr int32_t kCols      = 4;
constexpr int32_t kRows      = 3;
constexpr int32_t kCellW     = 800 / kCols;               // 200
constexpr int32_t kCellH     = kBodyH / kRows;            // 130
constexpr int32_t kIconSize  = 70;                        // native mask size — no scaling
constexpr int32_t kIconTopPad = 4;

// ── Category table ────────────────────────────────────────────────────────────
struct Entry {
    const uint8_t*  mask;    // pointer to PROGMEM 1-bit mask
    uint16_t        stride;
    const char*     code;
    const char*     name;
};

static const Entry kEntries[12] = {
    { A0leg_masks, A0leg_stride, "A0", "No ADS-B info"   },
    { A1leg_masks, A1leg_stride, "A1", "Light <7,000 kg"  },
    { A2leg_masks, A2leg_stride, "A2", "Medium 1"        },
    { A3leg_masks, A3leg_stride, "A3", "Medium 2"        },
    { A4leg_masks, A4leg_stride, "A4", "High vortex"     },
    { A5leg_masks, A5leg_stride, "A5", "Heavy"           },
    { A6leg_masks, A6leg_stride, "A6", "High performance"},
    { A7leg_masks, A7leg_stride, "A7", "Rotorcraft"      },
    { B0leg_masks, B0leg_stride, "B0", "Ultralight"      },
    { B1leg_masks, B1leg_stride, "B1", "Glider"          },
    { B2leg_masks, B2leg_stride, "B2", "Balloon"         },
    { NAleg_masks, NAleg_stride, "?",  "Unknown"         },
};

// ── HSV → RGB565 colour conversion ───────────────────────────────────────────
// h: 0-360°   s, v: 0.0-1.0
static uint16_t _hsvToColor565(float h, float s, float v) {
    h = fmodf(h, 360.0f);
    if (h < 0) h += 360.0f;
    const int   i = (int)(h / 60.0f) % 6;
    const float f = h / 60.0f - (int)(h / 60.0f);
    const float p = v * (1.0f - s);
    const float q = v * (1.0f - f * s);
    const float t = v * (1.0f - (1.0f - f) * s);
    float r, g, b;
    switch (i) {
        case 0: r=v; g=t; b=p; break;
        case 1: r=q; g=v; b=p; break;
        case 2: r=p; g=v; b=t; break;
        case 3: r=p; g=q; b=v; break;
        case 4: r=t; g=p; b=v; break;
        default: r=v; g=p; b=q; break;
    }
    return ((uint16_t)(r * 31) << 11)
         | ((uint16_t)(g * 63) << 5)
         |  (uint16_t)(b * 31);
}

// ── 1-bit PROGMEM mask renderer (native size, no scaling) ────────────────────
static void _drawMask(const Entry& e, int32_t left, int32_t top, uint16_t color) {
    for (int32_t y = 0; y < kIconSize; ++y) {
        int32_t x = 0;
        while (x < kIconSize) {
            const int32_t byteIdx = y * e.stride + (x >> 3);
            const uint8_t bits = pgm_read_byte(e.mask + byteIdx);
            const bool on = bits & (0x80 >> (x & 7));
            if (!on) { ++x; continue; }
            const int32_t runStart = x;
            do {
                ++x;
                if (x >= kIconSize) break;
                const int32_t nb = y * e.stride + (x >> 3);
                if (!(pgm_read_byte(e.mask + nb) & (0x80 >> (x & 7)))) break;
            } while (true);
            tft.drawFastHLine(left + runStart, top + y, x - runStart, color);
        }
    }
}

// ── Main entry point ──────────────────────────────────────────────────────────
inline void show() {
    // Dark background
    tft.fillScreen(0x0841);

    // Title bar
    tft.fillRect(0, 0, 800, kTitleH, 0x18E3);
    tft.drawFastHLine(0, kTitleH - 1, 800, TFT_WHITE);
    tft.setFont(&fonts::DejaVu24);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Aircraft Types", 400, kTitleH / 2);

    // Footer hint
    tft.setFont(&fonts::DejaVu18);
    tft.setTextColor(0xC618);
    tft.drawString("Tap anywhere to return", 400, kTitleH + kBodyH + kFooterH / 2);

    // 12 evenly-spaced hues starting at a random offset — vivid, fully saturated
    const float hueBase = (float)(esp_random() % 360);

    // Cell grid — 4 columns × 3 rows
    for (int i = 0; i < 12; i++) {
        const int col = i % kCols;
        const int row = i / kCols;
        const int32_t cellX = col * kCellW;
        const int32_t cellY = kTitleH + row * kCellH;

        // Subtle cell separator
        tft.drawRect(cellX, cellY, kCellW, kCellH, 0x2945);

        // Icon centred horizontally, padded from top
        const int32_t iconLeft = cellX + (kCellW - kIconSize) / 2;
        const int32_t iconTop  = cellY + kIconTopPad;
        const float   hue      = fmodf(hueBase + i * 30.0f, 360.0f);
        const uint16_t color   = _hsvToColor565(hue, 0.95f, 1.0f);
        _drawMask(kEntries[i], iconLeft, iconTop, color);

        // Category code in amber
        const int32_t codeY = cellY + kIconTopPad + kIconSize + 10;
        tft.setFont(&fonts::DejaVu18);
        tft.setTextColor(TFT_YELLOW);
        tft.setTextDatum(MC_DATUM);
        tft.drawString(kEntries[i].code, cellX + kCellW / 2, codeY);

        // Description in gray
        const int32_t nameY = codeY + 24;
        tft.setTextColor(0xC618);
        tft.drawString(kEntries[i].name, cellX + kCellW / 2, nameY);
    }

    tft.setTextDatum(TL_DATUM);

    // Brief settle before accepting input
    delay(300);

    // Block until tap-and-release
    bool wasDown = false;
    while (true) {
        int32_t tx, ty;
        if (tft.getTouch(&tx, &ty)) {
            wasDown = true;
        } else if (wasDown) {
            break;
        }
        delay(10);
    }
}

} // namespace LegendPage
