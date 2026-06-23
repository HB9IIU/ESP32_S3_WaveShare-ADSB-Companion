// ═════════════════════════════════════════════════════════════════════════════
// main.cpp — ESP32-S3 ESRI World Imagery satellite map display
//
// Hardware
//   Waveshare ESP32-S3-Touch-LCD-7  (800×480 RGB parallel LCD, 8 MB OPI PSRAM,
//   16 MB flash, GT911 capacitive touch, CH422G GPIO expander for backlight)
//
// Software stack
//   LovyanGFX  — RGB-panel driver; single PSRAM framebuffer, continuous DMA
//   LittleFS   — tile scratch file + map snapshot storage
//   FreeRTOS   — underlying RTOS (Arduino loop runs in its own task)
//
// Interaction model (state machine — see InteractionMode enum)
//   Normal   : tap anywhere → open Map Actions menu
//   Menu     : tap a button → save / delete / re-centre / zoom / close
//   Recenter : tap the desired new centre point on the map
//   Zoom     : numeric keypad to enter a new view radius
//
// Key hardware constraint
//   Flash writes on ESP32-S3 temporarily disable the data cache (DCache).
//   This starves the RGB panel's DMA from the PSRAM framebuffer and causes
//   visible flicker.  The backlight is turned off around any flash-write
//   operation (map save, LittleFS open) that lasts more than a few ms.
// ═════════════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <LittleFS.h>
#include <esp32-hal-psram.h>
#include "HB9IIUdisplayInit.h"
#if AIRCRAFT_LIVE_DATA
#include "aircraft/aircraft_live.h"
#else
#include "aircraft/aircraft_demo.h"
#endif
#include "boot_manager.h"
#include "fresh_setup.h"
#include "map_snapshot.h"
#include "myconfig.h"
#include "satellite_map.h"
#include "ui/altitude_legend.h"
#include "ui/aircraft_status_banner.h"
#include "ui/aircraft_info_page.h"
#include "ui/aircraft_picture_page.h"
#include "ui/aircraft_statistics_page.h"
#include "ui/home_marker.h"

LGFX tft;
#if AIRCRAFT_LIVE_DATA
static AircraftLive::Display aircraftOverlay;
#else
static AircraftDemo::Demo aircraftDemo;
#endif
static AltitudeLegend::Overlay altitudeLegend;
static AircraftStatusBanner::Overlay aircraftStatusBanner;
static AircraftInfoPage::Page aircraftInfoPage;
static AircraftPicturePage::Page aircraftPicturePage;
static AircraftStatisticsPage::Page aircraftStatisticsPage;
static HomeMarker::Overlay homeMarker;

// ═════════════════════════════════════════════════════════════════════════════
// Interaction state machine
// ═════════════════════════════════════════════════════════════════════════════
// Defined before the forward declarations because close_overlay() uses
// InteractionMode as a default parameter type.

enum class InteractionMode {
    Normal,    // idle; tap opens the action menu
    Menu,      // action menu is open; taps are routed to handle_menu_action()
    Zoom,      // zoom dialog is open; taps go to handle_zoom_action()
    Recenter,  // waiting for the user to tap a new map centre
    AircraftInfo, // selected-aircraft information page
    AircraftPicture, // selected-aircraft full-screen picture page
    AircraftStatistics, // receiver statistics page
};

enum class MenuAction {
    None,      // tap was outside any button
    Save,
    Delete,
    Recenter,
    Zoom,
    Close,
    FitAircraft,
    ToggleInfoBars,
    ToggleTracks,
    SetHome,
    AdsbServer,
    WiFiSettings,
};

// ═════════════════════════════════════════════════════════════════════════════
// Forward declarations
// ═════════════════════════════════════════════════════════════════════════════

// Startup helpers
static bool show_splash();
static void show_map_error(const char* message);

// Map rendering
static bool draw_map();
static bool draw_map_dark();
static void mark_selected_point(int32_t x, int32_t y);

// Overlay core (shared by both the action menu and the zoom dialog)
static void close_overlay(InteractionMode nextMode = InteractionMode::Normal);

// Map action menu
static bool open_action_menu();
static void close_action_menu();
static void draw_action_menu();
static void show_status_overlay(const char* title, const char* subtitle, uint32_t color);
static bool save_current_map(bool showProgress = true);
static void handle_menu_action(int32_t x, int32_t y);

// Zoom / radius dialog
static bool open_zoom_dialog();
static void draw_zoom_dialog();
static void handle_zoom_action(int32_t x, int32_t y);

// ═════════════════════════════════════════════════════════════════════════════
// Layout constants
// ═════════════════════════════════════════════════════════════════════════════

// ── Map action menu ───────────────────────────────────────────────────────────
// No title bar. Three rows of buttons, evenly spaced.
// Tap outside the panel to close (no Close button).
//
// Row 1 — map operations  : Save | Delete | Re-center | Zoom        (4 × 150 px)
// Row 2 — view / overlays : Fit  | Info   | Tracks    | Set home    (4 × 150 px)
// Row 3 — settings        :         WiFi  |  ADSB Server            (2 × 190 px, centred)
//
// Vertical spacing: top-pad 30 px, row-gap 30 px, bottom-pad 30 px
// → kMenuH = 3 × 60 + 4 × 30 = 300 px
constexpr int32_t kMenuX        = 70;
constexpr int32_t kMenuY        = 65;
constexpr int32_t kMenuW        = 660;
constexpr int32_t kMenuH        = 300;
constexpr int32_t kButtonH      = 60;

// Rows 1 & 2: 4 equal buttons, 15 px margin, 10 px gap → W = (660 - 30 - 30) / 4 = 150
constexpr int32_t kButtonW      = 150;
constexpr int32_t kButtonGap    = 10;
constexpr int32_t kFirstButtonX = 85;    // kMenuX + 15 px margin
constexpr int32_t kButtonY      = 95;    // kMenuY + 30 px top-pad
constexpr int32_t kWideButtonW  = 150;
constexpr int32_t kWideButtonGap = 10;
constexpr int32_t kFitButtonX   = kFirstButtonX;
constexpr int32_t kInfoButtonX  = kFitButtonX   + kWideButtonW + kWideButtonGap;
constexpr int32_t kTracksButtonX = kInfoButtonX + kWideButtonW + kWideButtonGap;
constexpr int32_t kHomeButtonX  = kTracksButtonX + kWideButtonW + kWideButtonGap;
constexpr int32_t kSecondButtonY = kButtonY + kButtonH + 30;  // 185

// Row 3: two centred settings buttons
constexpr int32_t kThirdButtonY  = kSecondButtonY + kButtonH + 30;  // 275
constexpr int32_t kThirdButtonW  = 190;
constexpr int32_t kThirdButtonGap = 20;
constexpr int32_t kWiFiButtonX   =
    kMenuX + (kMenuW - 2 * kThirdButtonW - kThirdButtonGap) / 2;
