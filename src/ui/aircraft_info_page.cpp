#include "aircraft_info_page.h"

#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp32-hal-psram.h>

#include "myconfig.h"
#include "nvs_config.h"

namespace AircraftInfoPage {
namespace {

constexpr int32_t kScreenWidth = 800;
constexpr int32_t kScreenHeight = 480;
constexpr uint16_t kBackground = 0x0841;
constexpr uint16_t kPanel = 0x18E3;
constexpr uint16_t kBorder = 0x5B0C;
constexpr uint16_t kMuted = 0xBDF7;

const char* valueOrDash(const char* value) {
    return value && value[0] ? value : "---";
}

// Decode multi-byte UTF-8 to a codepoint; advance *src past the bytes consumed.
// Returns 0xFFFD on invalid sequences.
static uint32_t nextCodepoint(const uint8_t** src) {
    const uint8_t* s = *src;
    uint32_t cp;
    if (*s < 0x80) {
        cp = *s++;
    } else if ((*s & 0xE0) == 0xC0 && (s[1] & 0xC0) == 0x80) {
        cp = (uint32_t)(*s & 0x1F) << 6 | (s[1] & 0x3F);
        s += 2;
    } else if ((*s & 0xF0) == 0xE0 &&
               (s[1] & 0xC0) == 0x80 && (s[2] & 0xC0) == 0x80) {
        cp = (uint32_t)(*s & 0x0F) << 12 |
             (uint32_t)(s[1] & 0x3F) << 6 | (s[2] & 0x3F);
        s += 3;
    } else {
        cp = 0xFFFD;
        ++s;
    }
    *src = s;
    return cp;
}

// Latin Extended codepoint → ASCII equivalent (covers Latin-1 and Latin Extended-A/B).
static char foldLatin(uint32_t cp) {
    if (cp >= 0xC0 && cp <= 0xC6) return 'A';
    if (cp == 0xC7)                return 'C';
    if (cp >= 0xC8 && cp <= 0xCB) return 'E';
    if (cp >= 0xCC && cp <= 0xCF) return 'I';
    if (cp == 0xD0)                return 'D';
    if (cp == 0xD1)                return 'N';
    if (cp >= 0xD2 && cp <= 0xD6) return 'O';
    if (cp == 0xD8)                return 'O';
    if (cp >= 0xD9 && cp <= 0xDC) return 'U';
    if (cp == 0xDD)                return 'Y';
    if (cp == 0xDE)                return 'T';
    if (cp == 0xDF)                return 's'; // ß
    if (cp >= 0xE0 && cp <= 0xE6) return 'a';
    if (cp == 0xE7)                return 'c';
    if (cp >= 0xE8 && cp <= 0xEB) return 'e';
    if (cp >= 0xEC && cp <= 0xEF) return 'i';
    if (cp == 0xF0)                return 'd';
    if (cp == 0xF1)                return 'n';
    if (cp >= 0xF2 && cp <= 0xF6) return 'o';
    if (cp == 0xF8)                return 'o';
    if (cp >= 0xF9 && cp <= 0xFC) return 'u';
    if (cp == 0xFD || cp == 0xFF) return 'y';
    if (cp == 0xFE)                return 't';
    // Latin Extended-A (U+0100–U+017F)
    if (cp >= 0x100 && cp <= 0x101) return (cp & 1) ? 'a' : 'A';
    if (cp >= 0x102 && cp <= 0x105) return (cp & 1) ? 'a' : 'A';
    if (cp >= 0x106 && cp <= 0x10D) return (cp & 1) ? 'c' : 'C';
    if (cp >= 0x10E && cp <= 0x111) return (cp & 1) ? 'd' : 'D';
    if (cp >= 0x112 && cp <= 0x11B) return (cp & 1) ? 'e' : 'E';
    if (cp >= 0x11C && cp <= 0x123) return (cp & 1) ? 'g' : 'G';
    if (cp >= 0x124 && cp <= 0x127) return (cp & 1) ? 'h' : 'H';
    if (cp >= 0x128 && cp <= 0x131) return (cp & 1) ? 'i' : 'I';
    if (cp >= 0x132 && cp <= 0x133) return (cp & 1) ? 'j' : 'J';
    if (cp >= 0x134 && cp <= 0x135) return (cp & 1) ? 'j' : 'J';
    if (cp >= 0x136 && cp <= 0x138) return (cp & 1) ? 'k' : 'K';
    if (cp >= 0x139 && cp <= 0x142) return (cp & 1) ? 'l' : 'L';
    if (cp >= 0x143 && cp <= 0x14B) return (cp & 1) ? 'n' : 'N';
    if (cp >= 0x14C && cp <= 0x151) return (cp & 1) ? 'o' : 'O';
    if (cp >= 0x154 && cp <= 0x159) return (cp & 1) ? 'r' : 'R';
    if (cp >= 0x15A && cp <= 0x161) return (cp & 1) ? 's' : 'S';
    if (cp >= 0x162 && cp <= 0x167) return (cp & 1) ? 't' : 'T';
    if (cp >= 0x168 && cp <= 0x173) return (cp & 1) ? 'u' : 'U';
    if (cp >= 0x174 && cp <= 0x175) return (cp & 1) ? 'w' : 'W';
    if (cp >= 0x176 && cp <= 0x178) return (cp & 1) ? 'y' : 'Y';
    if (cp >= 0x179 && cp <= 0x17E) return (cp & 1) ? 'z' : 'Z';
    return '\0'; // unknown — skip
}

// Fold UTF-8 string to ASCII, replacing extended Latin with base letters.
// dst must be at least dstSize bytes (includes NUL terminator).
static void foldUtf8(const char* src, char* dst, size_t dstSize) {
    const uint8_t* s = reinterpret_cast<const uint8_t*>(src);
    size_t di = 0;
    while (*s && di + 1 < dstSize) {
        const uint32_t cp = nextCodepoint(&s);
        if (cp < 0x80) {
            dst[di++] = static_cast<char>(cp);
        } else {
            const char a = foldLatin(cp);
            if (a) dst[di++] = a;
        }
    }
    dst[di] = '\0';
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

double bearingDegrees(double lat1, double lon1,
                      double lat2, double lon2) {
    const double startLat = lat1 * DEG_TO_RAD;
    const double endLat = lat2 * DEG_TO_RAD;
    const double dLon = (lon2 - lon1) * DEG_TO_RAD;
    const double y = sin(dLon) * cos(endLat);
    const double x =
        cos(startLat) * sin(endLat) -
        sin(startLat) * cos(endLat) * cos(dLon);
    double bearing = atan2(y, x) / DEG_TO_RAD;
    if (bearing < 0.0) bearing += 360.0;
    return bearing;
}

double alongTrackDistanceKm(double originLat, double originLon,
                            double destinationLat, double destinationLon,
                            double aircraftLat, double aircraftLon) {
    constexpr double kEarthRadiusKm = 6371.0;
    const double originToAircraftAngular =
        distanceKm(originLat, originLon, aircraftLat, aircraftLon) /
        kEarthRadiusKm;
    const double routeBearing =
        bearingDegrees(originLat, originLon,
                       destinationLat, destinationLon) * DEG_TO_RAD;
    const double aircraftBearing =
        bearingDegrees(originLat, originLon,
                       aircraftLat, aircraftLon) * DEG_TO_RAD;

    return atan2(
               sin(originToAircraftAngular) *
                   cos(aircraftBearing - routeBearing),
               cos(originToAircraftAngular)) *
           kEarthRadiusKm;
}

void drawField(int32_t x, int32_t y, int32_t width,
               const char* label, const char* value) {
    constexpr int32_t kHeight = 48;
    tft.fillRoundRect(x, y, width, kHeight, 6, kPanel);
    tft.drawRoundRect(x, y, width, kHeight, 6, kBorder);
    tft.setFont(&fonts::DejaVu12);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(kMuted);
    tft.drawString(label, x + 12, y + 6);
    tft.setFont(&fonts::DejaVu18);
    tft.setTextColor(TFT_WHITE);
    tft.drawString(value, x + 12, y + 23);
}

void drawCountryField(int32_t x, int32_t y, int32_t width,
                      const char* country, const char* flagCode) {
    constexpr int32_t kHeight = 48;
    constexpr int32_t kFlagWidth = 48;
    constexpr int32_t kFlagHeight = 32;
    constexpr int32_t kFlagGap = 20;

    const char* value = valueOrDash(country);
    tft.fillRoundRect(x, y, width, kHeight, 6, kPanel);
    tft.drawRoundRect(x, y, width, kHeight, 6, kBorder);
    tft.setFont(&fonts::DejaVu12);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(kMuted);
    tft.drawString("COUNTRY", x + 12, y + 6);

    tft.setFont(&fonts::DejaVu18);
    tft.setTextColor(TFT_WHITE);
    const int32_t valueX = x + 12;
    tft.drawString(value, valueX, y + 23);

    if (!flagCode || strlen(flagCode) != 2) return;

    char flagPath[20];
    snprintf(flagPath, sizeof(flagPath), "/flags/%c%c.jpg",
             static_cast<char>(tolower(flagCode[0])),
             static_cast<char>(tolower(flagCode[1])));
    if (!LittleFS.exists(flagPath)) {
        Serial.printf("[aircraft-info] flag missing: %s\n", flagPath);
        return;
    }

    const int32_t flagX = valueX + tft.textWidth(value) + kFlagGap;
    const int32_t flagY = y + (kHeight - kFlagHeight) / 2;
    if (flagX + kFlagWidth > x + width - 8) {
        Serial.printf("[aircraft-info] flag does not fit beside %s\n", value);
        return;
    }

    if (!tft.drawJpgFile(LittleFS, flagPath, flagX, flagY,
                         kFlagWidth, kFlagHeight)) {
        Serial.printf("[aircraft-info] flag decode failed: %s\n", flagPath);
    }
}

}  // namespace

bool Page::open(const AircraftLive::Selection& selection,
                double referenceLatitude, double referenceLongitude) {
    if (!selection.valid || visible_) return false;

    backup_ = static_cast<lgfx::rgb565_t*>(
        ps_malloc(static_cast<size_t>(kScreenWidth) * kScreenHeight *
                  sizeof(lgfx::rgb565_t)));
    if (!backup_) {
        Serial.println("[aircraft-info] ERROR: PSRAM allocation failed");
        return false;
    }

    tft.readRect(0, 0, kScreenWidth, kScreenHeight, backup_);
    visible_ = true;

    tft.fillScreen(kBackground);
    tft.setFont(&fonts::DejaVu18);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(kMuted);
    tft.drawString("Loading aircraft information...",
                   kScreenWidth / 2, kScreenHeight / 2);

    Metadata metadata;
    fetchMetadata(selection.hex, metadata);
    RouteInfo route;
    fetchRoute(selection.flight, route);
    draw(selection, metadata, route,
         referenceLatitude, referenceLongitude);
    return true;
}

void Page::close() {
    if (!visible_) return;
    if (backup_) {
        tft.pushImage(0, 0, kScreenWidth, kScreenHeight, backup_);
        free(backup_);
        backup_ = nullptr;
    }
    visible_ = false;
}

bool Page::fetchMetadata(const char* hex, Metadata& metadata) {
    String url = NVSConfig::loadAdsbServer();
    url += "/hex/";
    url += hex;
    url += ".json";

    HTTPClient http;
    http.setTimeout(3500);
    http.setReuse(false);
    if (!http.begin(url)) return false;
    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[aircraft-info] metadata HTTP %d for %s\n",
                      code, hex);
        http.end();
        return false;
    }

