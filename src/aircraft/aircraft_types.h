#pragma once

#include <Arduino.h>

namespace Aircraft {

enum class Category : uint8_t {
    CategoryA0,
    CategoryA1,
    CategoryA2,
    CategoryA3,
    CategoryA4,
    CategoryA5,
    CategoryA6,
    CategoryA7,
    CategoryB0,
    CategoryB1,
    CategoryB2,
    Unknown,
};

struct State {
    float x = 0.0f;
    float y = 0.0f;
    float headingDeg = 0.0f;
    float speedPixelsPerSecond = 0.0f;
    float turnRateDegPerSecond = 0.0f;
    uint16_t color = 0xFFFF;
    uint8_t symbolSize = 32;
    Category category = Category::Unknown;
};

}  // namespace Aircraft