constexpr int32_t kAdsbButtonX   = kWiFiButtonX + kThirdButtonW + kThirdButtonGap;
constexpr int32_t kAdsbButtonW   = kThirdButtonW;

// ── Button colour palette (RGB565) ───────────────────────────────────────────
// All action-menu and zoom-dialog buttons share this palette so the UI has a
// single consistent visual language.  The 3D bevel is achieved with two extra
// lines per button: kBtnHi on top/left (light), kBtnSh on bottom/right (dark).
constexpr uint32_t kBtnFace    = 0x3AD0; // slate-blue face  ~RGB(56,88,128)
constexpr uint32_t kBtnHi      = 0x5BF5; // top/left highlight ~RGB(88,124,168)
constexpr uint32_t kBtnSh      = 0x1968; // bottom/right shadow ~RGB(24,44,64)
constexpr uint32_t kBtnText    = 0xFFFF; // white label text
constexpr uint32_t kBtnDisFace = 0x2967; // disabled face (dark muted)
constexpr uint32_t kBtnDisText = 0x7BD1; // disabled label (medium grey)

// ── Zoom / radius dialog ──────────────────────────────────────────────────────
// Taller panel that fills most of the screen; contains an input field,
// a 3×4 numpad on the left, and three action buttons on the right.
constexpr int32_t kZoomX      = 70;
constexpr int32_t kZoomY      = 35;
constexpr int32_t kZoomW      = 660;
constexpr int32_t kZoomH      = 410;
constexpr int32_t kKeyW       = 92;
constexpr int32_t kKeyH       = 54;
constexpr int32_t kKeyGap     = 10;
constexpr int32_t kKeyStartX  = 105;
constexpr int32_t kKeyStartY  = 178;  // top of numpad row 0

// ═════════════════════════════════════════════════════════════════════════════
// Global state
// ═════════════════════════════════════════════════════════════════════════════

static SatelliteMap::Request  mapRequest;    // parameters for the next/current map
static SatelliteMap::Viewport mapViewport;   // Mercator transform of the displayed map
static bool mapReady          = false;       // true when a valid map is on screen
static bool savedMapAvailable = false;       // true when a snapshot exists in LittleFS
static bool infoBarsVisible    = true;        // top legend + bottom live status
static double homeLatitude    = HOME_FALLBACK_LAT;
static double homeLongitude   = HOME_FALLBACK_LON;
#if AIRCRAFT_LIVE_DATA
static AircraftLive::Selection selectedAircraft;
#endif

// Touch state — updated every loop() iteration
static bool    touchWasDown = false;
static int32_t touchX       = 0;
static int32_t touchY       = 0;
static uint32_t lastTapMs   = 0;  // millis() of the last processed tap (debounce)

static InteractionMode interactionMode = InteractionMode::Normal;

// Overlay backup — PSRAM buffer holding the map pixels hidden by an overlay.
// Freed and set to nullptr when the overlay is dismissed (close_overlay).
static lgfx::rgb565_t* menuBackup = nullptr;
static int32_t overlayX = 0;
static int32_t overlayY = 0;
static int32_t overlayW = 0;
static int32_t overlayH = 0;

// Zoom dialog input state
static String zoomInput;       // digits typed so far
static bool   zoomUnitKm = false; // true = km, false = m

// ═════════════════════════════════════════════════════════════════════════════
// Entry points
// ═════════════════════════════════════════════════════════════════════════════

void setup() {
    Serial.begin(115200);
    Serial.setTxTimeoutMs(0);  // don't block if no USB-CDC monitor is connected
    delay(500);                // give USB-CDC time to enumerate on the host
    Serial.println("\n=== ESP32-S3 ESRI World Imagery Display ===");

    initTFT();
    Serial.println("TFT initialised.");
#if AIRCRAFT_LIVE_DATA
    aircraftOverlay.begin();
#else
    aircraftDemo.begin();
#endif
    altitudeLegend.begin();
    aircraftStatusBanner.begin();

    // Seed the map request with the compile-time fallback.  These values are
    // overwritten further down by either the saved snapshot or IP geolocation.
    mapRequest.centerLat = MAP_CENTER_LAT;
    mapRequest.centerLon = MAP_CENTER_LON;
    mapRequest.radiusKm  = MAP_RADIUS_KM;
    mapRequest.width     = 800;
    mapRequest.height    = 480;

    // ── Pre-boot LittleFS mount (splash only) ────────────────────────────────
    // Show the splash and keep it visible while BootManager runs through the
    // full network sequence (WiFi, geolocation, NTP).  The map is displayed
    // only after all init functions complete.
    if (!LittleFS.begin(false, "/littlefs", 10, "littlefs")) {
        Serial.println("LittleFS mount failed.");
    } else {
        Serial.println("LittleFS mounted.");
        show_splash();
        LittleFS.end();
    }

    // ── BootManager: WiFi, geolocation, NTP ──────────────────────────────────
    BootManager::run();

#if AIRCRAFT_LIVE_DATA
    aircraftOverlay.setServerBase(NVSConfig::loadAdsbServer());
#endif

    // Home is a fixed reference, independent from the movable map centre.
    NVSConfig::HomeData home = NVSConfig::loadHome();
    if (!home.valid) {
        const NVSConfig::LocationData loc = NVSConfig::loadLocation();
        home.lat = loc.valid ? static_cast<double>(loc.lat)
                             : HOME_FALLBACK_LAT;
        home.lon = loc.valid ? static_cast<double>(loc.lon)
                             : HOME_FALLBACK_LON;
        NVSConfig::saveHome(home.lat, home.lon);
        home.valid = true;
        Serial.printf("[home] initialized from %s: %.7f, %.7f\n",
                      loc.valid ? "IP geolocation" : "fallback",
                      home.lat, home.lon);
    } else {
        Serial.printf("[home] restored: %.7f, %.7f\n",
                      home.lat, home.lon);
    }
    homeLatitude = home.lat;
    homeLongitude = home.lon;
#if AIRCRAFT_LIVE_DATA
    aircraftOverlay.setHomeLocation(homeLatitude, homeLongitude);
#endif

    // ── Post-boot LittleFS mount (normal operation) ───────────────────────────
    if (!LittleFS.begin(false, "/littlefs", 10, "littlefs")) {
        Serial.println("[map] ERROR: LittleFS remount failed.");
        show_map_error("LittleFS failed");
        return;
    }

    MapSnapshot::Metadata metadata;
    if (MapSnapshot::load(LittleFS, metadata, mapViewport)) {
        // Saved snapshot found — restore its parameters and the stored pixels.
        mapRequest.centerLat = metadata.centerLat;
        mapRequest.centerLon = metadata.centerLon;
        mapRequest.radiusKm  = metadata.radiusKm;
        mapReady             = true;
        savedMapAvailable    = true;
        SatelliteMap::captureBackground();
        Serial.println("[map] restored saved map; tap for actions");

    } else {
        // No saved map — download a fresh one.  Use the IP-geolocated position
        // from NVS if available; otherwise keep the compile-time fallback.
        savedMapAvailable = false;
        const NVSConfig::LocationData loc = NVSConfig::loadLocation();
        if (loc.valid) {
            mapRequest.centerLat = loc.lat;
            mapRequest.centerLon = loc.lon;
            mapRequest.radiusKm  = MAP_GEO_RADIUS_KM;
            Serial.printf("[map] using IP-geolocated centre: lat=%.4f  lon=%.4f  radius=%.0f km\n",
                          loc.lat, loc.lon, static_cast<double>(MAP_GEO_RADIUS_KM));
        }
        draw_map();
    }
}

