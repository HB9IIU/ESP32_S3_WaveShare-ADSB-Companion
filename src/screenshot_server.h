#pragma once
#include <Arduino.h>
#include <WebServer.h>
#include <WiFi.h>
#include <WiFiClient.h>
#include "HB9IIUdisplayInit.h"

// Serves a live BMP screenshot of the display over HTTP on port 8080.
//
// Usage:
//   setup(): ScreenshotServer::begin();   // call after WiFi is up
//   loop():  ScreenshotServer::loop();
//
// Then open  http://<device-ip>:8080/screenshot.bmp  in any browser.
//
// The PSRAM buffer (800×480×2 = 768 KB) is allocated per-request and freed
// immediately after streaming — it does not sit idle between requests.

namespace ScreenshotServer {

static WebServer _srv(8080);

static void _serve() {
    constexpr uint16_t W = 800, H = 480;
    constexpr uint32_t ROW_BYTES = (uint32_t)W * 3;   // 2400 — already 4-byte aligned
    constexpr uint32_t BMP_SIZE  = 54 + ROW_BYTES * H;

    // Capture framebuffer
    auto* fb = static_cast<uint16_t*>(ps_malloc((size_t)W * H * sizeof(uint16_t)));
    if (!fb) { _srv.send(503, "text/plain", "PSRAM alloc failed"); return; }
    tft.readRect(0, 0, W, H, fb);

    // 54-byte BMP header
    uint8_t hdr[54] = {};
    hdr[0] = 'B'; hdr[1] = 'M';
    memcpy(hdr +  2, &BMP_SIZE,  4);   // file size
    hdr[10] = 54;                        // pixel data offset
    hdr[14] = 40;                        // DIB header size
    memcpy(hdr + 18, &W, 2);            // width
    int32_t h_neg = -(int32_t)H;
    memcpy(hdr + 22, &h_neg, 4);        // negative height = top-down row order
    hdr[26] = 1;  hdr[28] = 24;         // colour planes, bits-per-pixel
    uint32_t px_bytes = ROW_BYTES * H;
    memcpy(hdr + 34, &px_bytes, 4);     // pixel data size

    _srv.setContentLength(BMP_SIZE);
    _srv.sendHeader("Content-Disposition", "inline; filename=\"screenshot.bmp\"");
    _srv.send(200, "image/bmp", "");

    WiFiClient client = _srv.client();
    client.write(hdr, sizeof(hdr));

    auto* row = static_cast<uint8_t*>(malloc(ROW_BYTES));
    if (row) {
        for (int y = 0; y < H; y++) {
            for (int x = 0; x < W; x++) {
                // tft.readRect on this RGB-parallel panel returns byte-swapped RGB565
                uint16_t px = fb[y * W + x];
                px = (uint16_t)((px >> 8) | (px << 8));
                // Expand RGB565 → RGB888 with bit replication
                uint8_t r = (px >> 11) & 0x1F;  r = (r << 3) | (r >> 2);
                uint8_t g = (px >>  5) & 0x3F;  g = (g << 2) | (g >> 4);
                uint8_t b = (px      ) & 0x1F;  b = (b << 3) | (b >> 2);
                row[x * 3]     = b;   // BMP pixel order: BGR
                row[x * 3 + 1] = g;
                row[x * 3 + 2] = r;
            }
            client.write(row, ROW_BYTES);
        }
        free(row);
    }
    free(fb);
}

inline void begin() {
    _srv.on("/screenshot.bmp", HTTP_GET, _serve);
    _srv.begin();
    Serial.printf("[screenshot] http://%s:8080/screenshot.bmp\n",
                  WiFi.localIP().toString().c_str());
}

inline void loop() { _srv.handleClient(); }

} // namespace ScreenshotServer
