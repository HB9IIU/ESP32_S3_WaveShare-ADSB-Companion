#include "aircraft_status_banner.h"

#include <esp32-hal-psram.h>

namespace AircraftStatusBanner {
namespace {

constexpr int32_t kX = 20;
constexpr int32_t kY = 447;
constexpr int32_t kW = 760;
constexpr int32_t kH = 28;
constexpr int32_t kRadius = 5;

constexpr uint16_t kPanel = 0x18E3;
constexpr uint16_t kPanelBorder = 0x5B0C;
constexpr uint16_t kText = TFT_WHITE;
constexpr uint16_t kMutedText = 0xBDF7;

void drawField(int32_t labelX, int32_t valueX, const char* label,
               const char* value) {
    const int32_t centerY = kY + kH / 2;
    tft.setTextColor(kMutedText);
    tft.drawString(label, labelX, centerY);
    tft.setTextColor(kText);
    tft.drawString(value, valueX, centerY);
}

void drawSeparator(int32_t x) {
    tft.drawFastVLine(x, kY + 7, kH - 14, kPanelBorder);
}

void formatThousands(int32_t value, char* output, size_t outputSize) {
    if (value >= 1000) {
        snprintf(output, outputSize, "%ld,%03ld",
                 static_cast<long>(value / 1000),
                 static_cast<long>(value % 1000));
    } else {
        snprintf(output, outputSize, "%ld", static_cast<long>(value));
    }
}

void drawStatus(const AircraftLive::Status& status) {
    char total[8];
    char positioned[8];
    char drawn[8];
    char nearest[32];
    char farthest[16];
    char altitudeRange[24];

    snprintf(total, sizeof(total), "%u",
             static_cast<unsigned>(status.totalAircraft));
    snprintf(positioned, sizeof(positioned), "%u",
             static_cast<unsigned>(status.positionAircraft));
    snprintf(drawn, sizeof(drawn), "%u",
             static_cast<unsigned>(status.drawnAircraft));

    if (status.drawnAircraft > 0) {
        snprintf(nearest, sizeof(nearest), "%.1f km", status.nearestKm);
        snprintf(farthest, sizeof(farthest), "%.1f km", status.farthestKm);
    } else {
        strncpy(nearest, "---", sizeof(nearest));
        nearest[sizeof(nearest) - 1] = '\0';
        strncpy(farthest, "---", sizeof(farthest));
        farthest[sizeof(farthest) - 1] = '\0';
    }

    if (status.minimumAltitudeMeters >= 0 &&
        status.maximumAltitudeMeters >= 0) {
        char minimumAltitude[12];
        char maximumAltitude[12];
        formatThousands(status.minimumAltitudeMeters,
                        minimumAltitude, sizeof(minimumAltitude));
        formatThousands(status.maximumAltitudeMeters,
                        maximumAltitude, sizeof(maximumAltitude));
        snprintf(altitudeRange, sizeof(altitudeRange), "%s/%s m",
                 minimumAltitude, maximumAltitude);
    } else {
        strncpy(altitudeRange, "---", sizeof(altitudeRange));
        altitudeRange[sizeof(altitudeRange) - 1] = '\0';
    }

    tft.fillRoundRect(kX + 1, kY + 1, kW, kH, kRadius, 0x0841);
    tft.fillRoundRect(kX, kY, kW, kH, kRadius, kPanel);
    tft.drawRoundRect(kX, kY, kW, kH, kRadius, kPanelBorder);

    tft.setFont(&fonts::DejaVu12);
    tft.setTextDatum(ML_DATUM);

    drawField(kX + 15,  kX + 50,  "TOT",  total);
    drawSeparator(kX + 91);
    drawField(kX + 106, kX + 142, "POS",  positioned);
    drawSeparator(kX + 183);
    drawField(kX + 198, kX + 239, "DRW",  drawn);
    drawSeparator(kX + 280);
    drawField(kX + 295, kX + 343, "NEAR", nearest);
    drawSeparator(kX + 449);
    drawField(kX + 464, kX + 497, "FAR",  farthest);
    drawSeparator(kX + 581);
    drawField(kX + 596, kX + 629, "ALT",  altitudeRange);

    tft.setTextDatum(TL_DATUM);
}

}  // namespace

bool Overlay::begin() {
    if (backup_) return true;
    backup_ = static_cast<lgfx::rgb565_t*>(
        ps_malloc(static_cast<size_t>(kW) * kH *
                  sizeof(lgfx::rgb565_t)));
    if (!backup_) {
        Serial.println("[status] ERROR: PSRAM allocation failed");
        return false;
    }
    return true;
}

void Overlay::show(const AircraftLive::Status& status) {
    if (!backup_) return;

    if (!visible_) {
        tft.readRect(kX, kY, kW, kH, backup_);
        visible_ = true;
    } else if (displayedGeneration_ == status.generation) {
        return;
    }

    drawStatus(status);
    displayedGeneration_ = status.generation;
}

void Overlay::hide() {
    if (!visible_ || !backup_) return;
    tft.pushImage(kX, kY, kW, kH, backup_);
    displayedGeneration_ = UINT32_MAX;
    visible_ = false;
}

}  // namespace AircraftStatusBanner
