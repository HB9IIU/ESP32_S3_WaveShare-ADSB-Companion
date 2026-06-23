#pragma once

#include <Arduino.h>

#include "aircraft/aircraft_live.h"

namespace AircraftPicturePage {

class Page {
public:
    bool open(const AircraftLive::Selection& selection);
    void close();
    bool visible() const { return visible_; }

private:
    bool fetchAndDraw(const char* hex);
    void drawMessage(const char* title, const char* detail);
    bool visible_ = false;
};

}  // namespace AircraftPicturePage
