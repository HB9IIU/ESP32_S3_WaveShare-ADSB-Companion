#pragma once

#include <Arduino.h>

#include "aircraft_renderer.h"
#include "aircraft_tracks.h"
#include "satellite_map.h"

namespace AircraftLive {

struct Status {
    size_t totalAircraft = 0;
    size_t positionAircraft = 0;
    size_t drawnAircraft = 0;
    float nearestKm = 0.0f;
    float farthestKm = 0.0f;
    int32_t minimumAltitudeMeters = -1;
    int32_t maximumAltitudeMeters = -1;
    uint32_t generation = 0;
};

struct Selection {
    bool valid = false;
    char hex[7] = {0};
    char flight[9] = {0};
    char category[4] = {'N', 'A', '\0'};
    double latitude = 0.0;
    double longitude = 0.0;
    float headingDeg = 0.0f;
    float groundSpeedKnots = -1.0f;
    int32_t altitudeMeters = -1;
};

class Display {
public:
    bool begin();
    bool update(const SatelliteMap::Viewport& viewport); // true = display changed
    void pause();
    bool fitMapRequest(double& centerLatitude, double& centerLongitude,
                       float& radiusKm, size_t& aircraftCount) const;
    void setTracksVisible(bool visible);
    void setHomeLocation(double latitude, double longitude);
    void setServerBase(const String& base);
    bool tracksVisible() const { return tracksVisible_; }
    bool tracksAvailable() const { return trailRenderer_.ready(); }
    bool available() const { return renderer_.ready(); }
    const Status& status() const { return status_; }
    bool selectAt(int32_t screenX, int32_t screenY,
                  Selection& selection) const;

private:
    static constexpr size_t kTrackCapacity = 200;
    static constexpr size_t kDrawCapacity = 99;
#ifndef AIRCRAFT_SYMBOL_SIZE
#define AIRCRAFT_SYMBOL_SIZE 32
#endif
    static constexpr uint8_t kSymbolSize = AIRCRAFT_SYMBOL_SIZE;
    static constexpr uint32_t kFetchPeriodMs = 2000;
    static constexpr uint32_t kTrackTtlMs = 90000;
    static constexpr float kMaximumSeenPositionSeconds = 60.0f;
    static constexpr size_t kHistoryPointsPerTrack = 32;
    static constexpr size_t kRenderedTrailPointCapacity =
        kDrawCapacity * kHistoryPointsPerTrack;

    struct Track {
        bool used = false;
        char hex[7] = {0};
        char flight[9] = {0};
        double latitude = 0.0;
        double longitude = 0.0;
        float headingDeg = 0.0f;
        float groundSpeedKnots = -1.0f;
        int32_t altitudeMeters = -1;
        Aircraft::Category category = Aircraft::Category::Unknown;
        uint32_t lastUpdateMs = 0;
    };

    bool fetch();
    bool expireTracks(uint32_t now);
    bool redraw(const SatelliteMap::Viewport& viewport); // true = display changed
    int findTrack(const char* hex) const;
    int allocateTrack();

    Aircraft::Renderer renderer_;
    AircraftTracks::Renderer trailRenderer_;
    Track tracks_[kTrackCapacity];
    Aircraft::State drawStates_[kDrawCapacity];
    size_t visibleTrackIndices_[kDrawCapacity] = {0};
    SatelliteMap::Viewport displayedViewport_;
    uint32_t lastFetchMs_ = 0;
    uint32_t lastDebugMs_ = 0;
    size_t drawnCount_ = 0;
    size_t lastTotalAircraft_ = 0;
    size_t lastPositionAircraft_ = 0;
    Status status_;
    String serverBase_;
    double homeLatitude_ = 0.0;
    double homeLongitude_ = 0.0;
    bool homeValid_ = false;
    bool tracksVisible_ = false;
    bool running_ = false;
};

}  // namespace AircraftLive