void loop() {
    if (mapReady && interactionMode == InteractionMode::Normal) {
#if AIRCRAFT_LIVE_DATA
        // update() returns true only when it actually erased and redrew aircraft.
        // Only then do we need to hide the home dot first (so aircraft erase
        // can't leave stale pixels under it) and put it back on top afterwards.
        const bool aircraftRedrew = aircraftOverlay.update(mapViewport);
        if (aircraftRedrew) homeMarker.hide();
#else
        aircraftDemo.update();
#endif
        homeMarker.show(mapViewport, homeLatitude, homeLongitude);
        if (infoBarsVisible) {
            altitudeLegend.show();
#if AIRCRAFT_LIVE_DATA
            aircraftStatusBanner.show(aircraftOverlay.status());
#endif
        } else {
            altitudeLegend.hide();
            aircraftStatusBanner.hide();
        }
    } else {
        homeMarker.hide();
#if AIRCRAFT_LIVE_DATA
        aircraftOverlay.pause();
#else
        aircraftDemo.pause();
#endif
        altitudeLegend.hide();
        aircraftStatusBanner.hide();
    }

    int32_t x = 0;
    int32_t y = 0;
    const bool touching = tft.getTouch(&x, &y);

    if (touching) {
        // Track last known touch position (clamped to screen bounds).
        touchX       = constrain(x, 0, 799);
        touchY       = constrain(y, 0, 479);
        touchWasDown = true;
    } else if (touchWasDown) {
        // Finger just lifted — process the tap with a 300 ms debounce to
        // prevent double-triggering from a single physical touch.
        touchWasDown = false;

        if (mapReady && millis() - lastTapMs >= 300) {
            lastTapMs = millis();

            if (interactionMode == InteractionMode::Normal) {
#if AIRCRAFT_LIVE_DATA
                AircraftLive::Selection selection;
                if (aircraftOverlay.selectAt(
                        touchX, touchY, selection)) {
                    selectedAircraft = selection;
                    altitudeLegend.hide();
                    aircraftStatusBanner.hide();
                    homeMarker.hide();
                    aircraftOverlay.pause();
                    if (aircraftInfoPage.open(
                        selection,
                            homeLatitude,
                            homeLongitude)) {
                        interactionMode = InteractionMode::AircraftInfo;
                    }
                } else {
                    homeMarker.hide();
                    aircraftOverlay.pause();
                    open_action_menu();
                }
#else
                aircraftDemo.pause();
                open_action_menu();
#endif

            } else if (interactionMode == InteractionMode::Menu) {
                handle_menu_action(touchX, touchY);

            } else if (interactionMode == InteractionMode::Zoom) {
                handle_zoom_action(touchX, touchY);

            } else if (interactionMode == InteractionMode::Recenter) {
                double newLat = 0.0;
                double newLon = 0.0;
                if (SatelliteMap::screenToLatLon(mapViewport, touchX, touchY,
                                                  newLat, newLon)) {
                    Serial.printf("[touch] screen=%ld,%ld → lat=%.6f  lon=%.6f\n",
                                  static_cast<long>(touchX), static_cast<long>(touchY),
                                  newLat, newLon);

                    mark_selected_point(touchX, touchY);
                    delay(180);  // brief crosshair flash so the user sees the tap

                    const double previousLat = mapRequest.centerLat;
                    const double previousLon = mapRequest.centerLon;
                    mapRequest.centerLat = newLat;
                    mapRequest.centerLon = newLon;
                    interactionMode = InteractionMode::Normal;

                    if (!draw_map_dark()) {
                        // Network error — roll back to the previous position
                        mapRequest.centerLat = previousLat;
                        mapRequest.centerLon = previousLon;
                    }
                }

            } else if (interactionMode == InteractionMode::AircraftInfo) {
                if (aircraftPicturePage.open(selectedAircraft)) {
                    interactionMode = InteractionMode::AircraftPicture;
                }

            } else if (interactionMode == InteractionMode::AircraftPicture) {
                if (aircraftStatisticsPage.open()) {
                    interactionMode = InteractionMode::AircraftStatistics;
                }

            } else if (interactionMode ==
                       InteractionMode::AircraftStatistics) {
                aircraftStatisticsPage.close();
                aircraftPicturePage.close();
                aircraftInfoPage.close();
                interactionMode = InteractionMode::Normal;
            }
        }
    }

    delay(10);
}

// ═════════════════════════════════════════════════════════════════════════════
// Map rendering
// ═════════════════════════════════════════════════════════════════════════════

// Downloads and renders the map defined by mapRequest.
// Sets mapReady = true on success; shows an error screen on failure.
static bool draw_map() {
    altitudeLegend.hide();
    aircraftStatusBanner.hide();
    homeMarker.hide();
#if AIRCRAFT_LIVE_DATA
    aircraftOverlay.pause();
#else
    aircraftDemo.pause();
#endif
    mapReady = false;
    SatelliteMap::Result result =
        SatelliteMap::drawEsriWorldImagery(mapRequest, LittleFS);

    Serial.printf("[map] result=%s  zoom=%u  tiles=%u/%u  bytes=%u  http=%d\n",
                  SatelliteMap::errorToString(result.error),
                  result.zoom, result.tilesDrawn, result.tilesRequested,
                  static_cast<unsigned>(result.bytesWritten), result.httpCode);

    if (!result.ok()) {
        Serial.printf("[map] last URL: %s\n", result.lastUrl.c_str());
        show_map_error(SatelliteMap::errorToString(result.error));
        return false;
    }

    mapViewport      = result.viewport;
    mapReady         = true;
    interactionMode  = InteractionMode::Normal;
    Serial.printf("[map] ready — centre=%.6f,%.6f; tap for actions\n",
                  mapRequest.centerLat, mapRequest.centerLon);
    return true;
}