    JsonDocument filter;
    filter["country"] = true;
    filter["flag_rgb565"] = true;
    JsonObject hexdbFilter = filter["hexdb"].to<JsonObject>();
    hexdbFilter["Manufacturer"] = true;
    hexdbFilter["OperatorFlagCode"] = true;
    hexdbFilter["RegisteredOwners"] = true;
    hexdbFilter["Registration"] = true;
    hexdbFilter["Type"] = true;

    JsonDocument document;
    const DeserializationError error =
        deserializeJson(document, *http.getStreamPtr(),
                        DeserializationOption::Filter(filter));
    http.end();
    if (error) {
        Serial.printf("[aircraft-info] metadata JSON error: %s\n",
                      error.c_str());
        return false;
    }

    JsonObject hexdb = document["hexdb"].as<JsonObject>();
    strncpy(metadata.registration, hexdb["Registration"] | "",
            sizeof(metadata.registration) - 1);
    strncpy(metadata.country, document["country"] | "",
            sizeof(metadata.country) - 1);
    const char* flagAsset = document["flag_rgb565"] | "";
    if (strlen(flagAsset) >= 2) {
        metadata.flagCode[0] = flagAsset[0];
        metadata.flagCode[1] = flagAsset[1];
        metadata.flagCode[2] = '\0';
    }
    strncpy(metadata.type, hexdb["Type"] | "",
            sizeof(metadata.type) - 1);
    strncpy(metadata.manufacturer, hexdb["Manufacturer"] | "",
            sizeof(metadata.manufacturer) - 1);
    strncpy(metadata.operatorCode, hexdb["OperatorFlagCode"] | "",
            sizeof(metadata.operatorCode) - 1);
    strncpy(metadata.owner, hexdb["RegisteredOwners"] | "",
            sizeof(metadata.owner) - 1);
    return true;
}

