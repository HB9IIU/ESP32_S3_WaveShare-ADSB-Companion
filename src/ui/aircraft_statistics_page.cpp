#include "aircraft_statistics_page.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>

#include "HB9IIUdisplayInit.h"
#include "myconfig.h"
#include "nvs_config.h"

namespace AircraftStatisticsPage {
namespace {

constexpr int32_t kScreenWidth = 800;
constexpr int32_t kScreenHeight = 480;
constexpr uint16_t kBackground = 0x0841;
constexpr uint16_t kPanel = 0x18E3;
constexpr uint16_t kBorder = 0x5B0C;
constexpr uint16_t kMuted = 0xBDF7;

constexpr int32_t kColumnWidth = 240;
constexpr int32_t kColumnGap = 20;
constexpr int32_t kColumnX[3] = {20, 280, 540};
constexpr int32_t kCardHeight = 48;
constexpr int32_t kCardGap = 7;
constexpr int32_t kFirstCardY = 124;

String buildStatisticsUrl() {
    String url = NVSConfig::loadAdsbServer();
    url += "/stats_tft.json";
    return url;
}

void formatInteger(int32_t value, char* output, size_t outputSize) {
    if (value >= 1000) {
        snprintf(output, outputSize, "%ld,%03ld",
                 static_cast<long>(value / 1000),
                 static_cast<long>(value % 1000));
    } else {
        snprintf(output, outputSize, "%ld",
                 static_cast<long>(value));
    }
}

void formatDistance(float value, char* output, size_t outputSize) {
    snprintf(output, outputSize, "%.1f km", value);
}

void formatAltitude(int32_t value, char* output, size_t outputSize) {
    char number[20];
    formatInteger(value, number, sizeof(number));
    snprintf(output, outputSize, "%s m", number);
}

void formatSpeed(int32_t value, char* output, size_t outputSize) {
    char number[20];
    formatInteger(value, number, sizeof(number));
    snprintf(output, outputSize, "%s km/h", number);
}

void drawSectionTitle(int32_t x, const char* title) {
    tft.setTextDatum(TL_DATUM);
    tft.setFont(&fonts::DejaVu18);
    tft.setTextColor(kMuted);
    tft.drawString(title, x + 4, 94);
    tft.drawFastHLine(x, 117, kColumnWidth, kBorder);
}

void drawStatCard(int32_t x, int32_t row,
                  const char* label, const char* value) {
    const int32_t y = kFirstCardY + row * (kCardHeight + kCardGap);
    tft.fillRoundRect(x, y, kColumnWidth, kCardHeight, 6, kPanel);
    tft.drawRoundRect(x, y, kColumnWidth, kCardHeight, 6, kBorder);

    tft.setTextDatum(TL_DATUM);
    tft.setFont(&fonts::DejaVu12);
    tft.setTextColor(kMuted);
    tft.drawString(label, x + 12, y + 6);

    tft.setFont(&fonts::DejaVu18);
    tft.setTextColor(TFT_WHITE);
    tft.drawString(value && value[0] ? value : "---",
                   x + 12, y + 23);
}

}  // namespace

bool Page::open() {
    if (visible_) return false;
    visible_ = true;

    tft.fillScreen(kBackground);
    tft.setFont(&fonts::DejaVu18);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(kMuted);
    tft.drawString("Loading receiver statistics...",
                   kScreenWidth / 2, kScreenHeight / 2);

    Statistics statistics;
    if (!fetch(statistics)) {
        drawMessage("STATISTICS UNAVAILABLE",
                    "Tap anywhere to return to the map");
        return true;
    }

    draw(statistics);
    return true;
}

void Page::close() {
    visible_ = false;
}

bool Page::fetch(Statistics& statistics) {
    const String url = buildStatisticsUrl();
    Serial.printf("[statistics] fetching %s\n", url.c_str());

    HTTPClient http;
    http.setTimeout(5000);
    http.setReuse(false);
    if (!http.begin(url)) {
        Serial.println("[statistics] HTTP begin failed");
        return false;
    }

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[statistics] HTTP %d\n", code);
        http.end();
        return false;
    }

    JsonDocument filter;
    filter["aircraft_in_view"] = true;
    filter["nearest_km"] = true;
    filter["farthest_km"] = true;
    filter["highest_alt_m"] = true;
    filter["fastest_kmh"] = true;
    filter["unique_today"] = true;
    filter["peak_today"] = true;
    filter["uptime_str"] = true;
    filter["unique_ever"] = true;
    filter["closest_record_km"] = true;
    filter["farthest_record_km"] = true;
    filter["highest_record_m"] = true;
    filter["fastest_record_kmh"] = true;
    filter["peak_record"] = true;

    JsonDocument document;
    const DeserializationError error =
        deserializeJson(document, *http.getStreamPtr(),
                        DeserializationOption::Filter(filter));
    http.end();
    if (error) {
        Serial.printf("[statistics] JSON error: %s\n", error.c_str());
        return false;
    }