// Wraps draw_map() with a "screen will go dark" notice and backlight control.
// Used for zoom and re-centre operations where the download may take several
// seconds — showing the notice first sets user expectations.
static bool draw_map_dark() {
    constexpr int32_t kOvX = 150, kOvY = 178, kOvW = 500, kOvH = 124;
    constexpr int32_t kOvCX = kOvX + kOvW / 2;

    tft.fillRoundRect(kOvX, kOvY, kOvW, kOvH, 8, 0x18E3);
    tft.drawRoundRect(kOvX, kOvY, kOvW, kOvH, 8, TFT_WHITE);
    tft.setTextDatum(MC_DATUM);
    tft.setFont(&fonts::DejaVu24);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Building new map...", kOvCX, kOvY + 42);
    tft.setFont(&fonts::DejaVu18);
    tft.setTextColor(0xC618);
    tft.drawString("Screen will go dark while building.", kOvCX, kOvY + 82);
    tft.setTextDatum(TL_DATUM);

    delay(1200);  // pause so the user can read the message
    display_set_backlight(false);
    const bool ok = draw_map();
    display_set_backlight(true);
    return ok;
}

// Draws a yellow crosshair + circle at the tapped point.
// The caller should delay ~180 ms afterwards to let the user see it.
static void mark_selected_point(int32_t x, int32_t y) {
    constexpr int32_t arm = 10;
    tft.drawCircle(x, y, 7, TFT_YELLOW);
    tft.drawFastHLine(max(0, x - arm),
                      y,
                      min(arm * 2 + 1, 800 - max(0, x - arm)),
                      TFT_YELLOW);
    tft.drawFastVLine(x,
                      max(0, y - arm),
                      min(arm * 2 + 1, 480 - max(0, y - arm)),
                      TFT_YELLOW);
}

// ═════════════════════════════════════════════════════════════════════════════
// Overlay core
// ═════════════════════════════════════════════════════════════════════════════
// Both the action menu and the zoom dialog use the same open/close mechanism:
//   open  — PSRAM-allocate menuBackup, readRect() the covered pixels, draw overlay
//   close — pushImage() the backup back, free PSRAM, set next interaction mode

static bool open_action_menu() {
    if (interactionMode == InteractionMode::Menu) return true;

    overlayX = kMenuX;
    overlayY = kMenuY;
    overlayW = kMenuW;
    overlayH = kMenuH;

    const size_t pixels = static_cast<size_t>(kMenuW) * kMenuH;
    menuBackup = static_cast<lgfx::rgb565_t*>(
        ps_malloc(pixels * sizeof(lgfx::rgb565_t)));
    if (!menuBackup) {
        Serial.println("[menu] ERROR: PSRAM allocation failed");
        return false;
    }

    tft.readRect(overlayX, overlayY, overlayW, overlayH, menuBackup);
    interactionMode = InteractionMode::Menu;
    draw_action_menu();
    return true;
}

static void close_action_menu() {
    close_overlay();
}

static void close_overlay(InteractionMode nextMode) {
    if (!menuBackup) {
        interactionMode = nextMode;
        return;
    }
    tft.pushImage(overlayX, overlayY, overlayW, overlayH, menuBackup);
    free(menuBackup);
    menuBackup = nullptr;
    overlayX   = 0;
    overlayY   = 0;
    overlayW   = 0;
    overlayH   = 0;
    interactionMode = nextMode;
}

// ═════════════════════════════════════════════════════════════════════════════
// Map action menu
// ═════════════════════════════════════════════════════════════════════════════

// Draws a single action button with a 3D bevel effect.
//   disabled — grayed out face and text (used for Delete when no snapshot exists)
//   pressed  — face darkened and bevel inverted; text offset +1 px each axis
//
// The bevel uses four single-pixel lines:
//   top + left  → kBtnHi (lighter, simulates light from top-left)
//   bottom + right → kBtnSh (darker, simulates shadow below)
// Swapping hi/sh when pressed gives a "pushed in" appearance.
static void draw_button(int32_t x, int32_t y, int32_t width,
                        const char* label,
                        bool disabled = false, bool pressed = false) {
    const uint32_t face = disabled ? kBtnDisFace : (pressed ? kBtnSh : kBtnFace);
    tft.fillRoundRect(x, y, width, kButtonH, 6, face);

    if (!disabled) {
        const uint32_t hiC = pressed ? kBtnSh : kBtnHi;
        const uint32_t shC = pressed ? kBtnHi : kBtnSh;
        tft.drawFastHLine(x + 7, y + 2,            width - 14, hiC); // top
        tft.drawFastVLine(x + 2, y + 7,            kButtonH - 14, hiC); // left
        tft.drawFastHLine(x + 7, y + kButtonH - 3, width - 14, shC); // bottom
        tft.drawFastVLine(x + width - 3, y + 7,    kButtonH - 14, shC); // right
    }
    tft.drawRoundRect(x, y, width, kButtonH, 6, kBtnSh); // outer border

    tft.setFont(&fonts::DejaVu18);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(disabled ? kBtnDisText : kBtnText);
    tft.drawString(label,
                   x + width / 2 + (pressed ? 1 : 0),
                   y + kButtonH / 2 + (pressed ? 1 : 0));
}

static void draw_action_menu() {
    tft.fillRoundRect(kMenuX, kMenuY, kMenuW, kMenuH, 8, 0x18E3);
    tft.drawRoundRect(kMenuX, kMenuY, kMenuW, kMenuH, 8, TFT_WHITE);

    draw_button(kFirstButtonX, kButtonY, kButtonW, "Save");
    draw_button(kFirstButtonX +   (kButtonW + kButtonGap),
                kButtonY, kButtonW, "Delete", !savedMapAvailable);
    draw_button(kFirstButtonX + 2*(kButtonW + kButtonGap),
                kButtonY, kButtonW, "Re-center");
    draw_button(kFirstButtonX + 3*(kButtonW + kButtonGap),
                kButtonY, kButtonW, "Zoom");
    draw_button(kFitButtonX, kSecondButtonY, kWideButtonW, "Fit aircraft");
    draw_button(kInfoButtonX, kSecondButtonY, kWideButtonW,
                infoBarsVisible ? "Hide info bars" : "Show info bars");
#if AIRCRAFT_LIVE_DATA
    draw_button(kTracksButtonX, kSecondButtonY, kWideButtonW,
                aircraftOverlay.tracksVisible()
                    ? "Hide tracks" : "Show tracks",
                !aircraftOverlay.tracksAvailable());
#else
    draw_button(kTracksButtonX, kSecondButtonY, kWideButtonW,
                "Show tracks", true);
#endif
    draw_button(kHomeButtonX, kSecondButtonY, kWideButtonW,
                "Set home");
    draw_button(kWiFiButtonX, kThirdButtonY, kThirdButtonW, "WiFi");
    draw_button(kAdsbButtonX, kThirdButtonY, kAdsbButtonW, "ADSB Server");
    tft.setTextDatum(TL_DATUM);
}

