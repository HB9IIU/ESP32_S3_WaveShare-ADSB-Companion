#pragma once

#include <Arduino.h>
#include "aircraft_renderer.h"

namespace AircraftDemo {

class Demo {
public:
    bool begin();
    void update();
    void pause();
    bool available() const { return renderer_.ready(); }

private:
    static constexpr size_t kAircraftCount = 99;
#ifndef AIRCRAFT_SYMBOL_SIZE
#define AIRCRAFT_SYMBOL_SIZE 32
#endif
    static constexpr uint8_t kSymbolSize = AIRCRAFT_SYMBOL_SIZE;
    static constexpr uint32_t kFramePeriodMs = 50;

    void initialiseAircraft();
    void advance(float elapsedSeconds);

    Aircraft::Renderer renderer_;
    Aircraft::State aircraft_[kAircraftCount];
    uint32_t lastFrameMs_ = 0;
    bool running_ = false;
};

}  // namespace AircraftDemo
