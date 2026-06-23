#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// map_snapshot.h — Save and restore the satellite map image to/from LittleFS
//
// A snapshot consists of two files:
//   /saved_map.rgb565   — raw 16-bit RGB565 pixel data (800 × 480 × 2 bytes)
//   /saved_map.meta     — fixed-size Metadata struct with magic/version/viewport
//
// Atomic writes
//   Both files are written to .tmp paths first and renamed only on success,
//   so a power loss during a save never corrupts an existing good snapshot.
//
// PSRAM strategy
//   save() captures the full 768 KB framebuffer into PSRAM in one readRect(),
//   then writes it to flash in 32 KB chunks.  load() reads the whole file into
//   PSRAM and pushes it in one call.  Both operations fall back to a strip-by-
//   strip approach if PSRAM is unavailable, at the cost of tearing on display.
//
// Flash / DCache interaction
//   Flash writes disable the ESP32-S3 data cache (DCache) briefly, which
//   starves the RGB panel DMA of PSRAM framebuffer data and causes flicker.
//   The caller is expected to turn the backlight off for the duration of the
//   write phase; the ProgressFn callback signals when that phase begins.
// ═════════════════════════════════════════════════════════════════════════════

#include <Arduino.h>
#include <FS.h>
#include <functional>
#include "satellite_map.h"

namespace MapSnapshot {

// File paths
constexpr const char* kImagePath    = "/saved_map.rgb565";
constexpr const char* kMetadataPath = "/saved_map.meta";

// ── Metadata ──────────────────────────────────────────────────────────────────
// Stored as a flat binary struct.  kMagic + kVersion act as a two-level guard:
// magic ensures the file was written by this firmware, version rejects files
// from older firmware revisions with incompatible struct layouts.

struct Metadata {
    uint32_t magic      = 0;          // must equal kMagic (0x4D415031 = "MAP1")
    uint16_t version    = 0;          // must equal kVersion (1)
    uint16_t width      = 0;          // image width in pixels (must be 800)
    uint16_t height     = 0;          // image height in pixels (must be 480)
    uint8_t  zoom       = 0;          // OSM tile zoom level used when saved
    uint8_t  reserved[3] = {0,0,0};  // padding for alignment
    uint32_t imageBytes = 0;          // expected size of the .rgb565 file
    int64_t  savedAt    = 0;          // Unix timestamp (from time(nullptr))
    double   centerLat  = 0.0;        // map centre when saved (WGS-84)
    double   centerLon  = 0.0;
    float    radiusKm   = 0.0f;       // view half-span when saved
    double   westPx     = 0.0;        // Mercator world-pixel origin (for screenToLatLon)
    double   northPx    = 0.0;
    float    scaleX     = 0.0f;       // display pixels per world pixel
    float    scaleY     = 0.0f;
};

// ── Progress callback ─────────────────────────────────────────────────────────
// Called by save() at two distinct moments:
//   progress == 0.0f  — framebuffer has been captured into PSRAM; the display
//                        is free and the caller may now safely draw UI.
//   progress > 0.0f   — a chunk has been written to flash (0.0 < progress ≤ 1.0).
//
// Typical usage: on the first call (0.0f), show a "Saving…" overlay and turn
// the backlight off before the flash write phase begins.
using ProgressFn = std::function<void(float progress)>;

// ── Public API ────────────────────────────────────────────────────────────────

// Returns true if a valid, complete snapshot is present on the filesystem.
bool exists(fs::FS& fs);

// Captures the current TFT framebuffer and saves it as a snapshot.
// Calls progressFn(0.0f) once the capture is done (caller may draw UI),
// then progressFn(fraction) after each 32 KB write chunk.
bool save(fs::FS& fs,
          double centerLat, double centerLon, float radiusKm,
          const SatelliteMap::Viewport& viewport,
          ProgressFn progressFn = nullptr);

// Reads the saved snapshot and pushes it to the TFT.
// Populates metadata and viewport so the caller can resume map interactions.
bool load(fs::FS& fs, Metadata& metadata, SatelliteMap::Viewport& viewport);

// Deletes both snapshot files.  Returns true if neither file exists afterwards.
bool remove(fs::FS& fs);

} // namespace MapSnapshot
