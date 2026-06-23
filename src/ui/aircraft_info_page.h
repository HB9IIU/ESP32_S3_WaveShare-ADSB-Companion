#pragma once

#include <Arduino.h>
#include <LittleFS.h>

#include "HB9IIUdisplayInit.h"
#include "aircraft/aircraft_live.h"

namespace AircraftInfoPage {

class Page {
public:
    bool open(const AircraftLive::Selection& selection,
              double referenceLatitude, double referenceLongitude);
    void close();
    bool visible() const { return visible_; }

private:
    struct Metadata {
        char registration[16] = {0};
        char country[32] = {0};
        char flagCode[4] = {0};
        char type[32] = {0};
        char manufacturer[32] = {0};
        char operatorCode[16] = {0};
        char owner[48] = {0};
    };

    struct RouteInfo {
        char line1[96] = {0};
        char airline[64] = {0};
        double originLatitude = 0.0;
        double originLongitude = 0.0;
        double destinationLatitude = 0.0;
        double destinationLongitude = 0.0;
        bool valid = false;
    };

    bool fetchMetadata(const char* hex, Metadata& metadata);
    bool fetchRoute(const char* callsign, RouteInfo& route);
    void draw(const AircraftLive::Selection& selection,
              const Metadata& metadata,
              const RouteInfo& route,
              double referenceLatitude, double referenceLongitude);

    lgfx::rgb565_t* backup_ = nullptr;
    bool visible_ = false;
};

}  // namespace AircraftInfoPage