    statistics.aircraftInView = document["aircraft_in_view"] | 0;
    statistics.nearestKm = document["nearest_km"] | 0.0f;
    statistics.farthestKm = document["farthest_km"] | 0.0f;
    statistics.highestAltitudeM = document["highest_alt_m"] | 0;
    statistics.fastestKmh = document["fastest_kmh"] | 0;
    statistics.uniqueToday = document["unique_today"] | 0;
    statistics.peakToday = document["peak_today"] | 0;
    statistics.uniqueEver = document["unique_ever"] | 0;
    statistics.closestRecordKm =
        document["closest_record_km"] | 0.0f;
    statistics.farthestRecordKm =
        document["farthest_record_km"] | 0.0f;
    statistics.highestRecordM = document["highest_record_m"] | 0;
    statistics.fastestRecordKmh =
        document["fastest_record_kmh"] | 0;
    statistics.peakRecord = document["peak_record"] | 0;
    strncpy(statistics.uptime, document["uptime_str"] | "---",
            sizeof(statistics.uptime) - 1);

    Serial.printf(
        "[statistics] loaded: in_view=%ld unique_today=%ld unique_ever=%ld\n",
        static_cast<long>(statistics.aircraftInView),
        static_cast<long>(statistics.uniqueToday),
        static_cast<long>(statistics.uniqueEver));
    return true;
}

void Page::draw(const Statistics& statistics) {
    tft.fillScreen(kBackground);

    tft.fillRoundRect(20, 16, 760, 62, 8, kPanel);
    tft.drawRoundRect(20, 16, 760, 62, 8, kBorder);
    tft.setTextDatum(ML_DATUM);
    tft.setFont(&fonts::DejaVu24);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("RECEIVER STATISTICS", 42, 47);
    tft.setTextDatum(MR_DATUM);
    tft.setFont(&fonts::DejaVu18);
    tft.setTextColor(kMuted);
    tft.drawString("Tap to return to map", 758, 47);

    drawSectionTitle(kColumnX[0], "NOW");
    drawSectionTitle(kColumnX[1], "TODAY");
    drawSectionTitle(kColumnX[2], "RECORDS");

    char value[32];

    formatInteger(statistics.aircraftInView, value, sizeof(value));
    drawStatCard(kColumnX[0], 0, "AIRCRAFT IN VIEW", value);
    formatDistance(statistics.nearestKm, value, sizeof(value));
    drawStatCard(kColumnX[0], 1, "NEAREST", value);
    formatDistance(statistics.farthestKm, value, sizeof(value));
    drawStatCard(kColumnX[0], 2, "FARTHEST", value);
    formatAltitude(statistics.highestAltitudeM, value, sizeof(value));
    drawStatCard(kColumnX[0], 3, "HIGHEST ALTITUDE", value);
    formatSpeed(statistics.fastestKmh, value, sizeof(value));
    drawStatCard(kColumnX[0], 4, "FASTEST SPEED", value);

    formatInteger(statistics.uniqueToday, value, sizeof(value));
    drawStatCard(kColumnX[1], 0, "UNIQUE AIRCRAFT", value);
    formatInteger(statistics.peakToday, value, sizeof(value));
    drawStatCard(kColumnX[1], 1, "PEAK IN VIEW", value);
    drawStatCard(kColumnX[1], 2, "HISTORY AGE", statistics.uptime);

    formatInteger(statistics.uniqueEver, value, sizeof(value));
    drawStatCard(kColumnX[2], 0, "UNIQUE AIRCRAFT", value);
    formatDistance(statistics.closestRecordKm, value, sizeof(value));
    drawStatCard(kColumnX[2], 1, "CLOSEST", value);
    formatDistance(statistics.farthestRecordKm, value, sizeof(value));
    drawStatCard(kColumnX[2], 2, "FARTHEST", value);
    formatAltitude(statistics.highestRecordM, value, sizeof(value));
    drawStatCard(kColumnX[2], 3, "HIGHEST ALTITUDE", value);
    formatSpeed(statistics.fastestRecordKmh, value, sizeof(value));
    drawStatCard(kColumnX[2], 4, "FASTEST SPEED", value);
    formatInteger(statistics.peakRecord, value, sizeof(value));
    drawStatCard(kColumnX[2], 5, "PEAK IN VIEW", value);
}

void Page::drawMessage(const char* title, const char* detail) {
    tft.fillScreen(kBackground);
    tft.setTextDatum(MC_DATUM);
    tft.setFont(&fonts::DejaVu24);
    tft.setTextColor(TFT_WHITE);
    tft.drawString(title, kScreenWidth / 2, 210);
    tft.setFont(&fonts::DejaVu18);
    tft.setTextColor(kMuted);
    tft.drawString(detail, kScreenWidth / 2, 260);
}

}  // namespace AircraftStatisticsPage
