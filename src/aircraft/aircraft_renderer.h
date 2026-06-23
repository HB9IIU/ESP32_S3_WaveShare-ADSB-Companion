#pragma once

#include <Arduino.h>
#include "HB9IIUdisplayInit.h"
#include "aircraft_types.h"

namespace Aircraft {

class Renderer {
public:
    bool begin(size_t capacity, uint8_t maximumSymbolSize);
    void end();
    void erase();
    void draw(const State* aircraft, size_t count);
    // Per-aircraft erase-then-draw: each symbol is absent for only the
    // duration of one restoreBackground() + one drawScaledMask() call,
    // rather than all symbols being erased simultaneously.
    void update(const State* aircraft, size_t count);
    bool ready() const { return layers_ != nullptr; }

private:
    struct Layer {
        int16_t x = 0;
        int16_t y = 0;
        uint8_t size = 0;
        uint16_t color = 0;
        int16_t headingDeg = 0;
        Category category = Category::Unknown;
        bool active = false;
    };

    void drawScaledMask(const State& aircraft, int32_t left, int32_t top,
                        uint8_t size);

    Layer* layers_ = nullptr;
    size_t capacity_ = 0;
    uint8_t maximumSymbolSize_ = 0;
};

}  // namespace Aircraft
