#pragma once

#include <Arduino.h>
#include "HB9IIUdisplayInit.h"

namespace AltitudeLegend {

// Aircraft centers above this line are withheld so symbols cannot damage the
// persistent legend overlay during track refreshes.
constexpr int32_t kReservedBottom = 36;

class Overlay {
public:
    bool begin();
    void show();
    void hide();
    bool available() const { return backup_ != nullptr; }

private:
    lgfx::rgb565_t* backup_ = nullptr;
    bool visible_ = false;
};

}  // namespace AltitudeLegend
