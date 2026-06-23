#pragma once

#include <Arduino.h>

#include "HB9IIUdisplayInit.h"
#include "aircraft/aircraft_live.h"

namespace AircraftStatusBanner {

// Aircraft centres below this line are withheld so symbol redraws cannot
// overwrite the persistent status overlay.
constexpr int32_t kReservedTop = 444;

class Overlay {
public:
    bool begin();
    void show(const AircraftLive::Status& status);
    void hide();
    bool available() const { return backup_ != nullptr; }

private:
    lgfx::rgb565_t* backup_ = nullptr;
    uint32_t displayedGeneration_ = UINT32_MAX;
    bool visible_ = false;
};

}  // namespace AircraftStatusBanner
