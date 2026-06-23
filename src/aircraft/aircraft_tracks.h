#pragma once

#include <Arduino.h>

#include "HB9IIUdisplayInit.h"
#include "satellite_map.h"

namespace AircraftTracks {

class Renderer {
public:
    bool begin(size_t trackCapacity, size_t pointsPerTrack,
               size_t renderedPointCapacity);
    void end();
    void resetTrack(size_t trackIndex);
    void record(size_t trackIndex, double latitude, double longitude,
                uint32_t now);
    void erase();
    void draw(const SatelliteMap::Viewport& viewport,
              const size_t* trackIndices, const uint16_t* colors,
              size_t trackCount, int32_t topLimit, int32_t bottomLimit,
              uint32_t now);
    bool ready() const {
        return points_ && counts_ && heads_ && lastRecordMs_ && layers_;
    }

private:
    struct Point {
        float latitude = 0.0f;
        float longitude = 0.0f;
        uint32_t recordedMs = 0;
    };

    struct Layer {
        int16_t x = 0;
        int16_t y = 0;
        bool active = false;
    };

    Point* pointAt(size_t trackIndex, size_t logicalIndex);

    Point* points_ = nullptr;
    uint8_t* counts_ = nullptr;
    uint8_t* heads_ = nullptr;
    uint32_t* lastRecordMs_ = nullptr;
    Layer* layers_ = nullptr;
    size_t trackCapacity_ = 0;
    size_t pointsPerTrack_ = 0;
    size_t renderedPointCapacity_ = 0;
    size_t renderedPointCount_ = 0;
};

}  // namespace AircraftTracks