// Shows a two-line status overlay in the same style as the Save result popup.
static void show_status_overlay(const char* title, const char* subtitle, uint32_t color) {
    constexpr int32_t kOvX = 150, kOvY = 178, kOvW = 500, kOvH = 124;
    constexpr int32_t kOvCX   = kOvX + kOvW / 2;
    constexpr int32_t kLine1Y = kOvY + 42;
    constexpr int32_t kLine2Y = kOvY + 82;

    const size_t ovPixels = static_cast<size_t>(kOvW) * kOvH;
    auto* ovBackup = static_cast<lgfx::rgb565_t*>(
        ps_malloc(ovPixels * sizeof(lgfx::rgb565_t)));
    if (ovBackup) tft.readRect(kOvX, kOvY, kOvW, kOvH, ovBackup);

    tft.fillRoundRect(kOvX, kOvY, kOvW, kOvH, 8, 0x18E3);
    tft.drawRoundRect(kOvX, kOvY, kOvW, kOvH, 8, color);
    tft.setFont(&fonts::DejaVu24);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(color);
    tft.drawString(title, kOvCX, kLine1Y);
    tft.setFont(&fonts::DejaVu18);
    tft.setTextColor(0xC618);
    tft.drawString(subtitle, kOvCX, kLine2Y);
    tft.setTextDatum(TL_DATUM);
    delay(900);

    if (ovBackup) {
        tft.pushImage(kOvX, kOvY, kOvW, kOvH, ovBackup);
        free(ovBackup);
    }
}

// Saves the clean map framebuffer and gives visual feedback while flash writes
// temporarily stall the RGB panel's access to PSRAM.
static bool save_current_map(bool showProgress) {
    constexpr int32_t kOvX = 150, kOvY = 178, kOvW = 500, kOvH = 124;
    constexpr int32_t kOvCX = kOvX + kOvW / 2;
    constexpr int32_t kLine1Y = kOvY + 42;
    constexpr int32_t kLine2Y = kOvY + 82;

    const size_t ovPixels = static_cast<size_t>(kOvW) * kOvH;
    auto* ovBackup = static_cast<lgfx::rgb565_t*>(
        ps_malloc(ovPixels * sizeof(lgfx::rgb565_t)));

    bool backlightOff = false;
    bool overlayDrawn = false;

    const bool saved = MapSnapshot::save(
        LittleFS,
        mapRequest.centerLat,
        mapRequest.centerLon,
        mapRequest.radiusKm,
        mapViewport,
        [&](float /*progress*/) {
            if (overlayDrawn) return;
            overlayDrawn = true;
            if (showProgress) {
                if (ovBackup) {
                    tft.readRect(kOvX, kOvY, kOvW, kOvH, ovBackup);
                }
                tft.fillRoundRect(kOvX, kOvY, kOvW, kOvH, 8, 0x18E3);
                tft.drawRoundRect(kOvX, kOvY, kOvW, kOvH, 8, TFT_WHITE);
                tft.setTextDatum(MC_DATUM);
                tft.setFont(&fonts::DejaVu24);
                tft.setTextColor(TFT_WHITE);
                tft.drawString("Saving map...", kOvCX, kLine1Y);
                tft.setFont(&fonts::DejaVu18);
                tft.setTextColor(0xC618);
                tft.drawString("Screen will go dark while saving.", kOvCX, kLine2Y);
                tft.setTextDatum(TL_DATUM);
                delay(1200);
            }
            display_set_backlight(false);
            backlightOff = true;
        });

    if (backlightOff) display_set_backlight(true);

    if (showProgress && overlayDrawn) {
        const uint32_t color = saved ? TFT_WHITE : TFT_RED;
        tft.fillRoundRect(kOvX, kOvY, kOvW, kOvH, 8, 0x18E3);
        tft.drawRoundRect(kOvX, kOvY, kOvW, kOvH, 8, color);
        tft.setFont(&fonts::DejaVu24);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(color);
        tft.drawString(saved ? "Map saved" : "Save failed",
                       kOvCX, kLine1Y);
        tft.setFont(&fonts::DejaVu18);
        tft.setTextColor(0xC618);
        tft.drawString(saved ? "Snapshot stored successfully."
                             : "The snapshot could not be stored.",
                       kOvCX, kLine2Y);
        tft.setTextDatum(TL_DATUM);
        delay(900);
    }

    if (showProgress && overlayDrawn && ovBackup) {
        tft.pushImage(kOvX, kOvY, kOvW, kOvH, ovBackup);
    }
    if (ovBackup) free(ovBackup);

    savedMapAvailable = saved || MapSnapshot::exists(LittleFS);
    if (!showProgress && !saved) {
        show_status_overlay("Auto-save failed", "The snapshot could not be stored.", TFT_RED);
    }
    return saved;
}

// Maps a screen tap coordinate to a MenuAction.
static MenuAction menu_action_at(int32_t x, int32_t y) {
    if (y >= kButtonY && y < kButtonY + kButtonH) {
        for (int32_t i = 0; i < 4; i++) {
            const int32_t left = kFirstButtonX + i * (kButtonW + kButtonGap);
            if (x >= left && x < left + kButtonW) {
                return static_cast<MenuAction>(i + 1);
            }
        }
    }
    if (y >= kSecondButtonY && y < kSecondButtonY + kButtonH) {
        if (x >= kFitButtonX && x < kFitButtonX + kWideButtonW) {
            return MenuAction::FitAircraft;
        }
        if (x >= kInfoButtonX && x < kInfoButtonX + kWideButtonW) {
            return MenuAction::ToggleInfoBars;
        }
        if (x >= kTracksButtonX &&
            x < kTracksButtonX + kWideButtonW) {
            return MenuAction::ToggleTracks;
        }
        if (x >= kHomeButtonX &&
            x < kHomeButtonX + kWideButtonW) {
            return MenuAction::SetHome;
        }
    }
    if (y >= kThirdButtonY && y < kThirdButtonY + kButtonH) {
        if (x >= kWiFiButtonX && x < kWiFiButtonX + kThirdButtonW) {
            return MenuAction::WiFiSettings;
        }
        if (x >= kAdsbButtonX && x < kAdsbButtonX + kAdsbButtonW) {
            return MenuAction::AdsbServer;
        }
    }
    return MenuAction::None;
}

