#pragma once

#include <Arduino.h>

namespace AircraftStatisticsPage {

class Page {
public:
    bool open();
    void close();
    bool visible() const { return visible_; }

private:
    struct Statistics {
        int32_t aircraftInView = 0;
        float nearestKm = 0.0f;
        float farthestKm = 0.0f;
        int32_t highestAltitudeM = 0;
        int32_t fastestKmh = 0;

        int32_t uniqueToday = 0;
        int32_t peakToday = 0;
        char uptime[32] = {0};

        int32_t uniqueEver = 0;
        float closestRecordKm = 0.0f;
        float farthestRecordKm = 0.0f;
        int32_t highestRecordM = 0;
        int32_t fastestRecordKmh = 0;
        int32_t peakRecord = 0;
    };

    bool fetch(Statistics& statistics);
    void draw(const Statistics& statistics);
    void drawMessage(const char* title, const char* detail);

    bool visible_ = false;
};

}  // namespace AircraftStatisticsPage
