#include "aircraft_demo.h"

#include <math.h>

#include "HB9IIUdisplayInit.h"

namespace AircraftDemo {
namespace {

constexpr int32_t kScreenWidth = 800;
constexpr int32_t kScreenHeight = 480;

constexpr uint16_t kAircraftColor = TFT_CYAN;
constexpr size_t kCategoryCount = 12;

float randomFloat(float low, float high) {
    return low + (high - low) *
        (static_cast<float>(random(0, 10001)) / 10000.0f);
}

float normaliseHeading(float heading) {
    while (heading < 0.0f) heading += 360.0f;
    while (heading >= 360.0f) heading -= 360.0f;
    return heading;
}

}  // namespace

bool Demo::begin() {
    randomSeed(esp_random());
    if (!renderer_.begin(kAircraftCount, kSymbolSize)) {
        Serial.println("[aircraft-demo] ERROR: renderer PSRAM allocation failed");
        return false;
    }

    initialiseAircraft();
    Serial.printf("[aircraft-demo] ready: %u aircraft, 12 categories, %u px\n",
                  static_cast<unsigned>(kAircraftCount),
                  static_cast<unsigned>(kSymbolSize));
    return true;
}

void Demo::initialiseAircraft() {
    for (size_t i = 0; i < kAircraftCount; ++i) {
        Aircraft::State& item = aircraft_[i];
        item.category =
            static_cast<Aircraft::Category>(i % kCategoryCount);
        item.symbolSize = kSymbolSize;
        item.color = kAircraftColor;

        const float margin = item.symbolSize / 2.0f + 2.0f;
        item.x = randomFloat(margin, kScreenWidth - margin);
        item.y = randomFloat(margin, kScreenHeight - margin);
        item.headingDeg = randomFloat(0.0f, 360.0f);
        item.speedPixelsPerSecond = randomFloat(18.0f, 46.0f);
        item.turnRateDegPerSecond = randomFloat(-18.0f, 18.0f);
    }
}

void Demo::update() {
    if (!available()) return;

    if (!running_) {
        renderer_.draw(aircraft_, kAircraftCount);
        lastFrameMs_ = millis();
        running_ = true;
        return;
    }

    const uint32_t now = millis();
    const uint32_t elapsedMs = now - lastFrameMs_;
    if (elapsedMs < kFramePeriodMs) return;

    renderer_.erase();
    advance(min(elapsedMs, 200U) / 1000.0f);
    renderer_.draw(aircraft_, kAircraftCount);
    lastFrameMs_ = now;
}

void Demo::pause() {
    if (!running_) return;
    renderer_.erase();
    running_ = false;
}

void Demo::advance(float elapsedSeconds) {
    for (Aircraft::State& item : aircraft_) {
        item.headingDeg = normaliseHeading(
            item.headingDeg + item.turnRateDegPerSecond * elapsedSeconds);

        const float radians = item.headingDeg * DEG_TO_RAD;
        item.x += sinf(radians) * item.speedPixelsPerSecond * elapsedSeconds;
        item.y -= cosf(radians) * item.speedPixelsPerSecond * elapsedSeconds;

        const float margin = item.symbolSize / 2.0f + 2.0f;
        const float minY = margin;
        const float maxY = kScreenHeight - margin;
        const float minX = margin;
        const float maxX = kScreenWidth - margin;

        if (item.x < minX || item.x > maxX) {
            item.x = constrain(item.x, minX, maxX);
            item.headingDeg = normaliseHeading(360.0f - item.headingDeg);
        }
        if (item.y < minY || item.y > maxY) {
            item.y = constrain(item.y, minY, maxY);
            item.headingDeg = normaliseHeading(180.0f - item.headingDeg);
        }
    }
}

}  // namespace AircraftDemo
