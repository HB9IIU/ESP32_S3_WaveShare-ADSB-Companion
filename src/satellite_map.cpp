#include "satellite_map.h"

#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <float.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <math.h>
#include <memory>

namespace SatelliteMap {
namespace {

// ─────────────────────────────────────────────────────────────────────────────
// Geographic constants
// ─────────────────────────────────────────────────────────────────────────────

constexpr double kEarthCircumferenceMeters = 40075016.68557849;
constexpr double kEarthRadiusKm            = 6371.0088;
// Web Mercator (EPSG:3857) is undefined beyond ±85.05°; clamping prevents
// infinite values in the logarithmic latitude formula.
constexpr double kMaxMercatorLat           = 85.05112878;

constexpr size_t kDownloadBufferSize = 4096; // byte buffer for streaming HTTPS body

// ─────────────────────────────────────────────────────────────────────────────
// Internal data structures
// ─────────────────────────────────────────────────────────────────────────────

// A point in Web Mercator world-pixel space at a given zoom level.
// Origin is the top-left corner of the world tile grid.
struct WorldPoint {
    double x;
    double y;
};

// Everything needed to loop over the tile grid and place each tile on screen.
struct TilePlan {
    uint8_t zoom;
    double  westPx, eastPx;   // world-pixel bounds of the requested view
    double  northPx, southPx;
    float   scaleX, scaleY;   // display pixels per world pixel
    int32_t minTileX, maxTileX;
    int32_t minTileY, maxTileY;
};

// ─────────────────────────────────────────────────────────────────────────────
// Math helpers
// ─────────────────────────────────────────────────────────────────────────────

double clampDouble(double value, double minValue, double maxValue) {
    if (value < minValue) return minValue;
    if (value > maxValue) return maxValue;
    return value;
}

double degToRad(double degrees) { return degrees * PI / 180.0; }
double radToDeg(double radians) { return radians * 180.0 / PI; }
bool   isHttpSuccess(int code)  { return code >= 200 && code < 300; }

void removeIfExists(fs::FS& fs, const char* path) {
    if (fs.exists(path)) fs.remove(path);
}

// ─────────────────────────────────────────────────────────────────────────────
// JPEG validation
// ─────────────────────────────────────────────────────────────────────────────

// Confirms that the file starts with the JPEG SOI marker (FF D8) and ends
// with the EOI marker (FF D9).  Catches truncated or HTML error-page downloads
// before we attempt to decode them.
bool looksLikeJpeg(fs::FS& fs, const char* path) {
    File file = fs.open(path, FILE_READ);
    if (!file) return false;

    uint8_t header[2] = {0, 0};
    uint8_t tail[2]   = {0, 0};
    const size_t size = file.size();

    if (size < 4 || file.read(header, sizeof(header)) != sizeof(header)) {
        file.close();
        return false;
    }

    file.seek(size - 2);
    const bool readTail = file.read(tail, sizeof(tail)) == sizeof(tail);
    file.close();

    return readTail &&
           header[0] == 0xFF && header[1] == 0xD8 &&
           tail[0]   == 0xFF && tail[1]   == 0xD9;
}

// ─────────────────────────────────────────────────────────────────────────────
// Web Mercator projection
// ─────────────────────────────────────────────────────────────────────────────

// Converts WGS-84 lat/lon to a world-pixel coordinate at the given OSM zoom.
// The formula is the standard Web Mercator (EPSG:3857) used by OSM, ESRI, and
// Google Maps.  The world is a square of side 256 × 2^zoom pixels; the origin
// is the top-left (NW) corner.
WorldPoint latLonToWorldPixel(double lat, double lon, uint8_t zoom) {
    lat = clampDouble(lat, -kMaxMercatorLat, kMaxMercatorLat);

    const double sinLat  = sin(degToRad(lat));
    const double mapSize = static_cast<double>(kTileSize) * static_cast<double>(1UL << zoom);

    WorldPoint point;
    point.x = (lon + 180.0) / 360.0 * mapSize;
    // Mercator Y: log((1+sin)/(1-sin)) / (4π) gives a value in [0, 1],
    // measured from the north pole (top).
    point.y = (0.5 - log((1.0 + sinLat) / (1.0 - sinLat)) / (4.0 * PI)) * mapSize;
    return point;
}

// Returns the ground distance represented by one pixel at the given lat/zoom.
// Used by chooseZoom() to match the requested metres-per-pixel.
double latitudeMetersPerPixel(double lat, uint8_t zoom) {
    return cos(degToRad(lat)) * kEarthCircumferenceMeters /
           (static_cast<double>(kTileSize) * static_cast<double>(1UL << zoom));
}

// ─────────────────────────────────────────────────────────────────────────────
// Tile planning
// ─────────────────────────────────────────────────────────────────────────────

// Calculates which tiles are needed for the request and how they map to screen
// coordinates.  All arithmetic is done in world-pixel space so that the
// display-pixel positions are correct regardless of fractional tile coverage.
TilePlan makeTilePlan(const Request& request) {
    TilePlan plan{};

    // Choose a zoom level whose scale matches the requested radius.
    const double verticalMeters      = static_cast<double>(request.radiusKm) * 2000.0;
    const double desiredMetersPerPixel = verticalMeters / request.height;
    plan.zoom = chooseZoom(request.centerLat, desiredMetersPerPixel,
                           request.minZoom, request.maxZoom);

    // Calculate the bounding box in degrees.  Longitude delta is corrected for
    // the cosine of the latitude to keep the horizontal span proportional.
    const double latDelta    = radToDeg(static_cast<double>(request.radiusKm) / kEarthRadiusKm);
    const double horizontalKm = static_cast<double>(request.radiusKm) * 2.0 *
                                static_cast<double>(request.width) /
                                static_cast<double>(request.height);
    const double cosLat      = max(0.000001, cos(degToRad(request.centerLat)));
    const double lonDelta    = radToDeg((horizontalKm / 2.0) / (kEarthRadiusKm * cosLat));

    const double northLat = clampDouble(request.centerLat + latDelta, -kMaxMercatorLat, kMaxMercatorLat);
    const double southLat = clampDouble(request.centerLat - latDelta, -kMaxMercatorLat, kMaxMercatorLat);
    const double westLon  = clampDouble(request.centerLon - lonDelta, -180.0, 180.0);
    const double eastLon  = clampDouble(request.centerLon + lonDelta, -180.0, 180.0);

    const WorldPoint nw = latLonToWorldPixel(northLat, westLon, plan.zoom);
    const WorldPoint se = latLonToWorldPixel(southLat, eastLon, plan.zoom);

    plan.westPx  = nw.x;
    plan.eastPx  = se.x;
    plan.northPx = nw.y;
    plan.southPx = se.y;
    plan.scaleX  = static_cast<float>(request.width  / (plan.eastPx  - plan.westPx));
    plan.scaleY  = static_cast<float>(request.height / (plan.southPx - plan.northPx));

    // Find the range of tile grid cells that intersect the bounding box.
    // The - 0.001 prevents floating-point rounding from including a tile whose
    // right/bottom edge exactly touches the view boundary.
    plan.minTileX = static_cast<int32_t>(floor(plan.westPx  / kTileSize));
    plan.maxTileX = static_cast<int32_t>(floor((plan.eastPx  - 0.001) / kTileSize));
    plan.minTileY = static_cast<int32_t>(floor(plan.northPx / kTileSize));
    plan.maxTileY = static_cast<int32_t>(floor((plan.southPx - 0.001) / kTileSize));

    const int32_t maxTileIndex = (1L << plan.zoom) - 1;
    plan.minTileX = constrain(plan.minTileX, 0, maxTileIndex);
    plan.maxTileX = constrain(plan.maxTileX, 0, maxTileIndex);
    plan.minTileY = constrain(plan.minTileY, 0, maxTileIndex);
    plan.maxTileY = constrain(plan.maxTileY, 0, maxTileIndex);

    return plan;
}

// ─────────────────────────────────────────────────────────────────────────────
// HTTPS tile download
// ─────────────────────────────────────────────────────────────────────────────

// Downloads a single tile from the ESRI server to a file in LittleFS.
// Uses an atomic write: streams to a .tmp file and renames only on success,
// so a failed download never leaves a corrupt file at the final path.
// WiFiClientSecure is heap-allocated to avoid a 16 KB SSL context on the stack.
Result downloadTileToFs(const String& url, const char* path,
                        uint32_t timeoutMs, fs::FS& fs) {
    Result result;
    result.lastUrl = url;

    if (WiFi.status() != WL_CONNECTED) {
        result.error = Error::WifiUnavailable;
        return result;
    }

    String tempPath = String(path) + ".tmp";
    removeIfExists(fs, tempPath.c_str());

    File output = fs.open(tempPath.c_str(), FILE_WRITE);
    if (!output) {
        result.error = Error::FileOpenFailed;
        return result;
    }

    // Heap-allocate the SSL client — its internal buffers are ~16 KB and would
    // overflow the Arduino loop task stack if placed on it.
    std::unique_ptr<WiFiClientSecure> client(new WiFiClientSecure());
    if (!client) {
        output.close();
        removeIfExists(fs, tempPath.c_str());
        result.error = Error::DownloadFailed;
        return result;
    }
    client->setInsecure(); // ESRI cert chain changes occasionally; pin-free is acceptable here

    HTTPClient http;
    http.setTimeout(timeoutMs);
    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
    http.setUserAgent("ESP32-S3-ESRI-TileMap/1.0");

    if (!http.begin(*client, url)) {
        output.close();
        removeIfExists(fs, tempPath.c_str());
        result.error = Error::HttpBeginFailed;
        return result;
    }

    result.httpCode = http.GET();
    if (result.httpCode <= 0) {
        http.end(); output.close();
        removeIfExists(fs, tempPath.c_str());
        result.error = Error::ServerUnavailable;
        return result;
    }
    if (!isHttpSuccess(result.httpCode)) {
        http.end(); output.close();
        removeIfExists(fs, tempPath.c_str());
        result.error = Error::HttpRequestFailed;
        return result;
    }

    // Stream the response body in 4 KB chunks into LittleFS.
    WiFiClient* stream  = http.getStreamPtr();
    int         remaining = http.getSize(); // -1 if Transfer-Encoding: chunked
    std::unique_ptr<uint8_t[]> buffer(new uint8_t[kDownloadBufferSize]);
    if (!buffer) {
        http.end(); output.close();
        removeIfExists(fs, tempPath.c_str());
        result.error = Error::DownloadFailed;
        return result;
    }

    while (http.connected() && (remaining > 0 || remaining == -1)) {
        const size_t available = stream->available();
        if (available == 0) { delay(1); continue; }

        const size_t wanted  = remaining > 0
                                   ? min(available, static_cast<size_t>(remaining))
                                   : available;
        const size_t toRead  = min(wanted, kDownloadBufferSize);
        const int    readCount = stream->readBytes(buffer.get(), toRead);

        if (readCount <= 0) break;

        if (output.write(buffer.get(), readCount) != static_cast<size_t>(readCount)) {
            http.end(); output.close();
            removeIfExists(fs, tempPath.c_str());
            result.error = Error::DownloadFailed;
            return result;
        }
        result.bytesWritten += readCount;
        if (remaining > 0) remaining -= readCount;
    }

    http.end();
    output.close();

    if (result.bytesWritten == 0) {
        removeIfExists(fs, tempPath.c_str());
        result.error = Error::EmptyImage;
        return result;
    }
    if (!looksLikeJpeg(fs, tempPath.c_str())) {
        removeIfExists(fs, tempPath.c_str());
        result.error = Error::InvalidJpeg;
        return result;
    }

    // Atomic rename: only now is the good file visible at the final path.
    removeIfExists(fs, path);
    if (!fs.rename(tempPath.c_str(), path)) {
        removeIfExists(fs, tempPath.c_str());
        result.error = Error::DownloadFailed;
        return result;
    }

    result.error = Error::Ok;
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
// Tile rendering helpers
// ─────────────────────────────────────────────────────────────────────────────

// Decodes and scales a JPEG tile onto any LovyanGFX surface (physical TFT or
// PSRAM sprite).  Accepts a generic LovyanGFX& so the call site can switch
// between direct-to-screen and off-screen modes without branching.
Error displayCachedTile(lgfx::LovyanGFX& target,
                        fs::FS& fs,
                        const char* path,
                        int32_t x, int32_t y,
                        int32_t width, int32_t height,
                        float scaleX, float scaleY) {
    if (!path || !fs.exists(path)) return Error::FileOpenFailed;
    if (!looksLikeJpeg(fs, path))  return Error::InvalidJpeg;
    const bool drawn = target.drawJpgFile(fs, path, x, y, width, height,
                                           0, 0, scaleX, scaleY);
    return drawn ? Error::Ok : Error::DisplayFailed;
}

// Thin green progress bar drawn at the very bottom of the physical TFT while
// the PSRAM sprite is being built.  Gives the user visual feedback during
// multi-tile downloads without disturbing the main framebuffer.
// Overwritten by canvas.pushSprite() once all tiles are ready.
void drawTileProgress(uint16_t done, uint16_t total) {
    constexpr int32_t kY = 474, kH = 6;
    tft.fillRect(0, kY, 800, kH, 0x0861);  // dark background
    if (total > 0 && done < total) {
        tft.fillRect(0, kY, static_cast<int32_t>(800 * done / total), kH, 0x03E0); // green fill
    }
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// Public API implementation
// ─────────────────────────────────────────────────────────────────────────────

const char* errorToString(Error error) {
    switch (error) {
        case Error::Ok:                return "ok";
        case Error::WifiUnavailable:   return "wifi unavailable";
        case Error::ServerUnavailable: return "server unavailable";
        case Error::HttpBeginFailed:   return "http begin failed";
        case Error::HttpRequestFailed: return "http request failed";
        case Error::DownloadFailed:    return "download failed";
        case Error::FileOpenFailed:    return "file open failed";
        case Error::EmptyImage:        return "empty image";
        case Error::InvalidJpeg:       return "invalid jpeg";
        case Error::DisplayFailed:     return "display failed";
        case Error::TileMathFailed:    return "tile math failed";
    }
    return "unknown error";
}

// Picks the zoom level whose ground scale (metres/pixel) is closest to the
// target, comparing errors in log-space so that overshooting and undershooting
// by the same factor are penalised equally across all zoom levels.
uint8_t chooseZoom(double centerLat, float metersPerPixel,
                   uint8_t minZoom, uint8_t maxZoom) {
    minZoom = constrain(minZoom, static_cast<uint8_t>(1), static_cast<uint8_t>(19));
    maxZoom = constrain(maxZoom, minZoom,                  static_cast<uint8_t>(19));

    uint8_t bestZoom  = minZoom;
    double  bestError = DBL_MAX;

    for (uint8_t z = minZoom; z <= maxZoom; z++) {
        const double mpp   = latitudeMetersPerPixel(centerLat, z);
        const double error = fabs(log(mpp / metersPerPixel));
        if (error < bestError) {
            bestError = error;
            bestZoom  = z;
        }
    }
    return bestZoom;
}

// ESRI tile URL format: <base>/<zoom>/<row>/<col>
// Note: ESRI uses row (Y) before column (X), which is the opposite of OSM.
String buildTileUrl(uint8_t zoom, uint32_t tileX, uint32_t tileY) {
    String url = kEsriTileBaseUrl;
    url += "/"; url += String(zoom);
    url += "/"; url += String(tileY);   // row first
    url += "/"; url += String(tileX);
    return url;
}

// Inverts the Web Mercator projection: world-pixel → lat/lon.
// The inverse Mercator formula for latitude is atan(sinh(π - 2πy/mapSize)).
bool screenToLatLon(const Viewport& viewport,
                    int32_t screenX, int32_t screenY,
                    double& latitude, double& longitude) {
    if (!viewport.valid ||
        viewport.width  == 0 || viewport.height == 0 ||
        viewport.scaleX <= 0.0f || viewport.scaleY <= 0.0f ||
        screenX < 0 || screenY < 0 ||
        screenX >= viewport.width || screenY >= viewport.height) {
        return false;
    }

    const double mapSize   = static_cast<double>(kTileSize) *
                             static_cast<double>(1UL << viewport.zoom);
    const double worldX    = viewport.westPx  + screenX / viewport.scaleX;
    const double worldY    = viewport.northPx + screenY / viewport.scaleY;
    const double mercatorY = PI - (2.0 * PI * worldY / mapSize);

    longitude = worldX / mapSize * 360.0 - 180.0;
    latitude  = radToDeg(atan(sinh(mercatorY)));
    latitude  = clampDouble(latitude,  -kMaxMercatorLat, kMaxMercatorLat);
    longitude = clampDouble(longitude, -180.0,           180.0);
    return true;
}

bool latLonToScreen(const Viewport& viewport,
                    double latitude, double longitude,
                    int32_t& screenX, int32_t& screenY) {
    if (!viewport.valid ||
        viewport.width == 0 || viewport.height == 0 ||
        viewport.scaleX <= 0.0f || viewport.scaleY <= 0.0f) {
        return false;
    }

    latitude = clampDouble(latitude, -kMaxMercatorLat, kMaxMercatorLat);
    longitude = clampDouble(longitude, -180.0, 180.0);

    const double mapSize = static_cast<double>(kTileSize) *
                           static_cast<double>(1UL << viewport.zoom);
    const double latRad = degToRad(latitude);
    const double worldX = (longitude + 180.0) / 360.0 * mapSize;
    const double worldY =
        (1.0 - log(tan(latRad) + 1.0 / cos(latRad)) / PI) *
        0.5 * mapSize;

    screenX = static_cast<int32_t>(
        lround((worldX - viewport.westPx) * viewport.scaleX));
    screenY = static_cast<int32_t>(
        lround((worldY - viewport.northPx) * viewport.scaleY));

    return screenX >= 0 && screenY >= 0 &&
           screenX < viewport.width && screenY < viewport.height;
}

// ─────────────────────────────────────────────────────────────────────────────
// Main render function
// ─────────────────────────────────────────────────────────────────────────────

// Persistent PSRAM sprite holding the clean map background.
// Kept alive after drawEsriWorldImagery() so aircraft overlay pixels can be
// restored cheaply without re-fetching tiles.
static LGFX_Sprite* gBackgroundCanvas = nullptr;

// Scratch buffer for restoreBackground(): holds at most 64×64 raw pixels.
// uint16_t (not lgfx::rgb565_t) so readRect/pushImage both operate on the
// display-native 16-bit format with no intermediate byte-swap conversion.
static uint16_t gRestoreBuffer[64 * 64];

// Downloads and renders all tiles required to fill the requested area.
//
// Off-screen PSRAM sprite strategy
//   While tiles download, the old map image remains live on the display.
//   Each tile is decoded into a full-screen LGFX_Sprite (allocated in PSRAM).
//   A thin progress bar is drawn directly on the TFT to show download progress.
//   When all tiles are ready, one canvas.pushSprite(0,0) replaces the full
//   screen atomically, eliminating the tile-by-tile mosaic flicker that would
//   occur if tiles were decoded directly to the TFT.
//   If PSRAM allocation fails the function falls back to drawing tiles directly
//   to the TFT (the old flicker-prone behaviour), so the feature degrades
//   gracefully rather than failing.
//   The canvas is deleted once pushed; captureBackground() then reads the TFT
//   into a flat PSRAM buffer for cheap aircraft pixel restoration.
Result drawEsriWorldImagery(const Request& request, fs::FS& fs) {
    Result result;

    if (request.width == 0 || request.height == 0 || request.radiusKm <= 0.0f) {
        result.error = Error::TileMathFailed;
        return result;
    }
    if (WiFi.status() != WL_CONNECTED) {
        result.error = Error::WifiUnavailable;
        return result;
    }

    const char*    tilePath = request.tilePath ? request.tilePath : kDefaultTilePath;
    const TilePlan plan     = makeTilePlan(request);
    result.zoom             = plan.zoom;

    if (plan.eastPx  <= plan.westPx  ||
        plan.southPx <= plan.northPx ||
        plan.scaleX  <= 0.0f || plan.scaleY <= 0.0f ||
        plan.maxTileX < plan.minTileX || plan.maxTileY < plan.minTileY) {
        result.error = Error::TileMathFailed;
        return result;
    }

    // Populate the viewport so the caller can save it and use screenToLatLon().
    result.viewport.valid   = true;
    result.viewport.zoom    = plan.zoom;
    result.viewport.width   = request.width;
    result.viewport.height  = request.height;
    result.viewport.westPx  = plan.westPx;
    result.viewport.northPx = plan.northPx;
    result.viewport.scaleX  = plan.scaleX;
    result.viewport.scaleY  = plan.scaleY;

    Serial.printf("[map] center %.6f %.6f  radius %.3f km\n",
                  request.centerLat, request.centerLon, request.radiusKm);
    Serial.printf("[map] zoom=%u  tiles x=%ld..%ld  y=%ld..%ld  scale=%.4f %.4f\n",
                  plan.zoom,
                  static_cast<long>(plan.minTileX), static_cast<long>(plan.maxTileX),
                  static_cast<long>(plan.minTileY), static_cast<long>(plan.maxTileY),
                  plan.scaleX, plan.scaleY);

    const uint16_t tilesTotal =
        static_cast<uint16_t>((plan.maxTileX - plan.minTileX + 1) *
                               (plan.maxTileY - plan.minTileY + 1));

    // ── Off-screen PSRAM sprite ───────────────────────────────────────────────
    // Free any previous background canvas so the new map replaces it cleanly.
    if (gBackgroundCanvas) {
        gBackgroundCanvas->deleteSprite();
        delete gBackgroundCanvas;
        gBackgroundCanvas = nullptr;
    }
    gBackgroundCanvas = new LGFX_Sprite(&tft);
    gBackgroundCanvas->setPsram(true);
    gBackgroundCanvas->setColorDepth(16);
    const bool useCanvas =
        (gBackgroundCanvas->createSprite(request.width, request.height) != nullptr);

    if (!useCanvas) {
        // PSRAM allocation failed — fall back to direct TFT rendering.
        delete gBackgroundCanvas;
        gBackgroundCanvas = nullptr;
    }

    lgfx::LovyanGFX& target = useCanvas
        ? static_cast<lgfx::LovyanGFX&>(*gBackgroundCanvas)
        : static_cast<lgfx::LovyanGFX&>(tft);

    if (useCanvas) {
        Serial.printf("[map] PSRAM sprite %u×%u — old map stays live during download\n",
                      request.width, request.height);
        gBackgroundCanvas->fillScreen(TFT_BLACK);
        gBackgroundCanvas->setClipRect(0, 0, request.width, request.height);
        drawTileProgress(0, tilesTotal);
    } else {
        Serial.println("[map] No PSRAM sprite — drawing tiles direct to TFT");
        tft.fillScreen(TFT_BLACK);
        tft.setClipRect(0, 0, request.width, request.height);
    }

    // ── Tile download and render loop ─────────────────────────────────────────
    for (int32_t ty = plan.minTileY; ty <= plan.maxTileY; ty++) {
        for (int32_t tx = plan.minTileX; tx <= plan.maxTileX; tx++) {

            // Compute the destination rectangle for this tile in display pixels.
            const double tileLeftPx  = static_cast<double>(tx) * kTileSize;
            const double tileTopPx   = static_cast<double>(ty) * kTileSize;
            const double left   = (tileLeftPx           - plan.westPx)  * plan.scaleX;
            const double top    = (tileTopPx            - plan.northPx) * plan.scaleY;
            const double right  = (tileLeftPx + kTileSize - plan.westPx)  * plan.scaleX;
            const double bottom = (tileTopPx  + kTileSize - plan.northPx) * plan.scaleY;
            const int32_t dstX      = static_cast<int32_t>(floor(left));
            const int32_t dstY      = static_cast<int32_t>(floor(top));
            const int32_t dstRight  = static_cast<int32_t>(ceil(right))   + 1;
            const int32_t dstBottom = static_cast<int32_t>(ceil(bottom))  + 1;
            const int32_t drawW     = dstRight  - dstX;
            const int32_t drawH     = dstBottom - dstY;

            // The tile grid includes border cells that are only partially
            // within the view; rounding can push them fully off-screen.
            if (dstRight <= 0 || dstBottom <= 0 ||
                dstX >= request.width || dstY >= request.height) {
                Serial.printf("[map] skip off-screen tile z=%u x=%ld y=%ld\n",
                              plan.zoom, static_cast<long>(tx), static_cast<long>(ty));
                continue;
            }

            result.tilesRequested++;
            Serial.printf("[map] tile z=%u x=%ld y=%ld\n",
                          plan.zoom, static_cast<long>(tx), static_cast<long>(ty));

            Result tileResult = downloadTileToFs(
                buildTileUrl(plan.zoom, tx, ty), tilePath, request.httpTimeoutMs, fs);
            result.lastUrl      = tileResult.lastUrl;
            result.httpCode     = tileResult.httpCode;
            result.bytesWritten += tileResult.bytesWritten;

            if (!tileResult.ok()) {
                result.error = tileResult.error;
                target.clearClipRect();
                if (useCanvas) {
                    gBackgroundCanvas->deleteSprite();
                    delete gBackgroundCanvas;
                    gBackgroundCanvas = nullptr;
                }
                return result;
            }

            // LovyanGFX's scaled JPEG decoder can leave a one-pixel gap at the
            // tile boundary.  Expanding each tile by one pixel into the next
            // tile's space fills the gap; the neighbour tile overwrites the
            // overlap when it is drawn.
            const float tileScaleX = static_cast<float>(drawW) / kTileSize;
            const float tileScaleY = static_cast<float>(drawH) / kTileSize;

            Error drawError = displayCachedTile(target, fs, tilePath,
                                                dstX, dstY, drawW, drawH,
                                                tileScaleX, tileScaleY);
            if (drawError != Error::Ok) {
                result.error = drawError;
                target.clearClipRect();
                if (useCanvas) {
                    gBackgroundCanvas->deleteSprite();
                    delete gBackgroundCanvas;
                    gBackgroundCanvas = nullptr;
                }
                return result;
            }

            result.tilesDrawn++;
            if (useCanvas) drawTileProgress(result.tilesDrawn, tilesTotal);
        }
    }

    target.clearClipRect();

    // Push the completed sprite to the screen in one DMA transfer.
    // The canvas is intentionally kept alive as gBackgroundCanvas so that
    // restoreBackground() can repair aircraft overlay pixels cheaply.
    if (useCanvas) {
        gBackgroundCanvas->pushSprite(0, 0);
    }

    result.error = Error::Ok;
    Serial.printf("[map] done — stack remaining: %u bytes\n",
                  static_cast<unsigned>(uxTaskGetStackHighWaterMark(nullptr)));
    return result;
}

bool hasBackground() {
    return gBackgroundCanvas != nullptr;
}

// Restores a dirty rectangle from the background sprite to the TFT.
// Reads only the needed w×h pixels from the sprite into gRestoreBuffer using
// raw uint16_t (display-native format, no byte-swap), then pushes those same
// raw pixels to the TFT.  Copying w×h ≤ 64×64 pixels is much faster than
// pushSprite(0,0) which must scan all 480 sprite rows to apply the clip rect.
void restoreBackground(int32_t x, int32_t y, int32_t w, int32_t h) {
    if (!gBackgroundCanvas || w <= 0 || h <= 0) return;
    const size_t nPixels = static_cast<size_t>(w) * h;
    if (nPixels > sizeof(gRestoreBuffer) / sizeof(gRestoreBuffer[0])) return;
    gBackgroundCanvas->readRect(x, y, w, h, gRestoreBuffer);
    tft.pushImage(x, y, w, h, gRestoreBuffer);
}

// Populates the background sprite from the current TFT display contents.
// tft.readRect(uint16_t*) reads the frame-buffer in its native 16-bit format
// and writes those raw values directly into the sprite buffer (same native
// format since the sprite is a child of tft).  Single call — no row loop.
bool captureBackground(uint16_t width, uint16_t height) {
    if (gBackgroundCanvas) {
        gBackgroundCanvas->deleteSprite();
        delete gBackgroundCanvas;
        gBackgroundCanvas = nullptr;
    }
    gBackgroundCanvas = new LGFX_Sprite(&tft);
    gBackgroundCanvas->setPsram(true);
    gBackgroundCanvas->setColorDepth(16);
    if (!gBackgroundCanvas->createSprite(width, height)) {
        delete gBackgroundCanvas;
        gBackgroundCanvas = nullptr;
        Serial.println("[map] captureBackground: PSRAM allocation failed");
        return false;
    }
    tft.readRect(0, 0, width, height,
                 static_cast<uint16_t*>(gBackgroundCanvas->getBuffer()));
    Serial.printf("[map] captureBackground: captured %u×%u\n", width, height);
    return true;
}

} // namespace SatelliteMap
