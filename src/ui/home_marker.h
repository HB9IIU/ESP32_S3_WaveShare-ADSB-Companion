#pragma once

#include <Arduino.h>

#include "HB9IIUdisplayInit.h"
#include "satellite_map.h"

namespace HomeMarker {

class Overlay {
public:
    void show(const SatelliteMap::Viewport& viewport,
              double latitude, double longitude);
    void hide();

private:
    static constexpr int32_t kSize = 9;

    int32_t left_ = 0;
    int32_t top_ = 0;
    bool visible_ = false;
};

}  // namespace HomeMarker