static void handle_menu_action(int32_t x, int32_t y) {
    const MenuAction action = menu_action_at(x, y);

    // Tap outside any button but still within the panel → ignore.
    // Tap outside the panel entirely → close the menu.
    if (action == MenuAction::None) {
        if (x < kMenuX || x >= kMenuX + kMenuW ||
            y < kMenuY || y >= kMenuY + kMenuH) {
            close_action_menu();
        }
        return;
    }

    // 90 ms press flash: darken the tapped button briefly before executing.
    // This gives tactile-style feedback without requiring a physical click.
    static const char* kBtnLabels[4] = {
        "Save", "Delete", "Re-center", "Zoom"
    };
    const bool isFitAction = action == MenuAction::FitAircraft;
    const bool isInfoAction = action == MenuAction::ToggleInfoBars;
    const bool isTracksAction = action == MenuAction::ToggleTracks;
    const bool isHomeAction = action == MenuAction::SetHome;
    const bool isAdsbAction   = action == MenuAction::AdsbServer;
    const bool isWiFiAction   = action == MenuAction::WiFiSettings;
    const bool isRow2Action =
        isFitAction || isInfoAction || isTracksAction || isHomeAction;
    const int32_t btnIdx = static_cast<int32_t>(action) - 1;
    const int32_t btnX = isWiFiAction ? kWiFiButtonX
        : isAdsbAction ? kAdsbButtonX
        : isRow2Action
            ? (isFitAction
                ? kFitButtonX
                : (isInfoAction
                    ? kInfoButtonX
                    : (isTracksAction ? kTracksButtonX : kHomeButtonX)))
            : kFirstButtonX + btnIdx * (kButtonW + kButtonGap);
    const int32_t btnY = (isAdsbAction || isWiFiAction) ? kThirdButtonY
        : (isRow2Action ? kSecondButtonY : kButtonY);
    const int32_t btnW = (isAdsbAction || isWiFiAction) ? kThirdButtonW
        : (isRow2Action ? kWideButtonW : kButtonW);
    const char* btnLabel = isFitAction
        ? "Fit aircraft"
        : (isInfoAction
            ? (infoBarsVisible ? "Hide info bars" : "Show info bars")
            : (isHomeAction
                ? "Set home"
                : (isAdsbAction
                    ? "ADSB Server"
                    : (isWiFiAction
                    ? "WiFi"
                    : (isTracksAction
#if AIRCRAFT_LIVE_DATA
                    ? (aircraftOverlay.tracksVisible()
                        ? "Hide tracks" : "Show tracks")
#else
                    ? "Show tracks"
#endif
                    : kBtnLabels[btnIdx])))));
    const bool btnDis =
        (action == MenuAction::Delete && !savedMapAvailable)
#if AIRCRAFT_LIVE_DATA
        || (isTracksAction && !aircraftOverlay.tracksAvailable())
#else
        || isTracksAction
#endif
        ;
    draw_button(btnX, btnY, btnW, btnLabel, btnDis, true);
    delay(90);
    draw_button(btnX, btnY, btnW, btnLabel, btnDis, false);

    if (action == MenuAction::ToggleInfoBars) {
        infoBarsVisible = !infoBarsVisible;
        close_action_menu();
        Serial.printf("[ui] info bars %s\n",
                      infoBarsVisible ? "shown" : "hidden");
        return;
    }

    if (action == MenuAction::ToggleTracks) {
#if AIRCRAFT_LIVE_DATA
        if (!aircraftOverlay.tracksAvailable()) return;
        aircraftOverlay.setTracksVisible(
            !aircraftOverlay.tracksVisible());
        close_action_menu();
        Serial.printf("[ui] aircraft tracks %s\n",
                      aircraftOverlay.tracksVisible()
                          ? "shown" : "hidden");
#endif
        return;
    }

    if (action == MenuAction::SetHome) {
        close_action_menu();

        double newHomeLatitude = homeLatitude;
        double newHomeLongitude = homeLongitude;
        if (FreshSetup::promptHome(
                homeLatitude, homeLongitude,
                newHomeLatitude, newHomeLongitude)) {
            homeLatitude = newHomeLatitude;
            homeLongitude = newHomeLongitude;
            NVSConfig::saveHome(homeLatitude, homeLongitude);
#if AIRCRAFT_LIVE_DATA
            aircraftOverlay.setHomeLocation(
                homeLatitude, homeLongitude);
#endif
            Serial.printf(
                "[home] updated from coordinate editor: %.7f, %.7f\n",
                homeLatitude, homeLongitude);
        }

        // The coordinate editor uses LVGL and clears the display on exit.
        // Restore the clean map from PSRAM in supported 64x64 tiles.
        for (int32_t y = 0; y < 480; y += 64) {
            const int32_t height = min(64, 480 - y);
            for (int32_t x = 0; x < 800; x += 64) {
                const int32_t width = min(64, 800 - x);
                SatelliteMap::restoreBackground(
                    x, y, width, height);
            }
        }
        interactionMode = InteractionMode::Normal;
        return;
    }

    if (action == MenuAction::WiFiSettings) {
        close_action_menu();

        char ssid[64] = {0};
        char password[64] = {0};
        if (FreshSetup::promptWiFi(ssid, sizeof(ssid), password, sizeof(password))) {
            NVSConfig::saveWiFi(ssid, password);
            tft.fillScreen(TFT_BLACK);
            tft.setFont(&fonts::DejaVu24);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(TFT_WHITE);
            tft.drawString("WiFi updated - rebooting...", 400, 240);
            tft.setTextDatum(TL_DATUM);
            delay(2000);
            ESP.restart();
        }

        // User did not complete the flow — restore the map.
        for (int32_t y = 0; y < 480; y += 64) {
            const int32_t height = min(64, 480 - y);
            for (int32_t x = 0; x < 800; x += 64) {
                const int32_t width = min(64, 800 - x);
                SatelliteMap::restoreBackground(x, y, width, height);
            }
        }
        interactionMode = InteractionMode::Normal;
        return;
    }

    if (action == MenuAction::AdsbServer) {
        close_action_menu();

        char url[128];
        const String current = NVSConfig::loadAdsbServer();
        if (FreshSetup::promptAdsbServer(url, sizeof(url), current.c_str())) {
            NVSConfig::saveAdsbServer(url);
#if AIRCRAFT_LIVE_DATA
            aircraftOverlay.setServerBase(String(url));
#endif
            Serial.printf("[adsb] server base updated: %s\n", url);
        }

        for (int32_t y = 0; y < 480; y += 64) {
            const int32_t height = min(64, 480 - y);
            for (int32_t x = 0; x < 800; x += 64) {
                const int32_t width = min(64, 800 - x);
                SatelliteMap::restoreBackground(x, y, width, height);
            }
        }
        interactionMode = InteractionMode::Normal;
        return;
    }

    if (action == MenuAction::Recenter) {
        close_action_menu();
        interactionMode = InteractionMode::Recenter;
        // Small hint label at the top of the screen
        tft.fillRoundRect(275, 12, 250, 42, 6, 0x18E3);
        tft.drawRoundRect(275, 12, 250, 42, 6, TFT_YELLOW);
        tft.setFont(&fonts::DejaVu18);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_YELLOW);
        tft.drawString("Tap the new center", 400, 33);
        tft.setTextDatum(TL_DATUM);
        return;
    }

    if (action == MenuAction::Zoom) {
        close_action_menu();
        open_zoom_dialog();
        return;
    }

    if (action == MenuAction::FitAircraft) {
#if AIRCRAFT_LIVE_DATA
        double fittedLat = 0.0;
        double fittedLon = 0.0;
        float fittedRadiusKm = 0.0f;
        size_t fittedAircraft = 0;
        if (!aircraftOverlay.fitMapRequest(
                fittedLat, fittedLon, fittedRadiusKm, fittedAircraft)) {
            close_action_menu();
            show_status_overlay("No live aircraft", "No ADS-B aircraft in range.", TFT_YELLOW);
            return;
        }

        const double previousLat = mapRequest.centerLat;
        const double previousLon = mapRequest.centerLon;
        const float previousRadius = mapRequest.radiusKm;

        close_action_menu();
        mapRequest.centerLat = fittedLat;
        mapRequest.centerLon = fittedLon;
        mapRequest.radiusKm = fittedRadiusKm;
        Serial.printf(
            "[adsb-fit] fitting %u aircraft: center=%.6f,%.6f radius=%.3f km\n",
            static_cast<unsigned>(fittedAircraft),
            fittedLat, fittedLon, fittedRadiusKm);

        if (!draw_map_dark()) {
            mapRequest.centerLat = previousLat;
            mapRequest.centerLon = previousLon;
            mapRequest.radiusKm = previousRadius;
            Serial.println("[adsb-fit] redraw failed — restoring previous map");
            draw_map_dark();
            return;
        }

        save_current_map(false);
#else
        close_action_menu();
        show_status_overlay("Live aircraft disabled", "Built without live data support.", TFT_YELLOW);
#endif
        return;
    }

    // Close the menu before the long-running operations below so the map is
    // fully restored before we capture or overwrite it.
    close_action_menu();

    if (action == MenuAction::Save) {
        save_current_map();
        return;
    }

    if (action == MenuAction::Delete) {
        if (!savedMapAvailable) {
            show_status_overlay("No saved map", "Nothing to remove.", TFT_YELLOW);
            return;
        }
        const bool removed = MapSnapshot::remove(LittleFS);
        savedMapAvailable  = !removed && MapSnapshot::exists(LittleFS);
        show_status_overlay(
            removed ? "Map deleted"   : "Delete failed",
            removed ? "The snapshot has been removed."
                    : "The snapshot could not be removed.",
            removed ? TFT_WHITE : TFT_RED);
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Zoom / radius dialog
// ═════════════════════════════════════════════════════════════════════════════

// Strips trailing zeros after a decimal point so "50.000" displays as "50",
// and "0.039" stays as "0.039".
static void trim_number(String& value) {
    while (value.indexOf('.') >= 0 && value.endsWith("0")) {
        value.remove(value.length() - 1);
    }
    if (value.endsWith(".")) value.remove(value.length() - 1);
    if (value.length() == 0) value = "0";
}

static String format_radius_value(float value) {
    String result(value, value < 10.0f ? 3 : 1);
    trim_number(result);
    return result;
}

static bool open_zoom_dialog() {
    if (interactionMode == InteractionMode::Zoom) return true;

    overlayX = kZoomX;
    overlayY = kZoomY;
    overlayW = kZoomW;
    overlayH = kZoomH;

    const size_t pixels = static_cast<size_t>(overlayW) * overlayH;
    menuBackup = static_cast<lgfx::rgb565_t*>(
        ps_malloc(pixels * sizeof(lgfx::rgb565_t)));
    if (!menuBackup) {
        Serial.println("[zoom] ERROR: PSRAM allocation failed");
        interactionMode = InteractionMode::Normal;
        return false;
    }

    tft.readRect(overlayX, overlayY, overlayW, overlayH, menuBackup);

    // Initialise the input field with the current radius in whichever unit
    // makes more sense (km for ≥ 1 km, metres otherwise).
    zoomUnitKm = mapRequest.radiusKm >= 1.0f;
    const float displayedValue =
        zoomUnitKm ? mapRequest.radiusKm : mapRequest.radiusKm * 1000.0f;
    zoomInput = format_radius_value(displayedValue);

    interactionMode = InteractionMode::Zoom;
    draw_zoom_dialog();
    return true;
}

// Draws a single key in the numpad or action row using the same 3D bevel style
// as the action-menu buttons (shared kBtn* palette).
static void draw_zoom_key(int32_t x, int32_t y, int32_t width, int32_t height,
                          const char* label, bool pressed = false) {
    const uint32_t face = pressed ? kBtnSh : kBtnFace;
    tft.fillRoundRect(x, y, width, height, 6, face);

    const uint32_t hiC = pressed ? kBtnSh : kBtnHi;
    const uint32_t shC = pressed ? kBtnHi : kBtnSh;
    tft.drawFastHLine(x + 7, y + 2,           width  - 14, hiC);
    tft.drawFastVLine(x + 2, y + 7,           height - 14, hiC);
    tft.drawFastHLine(x + 7, y + height - 3,  width  - 14, shC);
    tft.drawFastVLine(x + width - 3, y + 7,   height - 14, shC);
    tft.drawRoundRect(x, y, width, height, 6, kBtnSh);

    tft.setFont(&fonts::DejaVu18);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(kBtnText);
    tft.drawString(label,
                   x + width  / 2 + (pressed ? 1 : 0),
                   y + height / 2 + (pressed ? 1 : 0));
}

static void draw_zoom_dialog() {
    tft.fillRoundRect(kZoomX, kZoomY, kZoomW, kZoomH, 8, 0x18E3);
    tft.drawRoundRect(kZoomX, kZoomY, kZoomW, kZoomH, 8, TFT_WHITE);

    tft.setFont(&fonts::DejaVu24);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_WHITE);
    tft.drawString("Map radius", 400, 68);

    // ── Value display + unit toggle ───────────────────────────────────────────
    tft.fillRoundRect(105, 93, 430, 65, 6, TFT_BLACK);
    tft.drawRoundRect(105, 93, 430, 65, 6, TFT_WHITE);
    tft.setFont(&fonts::DejaVu24);
    tft.setTextColor(TFT_WHITE);
    tft.drawString(zoomInput, 320, 125);
    draw_zoom_key(550, 93, 145, 65, zoomUnitKm ? "km" : "m");

    // ── 3 × 4 numpad ─────────────────────────────────────────────────────────
    static const char* keys[12] = {
        "1", "2", "3",
        "4", "5", "6",
        "7", "8", "9",
        ".", "0", "<"
    };
    for (int32_t i = 0; i < 12; i++) {
        const int32_t col = i % 3;
        const int32_t row = i / 3;
        draw_zoom_key(kKeyStartX + col * (kKeyW + kKeyGap),
                      kKeyStartY + row * (kKeyH + 8),
                      kKeyW, kKeyH, keys[i]);
    }

    // ── Action buttons (right column, aligned with numpad top) ───────────────
    draw_zoom_key(435, 178, 230, 62, "Apply");
    draw_zoom_key(435, 250, 230, 62, "Clear");
    draw_zoom_key(435, 322, 230, 62, "Cancel");

    tft.setTextDatum(TL_DATUM);
}

