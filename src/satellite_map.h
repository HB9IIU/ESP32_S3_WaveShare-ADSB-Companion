#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// satellite_map.h — ESRI World Imagery tile downloader and renderer
//
// Downloads XYZ tiles from ESRI's server, stitches them into a mosaic, and
// renders the result to the 800 × 480 display.  Tiles are fetched one at a
// time via HTTPS, stored temporarily in LittleFS, decoded as JPEG, and drawn
// into an off-screen PSRAM sprite.  When all tiles are ready the sprite is
// pushed to the display in one atomic call to avoid visible tearing.
//
// Coordinate system
//   Tile addressing follows the OSM Slippy Map convention (Web Mercator /
//   EPSG:3857).  Latitude is clamped to ±85.05° (the Mercator projection
//   limit).  All internal world-pixel calculations use double precision to
//   avoid rounding errors at high zoom levels.
// ═════════════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <FS.h>
#include "HB9IIUdisplayInit.h"

namespace SatelliteMap {

// ── Constants ─────────────────────────────────────────────────────────────────

constexpr uint16_t kDefaultWidth  = 800;
constexpr uint16_t kDefaultHeight = 480;
constexpr uint16_t kTileSize      = 256;   // ESRI tiles are always 256 × 256 px

// ESRI World Imagery endpoint — tiles are served as .jpg at zoom/row/col
constexpr const char* kEsriTileBaseUrl =
    "https://server.arcgisonline.com/ArcGIS/rest/services/World_Imagery/MapServer/tile";

// Temporary file used for each downloaded tile (overwritten per tile)
constexpr const char* kDefaultTilePath = "/esri_tile.jpg";

// ── Error codes ───────────────────────────────────────────────────────────────

enum class Error {
    Ok,
    WifiUnavailable,
    ServerUnavailable,
    HttpBeginFailed,
    HttpRequestFailed,
    DownloadFailed,
    FileOpenFailed,
    EmptyImage,
    InvalidJpeg,
    DisplayFailed,
    TileMathFailed,
};

// ── Request ───────────────────────────────────────────────────────────────────

struct Request {
    double      centerLat     = 46.46651;         // WGS-84 decimal degrees
    double      centerLon     = 6.85534;
    float       radiusKm      = 50.0f;            // vertical half-span of the view
    uint16_t    width         = kDefaultWidth;    // output image dimensions (pixels)
    uint16_t    height        = kDefaultHeight;
    const char* tilePath      = kDefaultTilePath; // LittleFS scratch path for tiles
    uint32_t    httpTimeoutMs = 30000;
    uint8_t     minZoom       = 1;                // OSM zoom range; 1 = world, 19 = street
    uint8_t     maxZoom       = 19;
};

// ── Viewport ──────────────────────────────────────────────────────────────────
// Describes the Mercator coordinate system used to render the current map.
// Stored alongside the map so that screen taps can be converted back to
// geographic coordinates via screenToLatLon().

struct Viewport {
    bool     valid   = false;
    uint8_t  zoom    = 0;     // tile zoom level chosen for this render
    uint16_t width   = 0;     // display width (pixels)
    uint16_t height  = 0;     // display height (pixels)
    double   westPx  = 0.0;  // world-pixel X of the left screen edge at this zoom
    double   northPx = 0.0;  // world-pixel Y of the top screen edge
    float    scaleX  = 0.0f; // display pixels per world pixel, horizontal
    float    scaleY  = 0.0f; // display pixels per world pixel, vertical
};

// ── Result ────────────────────────────────────────────────────────────────────

struct Result {
    Error    error          = Error::Ok;
    int      httpCode       = 0;
    size_t   bytesWritten   = 0;
    uint8_t  zoom           = 0;
    uint16_t tilesRequested = 0;
    uint16_t tilesDrawn     = 0;
    String   lastUrl;
    Viewport viewport;

    bool ok() const { return error == Error::Ok; }
};

// ── Public API ────────────────────────────────────────────────────────────────

const char* errorToString(Error error);

// Picks the OSM tile zoom level whose metres-per-pixel most closely matches
// the requested view (log-space comparison to treat all zoom levels fairly).
uint8_t chooseZoom(double centerLat, float metersPerPixel,
                   uint8_t minZoom, uint8_t maxZoom);

// Builds an ESRI tile URL in the form <base>/<zoom>/<row>/<col>
String buildTileUrl(uint8_t zoom, uint32_t tileX, uint32_t tileY);

// Inverts the Mercator projection to map a screen pixel back to lat/lon.
// Returns false when the viewport is invalid or the point is out of bounds.
bool screenToLatLon(const Viewport& viewport,
                    int32_t screenX, int32_t screenY,
                    double& latitude, double& longitude);

// Projects a WGS-84 latitude/longitude onto the current rendered map.
// Returns false when the viewport is invalid or the point is off-screen.
bool latLonToScreen(const Viewport& viewport,
                    double latitude, double longitude,
                    int32_t& screenX, int32_t& screenY);

// Downloads all tiles needed to fill the requested area and renders them to
// the display.  Blocks until complete or until an error occurs.
Result drawEsriWorldImagery(const Request& request, fs::FS& fs);

// Returns true when a clean map background is held in PSRAM and can be used
// by restoreBackground() to repair aircraft overlay pixels without readRect.
bool hasBackground();

// Copies the clean map pixels covering (x, y, w, h) from the PSRAM background
// canvas back to the TFT framebuffer.  No-op when hasBackground() is false.
// Caller must ensure w*h <= 64*64 (the internal scratch buffer size).
void restoreBackground(int32_t x, int32_t y, int32_t w, int32_t h);

// Reads the current TFT display content into the PSRAM background canvas so
// that restoreBackground() works after a map snapshot is loaded directly onto
// the display (bypassing drawEsriWorldImagery).  Returns false if the PSRAM
// allocation fails.
bool captureBackground(uint16_t width = kDefaultWidth,
                       uint16_t height = kDefaultHeight);

} // namespace SatelliteMap