bool Page::fetchRoute(const char* callsign, RouteInfo& route) {
    Serial.println("[route] ────────────────────────────────");
    Serial.printf("[route] aircraft callsign: %s\n",
                  callsign && callsign[0] ? callsign : "(empty)");

    if (!callsign || !callsign[0]) {
        Serial.println("[route] ABORT: missing aircraft callsign");
        return false;
    }

    const String url =
        String("https://api.adsbdb.com/v0/callsign/") + callsign;
    Serial.println("[route] provider    : ADSBDB");
    Serial.printf("[route] request URL : %s\n", url.c_str());
    Serial.printf("[route] WiFi status=%d  RSSI=%d dBm  IP=%s\n",
                  static_cast<int>(WiFi.status()), WiFi.RSSI(),
                  WiFi.localIP().toString().c_str());

    WiFiClientSecure secureClient;
    secureClient.setInsecure();
    HTTPClient http;
    http.setTimeout(5000);
    http.setReuse(false);
    if (!http.begin(secureClient, url)) {
        Serial.println("[route] ERROR: HTTP begin failed");
        return false;
    }

    const uint32_t requestStartMs = millis();
    const int code = http.GET();
    const uint32_t requestDurationMs = millis() - requestStartMs;
    Serial.printf("[route] HTTP result=%d (%s), duration=%lu ms\n",
                  code, http.errorToString(code).c_str(),
                  static_cast<unsigned long>(requestDurationMs));

    if (code != HTTP_CODE_OK) {
        Serial.printf("[route] ERROR: ADSBDB returned HTTP %d\n", code);
        http.end();
        return false;
    }

    const String payload = http.getString();
    http.end();
    Serial.printf("[route] response bytes: %u\n",
                  static_cast<unsigned>(payload.length()));
    Serial.printf("[route] response body : %s\n", payload.c_str());

    if (payload.isEmpty()) {
        Serial.println("[route] ERROR: empty response body");
        return false;
    }

    JsonDocument document;
    const DeserializationError error =
        deserializeJson(document, payload);
    if (error) {
        Serial.printf("[route] ERROR: JSON parse failed: %s\n",
                      error.c_str());
        return false;
    }

    if (document["response"].is<const char*>()) {
        Serial.printf("[route] ADSBDB response: %s\n",
                      document["response"].as<const char*>());
        return false;
    }

    JsonObject flightRoute =
        document["response"]["flightroute"].as<JsonObject>();
    if (flightRoute.isNull()) {
        Serial.println("[route] ERROR: missing response.flightroute");
        return false;
    }

    JsonObject origin = flightRoute["origin"].as<JsonObject>();
    JsonObject destination =
        flightRoute["destination"].as<JsonObject>();
    JsonObject airline = flightRoute["airline"].as<JsonObject>();

    const char* originName = origin["municipality"] | "";
    if (!originName[0]) originName = origin["name"] | "";
    const char* destinationName = destination["municipality"] | "";
    if (!destinationName[0]) destinationName = destination["name"] | "";
    const char* originCode = origin["icao_code"] | "";
    const char* destinationCode = destination["icao_code"] | "";
    const char* airlineName = airline["name"] | "";

    if (!originName[0] || !destinationName[0] ||
        !origin["latitude"].is<double>() ||
        !origin["longitude"].is<double>() ||
        !destination["latitude"].is<double>() ||
        !destination["longitude"].is<double>()) {
        Serial.println(
            "[route] ERROR: origin/destination name or coordinates missing");
        return false;
    }

    snprintf(route.line1, sizeof(route.line1), "%s (%s)  >  %s (%s)",
             originName, originCode, destinationName, destinationCode);
    strncpy(route.airline, airlineName, sizeof(route.airline) - 1);
    route.originLatitude = origin["latitude"].as<double>();
    route.originLongitude = origin["longitude"].as<double>();
    route.destinationLatitude =
        destination["latitude"].as<double>();
    route.destinationLongitude =
        destination["longitude"].as<double>();
    route.valid = true;

    Serial.printf("[route] line1: %s\n",
                  route.line1[0] ? route.line1 : "(missing)");
    Serial.printf(
        "[route] origin=%.6f,%.6f destination=%.6f,%.6f airline=%s\n",
        route.originLatitude, route.originLongitude,
        route.destinationLatitude, route.destinationLongitude,
        route.airline[0] ? route.airline : "(missing)");
    Serial.printf("[route] result: %s\n",
                  route.valid ? "VALID" : "NO ROUTE INFORMATION");
    Serial.println("[route] ────────────────────────────────");
    return route.valid;
}