// Returns true if screen point (x, y) is within the rectangle
// (left, top, width, height).
static bool point_in_rect(int32_t x, int32_t y,
                           int32_t left, int32_t top,
                           int32_t width, int32_t height) {
    return x >= left && x < left + width &&
           y >= top  && y < top  + height;
}

static void handle_zoom_action(int32_t x, int32_t y) {

    // ── Unit toggle (km ↔ m) ──────────────────────────────────────────────────
    if (point_in_rect(x, y, 550, 93, 145, 65)) {
        draw_zoom_key(550, 93, 145, 65, zoomUnitKm ? "km" : "m", true);
        delay(90);
        float value = zoomInput.toFloat();
        if (value <= 0.0f) {
            // Guard against toggling when the field is empty or "Invalid"
            value = zoomUnitKm ? mapRequest.radiusKm : mapRequest.radiusKm * 1000.0f;
        }
        value      = zoomUnitKm ? value * 1000.0f : value / 1000.0f;
        zoomUnitKm = !zoomUnitKm;
        zoomInput  = format_radius_value(value);
        draw_zoom_dialog();
        return;
    }

    // ── Cancel ────────────────────────────────────────────────────────────────
    if (point_in_rect(x, y, 435, 322, 230, 62)) {
        draw_zoom_key(435, 322, 230, 62, "Cancel", true);
        delay(90);
        close_overlay();
        return;
    }

    // ── Clear ─────────────────────────────────────────────────────────────────
    if (point_in_rect(x, y, 435, 250, 230, 62)) {
        draw_zoom_key(435, 250, 230, 62, "Clear", true);
        delay(90);
        zoomInput = "";
        draw_zoom_dialog();
        return;
    }

    // ── Apply ─────────────────────────────────────────────────────────────────
    if (point_in_rect(x, y, 435, 178, 230, 62)) {
        draw_zoom_key(435, 178, 230, 62, "Apply", true);
        delay(90);

        const float entered     = zoomInput.toFloat();
        const float newRadiusKm = zoomUnitKm ? entered : entered / 1000.0f;

        if (entered <= 0.0f || newRadiusKm < 0.001f || newRadiusKm > 5000.0f) {
            zoomInput = "Invalid";
            draw_zoom_dialog();
            delay(650);
            const float displayedValue =
                zoomUnitKm ? mapRequest.radiusKm : mapRequest.radiusKm * 1000.0f;
            zoomInput = format_radius_value(displayedValue);
            draw_zoom_dialog();
            return;
        }

        const float previousRadius = mapRequest.radiusKm;
        close_overlay();
        mapRequest.radiusKm = newRadiusKm;
        Serial.printf("[zoom] radius %.3f → %.3f km\n", previousRadius, mapRequest.radiusKm);

        if (!draw_map_dark()) {
            // Download failed — restore the previous radius and try again so
            // the user sees the old map rather than an error screen.
            mapRequest.radiusKm = previousRadius;
            Serial.println("[zoom] redraw failed — restoring previous radius");
            draw_map_dark();
        }
        return;
    }

    // ── Digit / decimal / backspace keys ──────────────────────────────────────
    static const char* keys[12] = {
        "1", "2", "3",
        "4", "5", "6",
        "7", "8", "9",
        ".", "0", "<"
    };
    for (int32_t i = 0; i < 12; i++) {
        const int32_t col  = i % 3;
        const int32_t row  = i / 3;
        const int32_t keyX = kKeyStartX + col * (kKeyW + kKeyGap);
        const int32_t keyY = kKeyStartY + row * (kKeyH + 8);
        if (!point_in_rect(x, y, keyX, keyY, kKeyW, kKeyH)) continue;

        const String key = keys[i];
        if (key == "<") {
            if (zoomInput.length() > 0) zoomInput.remove(zoomInput.length() - 1);
        } else if (key == ".") {
            if (zoomInput.indexOf('.') < 0 && zoomInput.length() < 9) {
                if (zoomInput.length() == 0) zoomInput = "0";
                zoomInput += ".";
            }
        } else if (zoomInput.length() < 9) {
            if (zoomInput == "0" || zoomInput == "Invalid") zoomInput = "";
            zoomInput += key;
        }
        draw_zoom_dialog();
        return;
    }
}

