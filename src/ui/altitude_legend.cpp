#include "altitude_legend.h"

#include <esp32-hal-psram.h>

namespace AltitudeLegend {
namespace {

constexpr int32_t kX = 102;
constexpr int32_t kY = 5;
constexpr int32_t kW = 596;
constexpr int32_t kH = 28;
constexpr int32_t kRadius = 5;

constexpr uint16_t kPanel = 0x18E3;
constexpr uint16_t kPanelBorder = 0x5B0C;
constexpr uint16_t kText = TFT_WHITE;
constexpr uint16_t kMutedText = 0xBDF7;

void drawItem(int32_t x, uint16_t color, const char* label) {
    constexpr int32_t kSwatchSize = 9;
    const int32_t centerY = kY + kH / 2;

    tft.fillRect(x, centerY - kSwatchSize / 2,
                 kSwatchSize, kSwatchSize, color);
    tft.drawRect(x, centerY - kSwatchSize / 2,
                 kSwatchSize, kSwatchSize, kPanelBorder);
    tft.setTextColor(kText);
    tft.setTextDatum(ML_DATUM);
    tft.drawString(label, x + 14, centerY);
}

}  // namespace

bool Overlay::begin() {
    if (backup_) return true;
    backup_ = static_cast<lgfx::rgb565_t*>(
        ps_malloc(static_cast<size_t>(kW) * kH *
                  sizeof(lgfx::rgb565_t)));
    if (!backup_) {
        Serial.println("[legend] ERROR: PSRAM allocation failed");
        return false;
    }
    return true;
}

void Overlay::show() {
    if (visible_ || !backup_) return;

    tft.readRect(kX, kY, kW, kH, backup_);
    tft.fillRoundRect(kX + 1, kY + 1, kW, kH, kRadius, 0x0841);
    tft.fillRoundRect(kX, kY, kW, kH, kRadius, kPanel);
    tft.drawRoundRect(kX, kY, kW, kH, kRadius, kPanelBorder);

    tft.setFont(&fonts::DejaVu12);
    tft.setTextDatum(ML_DATUM);
    tft.setTextColor(kMutedText);
    tft.drawString("ALT m", kX + 11, kY + kH / 2);

    drawItem(kX + 63,  TFT_RED,      "< 1,000");
    drawItem(kX + 157, TFT_GREEN,    "1,000-5,000");
    drawItem(kX + 282, TFT_YELLOW,   "5,000-9,000");
    drawItem(kX + 407, TFT_CYAN,     "9,000+");
    drawItem(kX + 493, TFT_DARKGREY, "Unknown");

    tft.setTextDatum(TL_DATUM);
    visible_ = true;
}

void Overlay::hide() {
    if (!visible_ || !backup_) return;
    tft.pushImage(kX, kY, kW, kH, backup_);
    visible_ = false;
}

}  // namespace AltitudeLegend
