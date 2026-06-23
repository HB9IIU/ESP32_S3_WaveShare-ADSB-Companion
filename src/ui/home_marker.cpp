#include "home_marker.h"

#include "ui/aircraft_status_banner.h"
#include "ui/altitude_legend.h"

namespace HomeMarker {

void Overlay::show(const SatelliteMap::Viewport& viewport,
                   double latitude, double longitude) {
    if (visible_ || !viewport.valid) return;

    int32_t centerX = 0;
    int32_t centerY = 0;
    if (!SatelliteMap::latLonToScreen(
            viewport, latitude, longitude, centerX, centerY)) {
        return;
    }

    left_ = centerX - kSize / 2;
    top_ = centerY - kSize / 2;
    if (left_ < 0 ||
        top_ < AltitudeLegend::kReservedBottom ||
        left_ + kSize > viewport.width ||
        top_ + kSize > AircraftStatusBanner::kReservedTop) {
        return;
    }

    tft.fillCircle(centerX, centerY, 4, TFT_WHITE);
    tft.fillCircle(centerX, centerY, 2, TFT_MAGENTA);
    visible_ = true;
}

void Overlay::hide() {
    if (!visible_) return;
    SatelliteMap::restoreBackground(left_, top_, kSize, kSize);
    visible_ = false;
}

}  // namespace HomeMarker