// ═════════════════════════════════════════════════════════════════════════════
// Startup helpers
// ═════════════════════════════════════════════════════════════════════════════

// Displays the ADS-B splash from LittleFS.  The previous generic splash is
// retained as a fallback so a filesystem built without the new asset still
// starts cleanly.
static bool show_splash() {
    constexpr const char* kPrimarySplash = "/splash_adsb_800x480.jpg";
    constexpr const char* kFallbackSplash = "/splash.jpg";

    Serial.println("[splash] Listing LittleFS root:");
    File root = LittleFS.open("/");
    File f    = root.openNextFile();
    while (f) {
        Serial.printf("  %s  (%u bytes)\n", f.name(), f.size());
        f = root.openNextFile();
    }

    const char* splashPath = LittleFS.exists(kPrimarySplash)
                                 ? kPrimarySplash
                                 : kFallbackSplash;
    if (!LittleFS.exists(splashPath)) {
        Serial.println("[splash] ERROR: no splash image found.");
        return false;
    }
    Serial.printf("[splash] %s found — decoding…\n", splashPath);
    const bool ok =
        tft.drawJpgFile(LittleFS, splashPath, 0, 0, 800, 480);
    Serial.printf("[splash] drawJpgFile: %s\n", ok ? "OK" : "FAILED");
    return ok;
}

// Shows a full-screen error message when the map fails to load.
static void show_map_error(const char* message) {
    tft.fillScreen(TFT_BLACK);
    tft.setFont(&fonts::DejaVu24);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(TFT_RED);
    tft.drawString("Map failed", 400, 210);
    tft.setFont(&fonts::DejaVu18);
    tft.setTextColor(TFT_WHITE);
    tft.drawString(message ? message : "unknown error", 400, 270);
    tft.setTextDatum(TL_DATUM);
}
