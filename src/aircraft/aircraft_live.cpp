#include "aircraft_live.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>

#include "myconfig.h"
#include "ui/altitude_legend.h"
#include "ui/aircraft_status_banner.h"

namespace AircraftLive {
namespace {

Aircraft::Category parseCategory(const char* value) {
    if (!value || !value[0]) return Aircraft::Category::Unknown;
    if (value[0] == 'A') {
        switch (value[1]) {
            case '0': return Aircraft::Category::CategoryA0;
            case '1': return Aircraft::Category::CategoryA1;
            case '2': return Aircraft::Category::CategoryA2;
            case '3': return Aircraft::Category::CategoryA3;
            case '4': return Aircraft::Category::CategoryA4;
            case '5': return Aircraft::Category::CategoryA5;
            case '6': return Aircraft::Category::CategoryA6;
            case '7': return Aircraft::Category::CategoryA7;
            default: return Aircraft::Category::Unknown;
        }
    }
    if (value[0] == 'B') {
        switch (value[1]) {
            case '0': return Aircraft::Category::CategoryB0;
            case '1': return Aircraft::Category::CategoryB1;
            case '2': return Aircraft::Category::CategoryB2;
            default: return Aircraft::Category::Unknown;
        }
    }
    return Aircraft::Category::Unknown;
}

const char* categoryCode(Aircraft::Category category) {
    switch (category) {
        case Aircraft::Category::CategoryA0: return "A0";
        case Aircraft::Category::CategoryA1: return "A1";
        case Aircraft::Category::CategoryA2: return "A2";
        case Aircraft::Category::CategoryA3: return "A3";
        case Aircraft::Category::CategoryA4: return "A4";
        case Aircraft::Category::CategoryA5: return "A5";
        case Aircraft::Category::CategoryA6: return "A6";
        case Aircraft::Category::CategoryA7: return "A7";
        case Aircraft::Category::CategoryB0: return "B0";
        case Aircraft::Category::CategoryB1: return "B1";
        case Aircraft::Category::CategoryB2: return "B2";
        default: return "NA";
    }
}

uint16_t altitudeColor(int32_t altitudeMeters) {
    if (altitudeMeters < 0) return TFT_DARKGREY;
    if (altitudeMeters < 1000) return TFT_RED;
    if (altitudeMeters < 5000) return TFT_GREEN;
    if (altitudeMeters < 9000) return TFT_YELLOW;
    return TFT_CYAN;
}

double distanceKm(double lat1, double lon1, double lat2, double lon2) {
    constexpr double kEarthRadiusKm = 6371.0;
    const double dLat = (lat2 - lat1) * DEG_TO_RAD;
    const double dLon = (lon2 - lon1) * DEG_TO_RAD;
    const double a =
        sin(dLat * 0.5) * sin(dLat * 0.5) +
        cos(lat1 * DEG_TO_RAD) * cos(lat2 * DEG_TO_RAD) *
        sin(dLon * 0.5) * sin(dLon * 0.5);
    return kEarthRadiusKm * 2.0 * atan2(sqrt(a), sqrt(1.0 - a));
}

bool sameViewport(const SatelliteMap::Viewport& a,
                  const SatelliteMap::Viewport& b) {
    return a.valid == b.valid &&
           a.zoom == b.zoom &&
           a.width == b.width &&
           a.height == b.height &&
           a.westPx == b.westPx &&
           a.northPx == b.northPx &&
           a.scaleX == b.scaleX &&
           a.scaleY == b.scaleY;
}

}  // namespace

bool Display::begin() {
    if (!renderer_.begin(kDrawCapacity, kSymbolSize)) {
        Serial.println("[adsb] ERROR: renderer PSRAM allocation failed");
        return false;
    }
    if (!trailRenderer_.begin(kTrackCapacity, kHistoryPointsPerTrack,
                              kRenderedTrailPointCapacity)) {
        Serial.println("[adsb] WARNING: trail PSRAM allocation failed");
    }
    Serial.printf("[adsb] live overlay ready: max %u tracks, %u drawn\n",
                  static_cast<unsigned>(kTrackCapacity),
                  static_cast<unsigned>(kDrawCapacity));
    return true;
}

bool Display::update(const SatelliteMap::Viewport& viewport) {
    if (!available() || !viewport.valid) return false;

    const uint32_t now = millis();
    const bool viewportChanged = !sameViewport(viewport, displayedViewport_);
    const bool fetchDue = lastFetchMs_ == 0 ||
                          now - lastFetchMs_ >= kFetchPeriodMs;

    bool dataChanged = false;
    if (fetchDue) {
        lastFetchMs_ = now;
        dataChanged = fetch();
        // fetch() is blocking and stamps updated tracks with a newer millis()
        // value. Use a fresh timestamp here; reusing `now` from before the
        // request would make unsigned subtraction underflow and immediately
        // expire every newly received track.
        dataChanged = expireTracks(millis()) || dataChanged;
    }

    if (!running_ || viewportChanged || dataChanged) {
        return redraw(viewport);
    }
    return false;
}

void Display::pause() {
    if (!running_) return;
    renderer_.erase();
    trailRenderer_.erase();
    drawnCount_ = 0;
    running_ = false;
}

void Display::setTracksVisible(bool visible) {
    if (!trailRenderer_.ready()) visible = false;
    if (tracksVisible_ == visible) return;
    tracksVisible_ = visible;
    running_ = false;
}

void Display::setHomeLocation(double latitude, double longitude) {
    homeLatitude_ = latitude;
    homeLongitude_ = longitude;
    homeValid_ = true;
    running_ = false;
}

void Display::setServerBase(const String& base) {
    serverBase_ = base;
}

bool Display::selectAt(int32_t screenX, int32_t screenY,
                       Selection& selection) const {
    int bestIndex = -1;
    int32_t bestDistanceSquared = INT32_MAX;
    constexpr int32_t kHalfHitSize = kSymbolSize / 2;

    for (size_t i = 0; i < drawnCount_; ++i) {
        const int32_t centerX = lroundf(drawStates_[i].x);
        const int32_t centerY = lroundf(drawStates_[i].y);
        if (screenX < centerX - kHalfHitSize ||
            screenX >= centerX + kHalfHitSize ||
            screenY < centerY - kHalfHitSize ||
            screenY >= centerY + kHalfHitSize) {
            continue;
        }

        const int32_t dx = screenX - centerX;
        const int32_t dy = screenY - centerY;
        const int32_t distanceSquared = dx * dx + dy * dy;
        if (distanceSquared < bestDistanceSquared) {
            bestDistanceSquared = distanceSquared;
            bestIndex = static_cast<int>(visibleTrackIndices_[i]);
        }
    }

    if (bestIndex < 0) return false;

    const Track& track = tracks_[bestIndex];
    selection = Selection{};
    selection.valid = true;
    strncpy(selection.hex, track.hex, sizeof(selection.hex) - 1);
    strncpy(selection.flight, track.flight,
            sizeof(selection.flight) - 1);
    strncpy(selection.category, categoryCode(track.category),
            sizeof(selection.category) - 1);
    selection.latitude = track.latitude;
    selection.longitude = track.longitude;
    selection.headingDeg = track.headingDeg;
    selection.groundSpeedKnots = track.groundSpeedKnots;
    selection.altitudeMeters = track.altitudeMeters;
    return true;
}

bool Display::fitMapRequest(double& centerLatitude, double& centerLongitude,
                            float& radiusKm, size_t& aircraftCount) const {
    constexpr double kKmPerDegreeLatitude = 111.195;
    constexpr double kAspectHeightOverWidth = 480.0 / 800.0;
    constexpr double kPaddingFactor = 1.35;
    constexpr float kMinimumRadiusKm = 5.0f;

    double north = -90.0;
    double south = 90.0;
    double east = -180.0;
    double west = 180.0;
    aircraftCount = 0;

    for (const Track& track : tracks_) {
        if (!track.used) continue;
        north = max(north, track.latitude);
        south = min(south, track.latitude);
        east = max(east, track.longitude);
        west = min(west, track.longitude);
        ++aircraftCount;
    }

    if (aircraftCount == 0) return false;

    centerLatitude = (north + south) * 0.5;
    centerLongitude = (east + west) * 0.5;

    const double verticalHalfKm =
        (north - south) * kKmPerDegreeLatitude * 0.5;
    const double horizontalHalfKm =
        (east - west) * kKmPerDegreeLatitude *
        max(0.000001, cos(centerLatitude * DEG_TO_RAD)) * 0.5;

    // Map radius is the vertical half-span. Convert the horizontal requirement
    // to the equivalent vertical radius using the 800x480 display aspect.
    const double aspectAdjustedHorizontalKm =
        horizontalHalfKm * kAspectHeightOverWidth;
    const double requiredRadiusKm =
        max(verticalHalfKm, aspectAdjustedHorizontalKm) * kPaddingFactor;

    radiusKm = max(kMinimumRadiusKm,
                   static_cast<float>(requiredRadiusKm));
    radiusKm = min(radiusKm, 5000.0f);

    Serial.printf(
        "[adsb-fit] count=%u bounds N=%.6f S=%.6f E=%.6f W=%.6f "
        "center=%.6f,%.6f radius=%.3f km\n",
        static_cast<unsigned>(aircraftCount),
        north, south, east, west,
        centerLatitude, centerLongitude, radiusKm);
    return true;
}

bool Display::fetch() {
    if (WiFi.status() != WL_CONNECTED) return false;

    HTTPClient http;
    http.setTimeout(3500);
    http.setReuse(false);
    const String streamUrl = serverBase_ + "/tar1090/data/aircraft.json";
    if (!http.begin(streamUrl)) {
        Serial.println("[adsb] HTTP begin failed");
        return false;
    }

    const int httpCode = http.GET();
    if (httpCode != HTTP_CODE_OK) {
        Serial.printf("[adsb] HTTP %d (%s) from %s  wifi=%d rssi=%d\n",
                      httpCode, http.errorToString(httpCode).c_str(),
                      streamUrl.c_str(),
                      static_cast<int>(WiFi.status()), WiFi.RSSI());
        http.end();
        return false;
    }

    JsonDocument filter;
    JsonArray filterAircraft = filter["aircraft"].to<JsonArray>();
    JsonObject fields = filterAircraft.add<JsonObject>();
    fields["hex"] = true;
    fields["flight"] = true;
    fields["lat"] = true;
    fields["lon"] = true;
    fields["track"] = true;
    fields["gs"] = true;
    fields["seen_pos"] = true;
    fields["alt_baro"] = true;
    fields["category"] = true;

    JsonDocument document;
    const DeserializationError error =
        deserializeJson(document, *http.getStreamPtr(),
                        DeserializationOption::Filter(filter));
    http.end();

    if (error) {
        Serial.printf("[adsb] JSON error: %s\n", error.c_str());
        return false;
    }

    const uint32_t now = millis();
    size_t freshPositions = 0;
    JsonArray aircraft = document["aircraft"].as<JsonArray>();
    lastTotalAircraft_ = aircraft.size();
    for (JsonObject item : aircraft) {
        const char* hex = item["hex"] | "";
        if (!hex[0] ||
            !item["lat"].is<double>() ||
            !item["lon"].is<double>()) {
            continue;
        }

        const float seenPosition = item["seen_pos"] | 9999.0f;
        if (seenPosition > kMaximumSeenPositionSeconds) continue;

        int index = findTrack(hex);
        if (index < 0) {
            index = allocateTrack();
            if (index >= 0) trailRenderer_.resetTrack(index);
        }
        if (index < 0) continue;

        Track& track = tracks_[index];
        track.used = true;
        strncpy(track.hex, hex, sizeof(track.hex) - 1);
        track.hex[sizeof(track.hex) - 1] = '\0';
        const char* flight = item["flight"] | "";
        while (*flight == ' ') ++flight;
        strncpy(track.flight, flight, sizeof(track.flight) - 1);
        track.flight[sizeof(track.flight) - 1] = '\0';
        size_t flightLength = strlen(track.flight);
        while (flightLength > 0 &&
               track.flight[flightLength - 1] == ' ') {
            track.flight[--flightLength] = '\0';
        }
        track.latitude = item["lat"].as<double>();
        track.longitude = item["lon"].as<double>();
        track.headingDeg = item["track"] | 0.0f;
        track.groundSpeedKnots = item["gs"].is<float>() ||
                                 item["gs"].is<double>() ||
                                 item["gs"].is<int>()
            ? item["gs"].as<float>()
            : -1.0f;
        track.category = parseCategory(item["category"] | "");
        track.altitudeMeters = item["alt_baro"].is<int>()
            ? static_cast<int32_t>(
                lroundf(item["alt_baro"].as<float>() * 0.3048f))
            : -1;
        track.lastUpdateMs = now;
        trailRenderer_.record(
            index, track.latitude, track.longitude, now);
        ++freshPositions;
    }
    lastPositionAircraft_ = freshPositions;

    Serial.printf("[adsb] received=%u fresh=%u\n",
                  static_cast<unsigned>(aircraft.size()),
                  static_cast<unsigned>(freshPositions));
    return true;
}

bool Display::expireTracks(uint32_t now) {
    bool changed = false;
    for (Track& track : tracks_) {
        if (track.used && now - track.lastUpdateMs > kTrackTtlMs) {
            track.used = false;
            changed = true;
        }
    }
    return changed;
}

bool Display::redraw(const SatelliteMap::Viewport& viewport) {
    // Compute new draw states into temporaries first so we can skip the
    // erase+redraw entirely when nothing has moved on screen.
    Aircraft::State newStates[kDrawCapacity];
    size_t newVisibleIndices[kDrawCapacity];
    uint16_t newVisibleColors[kDrawCapacity];
    size_t newCount = 0;

    size_t usedTracks = 0;
    size_t projectionRejected = 0;
    size_t edgeRejected = 0;
    const Track* sampleTrack = nullptr;
    int32_t sampleX = 0;
    int32_t sampleY = 0;
    double nearestKm = 1e30;
    double farthestKm = 0.0;
    int32_t minimumAltitudeMeters = INT32_MAX;
    int32_t maximumAltitudeMeters = -1;
    bool nearestAvailable = false;

    constexpr int32_t kMargin = kSymbolSize / 2 + 1;
    for (size_t trackIndex = 0;
         trackIndex < kTrackCapacity;
         ++trackIndex) {
        const Track& track = tracks_[trackIndex];
        if (!track.used || newCount >= kDrawCapacity) continue;
        ++usedTracks;

        int32_t x = 0;
        int32_t y = 0;
        if (!SatelliteMap::latLonToScreen(
                viewport, track.latitude, track.longitude, x, y)) {
            ++projectionRejected;
            if (!sampleTrack) {
                sampleTrack = &track;
                sampleX = x;
                sampleY = y;
            }
            continue;
        }
        if (x < kMargin ||
            y < AltitudeLegend::kReservedBottom + kMargin ||
            x >= viewport.width - kMargin ||
            y >= AircraftStatusBanner::kReservedTop - kMargin) {
            ++edgeRejected;
            if (!sampleTrack) {
                sampleTrack = &track;
                sampleX = x;
                sampleY = y;
            }
            continue;
        }

        Aircraft::State& state = newStates[newCount];
        state.x = static_cast<float>(x);
        state.y = static_cast<float>(y);
        state.headingDeg = track.headingDeg;
        state.color = altitudeColor(track.altitudeMeters);
        state.symbolSize = kSymbolSize;
        state.category = track.category;
        newVisibleIndices[newCount] = trackIndex;
        newVisibleColors[newCount] = state.color;
        ++newCount;

        if (homeValid_) {
            const double km = distanceKm(
                homeLatitude_, homeLongitude_,
                track.latitude, track.longitude);
            if (km < nearestKm) {
                nearestKm = km;
                nearestAvailable = true;
            }
            farthestKm = max(farthestKm, km);
        }
        if (track.altitudeMeters >= 0) {
            minimumAltitudeMeters =
                min(minimumAltitudeMeters, track.altitudeMeters);
            maximumAltitudeMeters =
                max(maximumAltitudeMeters, track.altitudeMeters);
        }
    }

    // Skip the erase+redraw when the display is already correct.
    bool needsRedraw = !running_ || (newCount != drawnCount_);
    for (size_t i = 0; !needsRedraw && i < newCount; ++i) {
        const Aircraft::State& n = newStates[i];
        const Aircraft::State& o = drawStates_[i];
        if (lroundf(n.x) != lroundf(o.x) ||
            lroundf(n.y) != lroundf(o.y) ||
            n.color    != o.color         ||
            static_cast<int>(lroundf(n.headingDeg)) !=
                static_cast<int>(lroundf(o.headingDeg)) ||
            n.category != o.category)
            needsRedraw = true;
    }

    if (needsRedraw) {
        tft.startWrite();
        // Trails are batch-erased and redrawn first; aircraft symbols are
        // updated per-aircraft via update() so each symbol is absent for only
        // one restoreBackground() call instead of all symbols disappearing at once.
        trailRenderer_.erase();
        drawnCount_ = newCount;
        memcpy(drawStates_, newStates, newCount * sizeof(Aircraft::State));
        for (size_t i = 0; i < newCount; ++i)
            visibleTrackIndices_[i] = newVisibleIndices[i];

        if (tracksVisible_) {
            trailRenderer_.draw(
                viewport,
                newVisibleIndices, newVisibleColors, newCount,
                AltitudeLegend::kReservedBottom,
                AircraftStatusBanner::kReservedTop,
                millis());
        }
        renderer_.update(drawStates_, drawnCount_);
        tft.endWrite();
    }

    status_.totalAircraft = lastTotalAircraft_;
    status_.positionAircraft = lastPositionAircraft_;
    status_.drawnAircraft = drawnCount_;
    status_.nearestKm =
        nearestAvailable ? static_cast<float>(nearestKm) : 0.0f;
    status_.farthestKm = static_cast<float>(farthestKm);
    status_.minimumAltitudeMeters =
        minimumAltitudeMeters == INT32_MAX ? -1 : minimumAltitudeMeters;
    status_.maximumAltitudeMeters = maximumAltitudeMeters;
    ++status_.generation;
    displayedViewport_ = viewport;
    running_ = true;

    const uint32_t now = millis();
    if (drawnCount_ == 0 || now - lastDebugMs_ >= 10000) {
        lastDebugMs_ = now;
        double northLat = 0.0;
        double westLon = 0.0;
        double southLat = 0.0;
        double eastLon = 0.0;
        const bool nwOk = SatelliteMap::screenToLatLon(
            viewport, 0, 0, northLat, westLon);
        const bool seOk = SatelliteMap::screenToLatLon(
            viewport, viewport.width - 1, viewport.height - 1,
            southLat, eastLon);

        Serial.printf(
            "[adsb] tracks=%u visible=%u projection_reject=%u edge_reject=%u "
            "viewport=z%u westPx=%.3f northPx=%.3f scale=%.6f,%.6f\n",
            static_cast<unsigned>(usedTracks),
            static_cast<unsigned>(drawnCount_),
            static_cast<unsigned>(projectionRejected),
            static_cast<unsigned>(edgeRejected),
            viewport.zoom, viewport.westPx, viewport.northPx,
            viewport.scaleX, viewport.scaleY);

        if (nwOk && seOk) {
            Serial.printf(
                "[adsb] map bounds: lat %.5f..%.5f  lon %.5f..%.5f\n",
                southLat, northLat, westLon, eastLon);
        }
        if (sampleTrack) {
            Serial.printf(
                "[adsb] sample rejected: hex=%s lat=%.6f lon=%.6f "
                "screen=%ld,%ld heading=%.0f alt=%ld\n",
                sampleTrack->hex,
                sampleTrack->latitude, sampleTrack->longitude,
                static_cast<long>(sampleX), static_cast<long>(sampleY),
                sampleTrack->headingDeg,
                static_cast<long>(sampleTrack->altitudeMeters));
        }
    }
    return needsRedraw;
}

int Display::findTrack(const char* hex) const {
    for (size_t i = 0; i < kTrackCapacity; ++i) {
        if (tracks_[i].used && strncmp(tracks_[i].hex, hex, 6) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

int Display::allocateTrack() {
    for (size_t i = 0; i < kTrackCapacity; ++i) {
        if (!tracks_[i].used) return static_cast<int>(i);
    }

    size_t oldest = 0;
    for (size_t i = 1; i < kTrackCapacity; ++i) {
        if (tracks_[i].lastUpdateMs < tracks_[oldest].lastUpdateMs) {
            oldest = i;
        }
    }
    return static_cast<int>(oldest);
}

}  // namespace AircraftLive