void Page::draw(const AircraftLive::Selection& selection,
                const Metadata& metadata,
                const RouteInfo& route,
                double referenceLatitude, double referenceLongitude) {
    tft.fillScreen(kBackground);

    tft.fillRoundRect(20, 16, 760, 62, 8, kPanel);
    tft.drawRoundRect(20, 16, 760, 62, 8, kBorder);
    tft.setTextDatum(ML_DATUM);
    tft.setFont(&fonts::DejaVu24);
    tft.setTextColor(TFT_WHITE);
    tft.drawString(selection.flight[0] ? selection.flight : selection.hex,
                   42, 47);
    tft.setTextDatum(MR_DATUM);
    tft.setFont(&fonts::DejaVu18);
    tft.setTextColor(kMuted);
    String identity = String("ICAO ") + selection.hex +
                      "  CAT " + selection.category;
    tft.drawString(identity, 758, 47);

    char altitude[24];
    char speed[24];
    char heading[24];
    char distance[24];
    if (selection.altitudeMeters >= 0) {
        char altBuf[16];
        formatThousands(static_cast<int32_t>(selection.altitudeMeters),
                        altBuf, sizeof(altBuf));
        snprintf(altitude, sizeof(altitude), "%s m", altBuf);
    } else {
        strncpy(altitude, "---", sizeof(altitude));
    }
    if (selection.groundSpeedKnots >= 0.0f) {
        char spdBuf[16];
        formatThousands(static_cast<int32_t>(
                            lroundf(selection.groundSpeedKnots * 1.852f)),
                        spdBuf, sizeof(spdBuf));
        snprintf(speed, sizeof(speed), "%s km/h", spdBuf);
    } else {
        strncpy(speed, "---", sizeof(speed));
    }
    snprintf(heading, sizeof(heading), "%.0f deg",
             selection.headingDeg);
    snprintf(distance, sizeof(distance), "%.1f km / %.0f deg",
             distanceKm(referenceLatitude, referenceLongitude,
                        selection.latitude, selection.longitude),
             bearingDegrees(referenceLatitude, referenceLongitude,
                            selection.latitude, selection.longitude));
    tft.fillRoundRect(20, 90, 760, 58, 6, kPanel);
    tft.drawRoundRect(20, 90, 760, 58, 6, kBorder);
    tft.setTextDatum(MC_DATUM);
    if (route.valid) {
        const double totalKm = distanceKm(
            route.originLatitude, route.originLongitude,
            route.destinationLatitude, route.destinationLongitude);
        const double projectedKm = alongTrackDistanceKm(
            route.originLatitude, route.originLongitude,
            route.destinationLatitude, route.destinationLongitude,
            selection.latitude, selection.longitude);
        const double flownKm =
            fmax(0.0, fmin(totalKm, projectedKm));
        const double remainingKm = totalKm - flownKm;
        const double directRemainingKm = distanceKm(
            selection.latitude, selection.longitude,
            route.destinationLatitude, route.destinationLongitude);
        const int progressPercent = totalKm > 0.1
            ? constrain(static_cast<int>(
                  lround((flownKm / totalKm) * 100.0)), 0, 100)
            : 0;

        char totalText[16];
        char flownText[16];
        char remainingText[16];
        const int32_t totalRounded =
            static_cast<int32_t>(lround(totalKm));
        const int32_t flownRounded =
            static_cast<int32_t>(lround(flownKm));
        const int32_t remainingRounded = totalRounded - flownRounded;
        formatThousands(totalRounded, totalText, sizeof(totalText));
        formatThousands(flownRounded, flownText, sizeof(flownText));
        formatThousands(remainingRounded, remainingText,
                        sizeof(remainingText));
        char estimate[128];
        const double speedKmh =
            selection.groundSpeedKnots >= 0.0f
                ? selection.groundSpeedKnots * 1.852
                : 0.0;
        if (speedKmh >= 30.0) {
            const uint32_t remainingSeconds = static_cast<uint32_t>(
                lround((directRemainingKm / speedKmh) * 3600.0));
            const time_t arrivalTime =
                time(nullptr) + remainingSeconds;
            struct tm arrivalLocal {};
            char arrivalText[8] = "---";
            if (localtime_r(&arrivalTime, &arrivalLocal)) {
                snprintf(arrivalText, sizeof(arrivalText), "%02d:%02d",
                         arrivalLocal.tm_hour, arrivalLocal.tm_min);
            }
            snprintf(
                estimate, sizeof(estimate),
                "Total %s km  |  Flown %s km  |  Remaining %s km  |  %d%%  |  Estimated arrival %s",
                totalText, flownText, remainingText,
                progressPercent, arrivalText);
        } else {
            snprintf(
                estimate, sizeof(estimate),
                "Total %s km  |  Flown %s km  |  Remaining %s km  |  %d%%  |  Estimated arrival ---",
                totalText, flownText, remainingText, progressPercent);
        }

        char line1Ascii[sizeof(route.line1)];
        foldUtf8(route.line1, line1Ascii, sizeof(line1Ascii));
        tft.setFont(&fonts::DejaVu18);
        tft.setTextColor(TFT_WHITE);
        tft.drawString(line1Ascii, 400, 108);
        tft.setFont(&fonts::DejaVu12);
        tft.setTextColor(kMuted);
        tft.drawString(estimate, 400, 134);

        Serial.printf(
            "[route] estimate total=%.1f km flown=%.1f km "
            "remaining=%.1f km direct_remaining=%.1f km "
            "progress=%d%% speed=%.1f km/h\n",
            totalKm, flownKm, remainingKm,
            directRemainingKm, progressPercent, speedKmh);
    } else {
        tft.setFont(&fonts::DejaVu18);
        tft.setTextColor(kMuted);
        tft.drawString("No route information available", 400, 119);
    }

    constexpr int32_t kLeft = 20;
    constexpr int32_t kRight = 410;
    constexpr int32_t kWidth = 370;
    drawField(kLeft, 160, kWidth, "REGISTRATION",
              valueOrDash(metadata.registration));
    drawCountryField(kRight, 160, kWidth,
                     metadata.country, metadata.flagCode);
    drawField(kLeft, 218, kWidth, "ALTITUDE", altitude);
    drawField(kRight, 218, kWidth, "SPEED", speed);
    drawField(kLeft, 276, kWidth, "HEADING", heading);
    drawField(kRight, 276, kWidth, "DISTANCE / BEARING", distance);
    drawField(kLeft, 334, kWidth, "AIRCRAFT TYPE",
              valueOrDash(metadata.type));
    drawField(kRight, 334, kWidth, "MANUFACTURER",
              valueOrDash(metadata.manufacturer));
    drawField(kLeft, 392, kWidth, "OPERATOR",
              valueOrDash(metadata.operatorCode));
    drawField(kRight, 392, kWidth, "OWNER",
              valueOrDash(metadata.owner));

    tft.setFont(&fonts::DejaVu12);
    tft.setTextDatum(BC_DATUM);
    tft.setTextColor(kMuted);
    tft.drawString("Tap anywhere to return to the map",
                   kScreenWidth / 2, 468);
    tft.setTextDatum(TL_DATUM);
}

}  // namespace AircraftInfoPage
