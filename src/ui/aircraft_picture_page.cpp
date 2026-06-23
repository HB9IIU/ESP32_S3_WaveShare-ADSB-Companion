#include "aircraft_picture_page.h"

#include <HTTPClient.h>
#include <esp32-hal-psram.h>

#include "HB9IIUdisplayInit.h"
#include "myconfig.h"
#include "nvs_config.h"

namespace AircraftPicturePage {
namespace {

constexpr int32_t kScreenWidth = 800;
constexpr int32_t kScreenHeight = 480;
constexpr size_t kMaximumJpegBytes = 1024 * 1024;
constexpr uint16_t kBackground = 0x0841;
constexpr uint16_t kMuted = 0xBDF7;

String buildImageUrl(const char* hex) {
    String url = NVSConfig::loadAdsbServer();
    url += "/jpglarge/";
    url += hex;
    url += ".jpg";
    return url;
}

}  // namespace

bool Page::open(const AircraftLive::Selection& selection) {
    if (!selection.valid || !selection.hex[0] || visible_) return false;

    visible_ = true;
    tft.fillScreen(kBackground);
    tft.setFont(&fonts::DejaVu18);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(kMuted);
    tft.drawString("Loading aircraft picture...",
                   kScreenWidth / 2, kScreenHeight / 2);

    if (!fetchAndDraw(selection.hex)) {
        drawMessage("NO AIRCRAFT PICTURE",
                    "Tap anywhere to return to the map");
    }
    return true;
}

void Page::close() {
    visible_ = false;
}

bool Page::fetchAndDraw(const char* hex) {
    const String url = buildImageUrl(hex);
    Serial.printf("[aircraft-picture] fetching %s\n", url.c_str());

    HTTPClient http;
    http.setTimeout(10000);
    http.setReuse(false);
    if (!http.begin(url)) {
        Serial.println("[aircraft-picture] HTTP begin failed");
        return false;
    }

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[aircraft-picture] HTTP %d for %s\n",
                      code, hex);
        http.end();
        return false;
    }

    const int contentLength = http.getSize();
    if (contentLength <= 0 ||
        static_cast<size_t>(contentLength) > kMaximumJpegBytes) {
        Serial.printf("[aircraft-picture] invalid JPEG size: %d\n",
                      contentLength);
        http.end();
        return false;
    }

    uint8_t* jpeg = static_cast<uint8_t*>(
        ps_malloc(static_cast<size_t>(contentLength)));
    if (!jpeg) {
        Serial.printf("[aircraft-picture] PSRAM allocation failed: %d bytes\n",
                      contentLength);
        http.end();
        return false;
    }

    WiFiClient* stream = http.getStreamPtr();
    const size_t received =
        stream->readBytes(jpeg, static_cast<size_t>(contentLength));
    http.end();

    if (received != static_cast<size_t>(contentLength)) {
        Serial.printf(
            "[aircraft-picture] short read: %u of %d bytes\n",
            static_cast<unsigned>(received), contentLength);
        free(jpeg);
        return false;
    }

    if (contentLength < 4 ||
        jpeg[0] != 0xFF || jpeg[1] != 0xD8 ||
        jpeg[contentLength - 2] != 0xFF ||
        jpeg[contentLength - 1] != 0xD9) {
        Serial.println("[aircraft-picture] response is not a complete JPEG");
        free(jpeg);
        return false;
    }

    tft.fillScreen(TFT_BLACK);
    const bool drawn = tft.drawJpg(
        jpeg, static_cast<uint32_t>(contentLength),
        0, 0, kScreenWidth, kScreenHeight);
    free(jpeg);

    if (!drawn) {
        Serial.println("[aircraft-picture] JPEG decode failed");
        return false;
    }

    Serial.printf("[aircraft-picture] displayed %s (%d bytes)\n",
                  hex, contentLength);
    return true;
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

}  // namespace AircraftPicturePage
