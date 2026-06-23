#include "map_snapshot.h"

#include <LovyanGFX.hpp>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <time.h>
#include "HB9IIUdisplayInit.h"

namespace MapSnapshot {
namespace {

// ── File format constants ─────────────────────────────────────────────────────
constexpr uint32_t kMagic           = 0x4D415031; // "MAP1" — identifies our files
constexpr uint16_t kVersion         = 1;           // bump if Metadata layout changes
constexpr uint16_t kStripLines      = 8;           // lines per strip in the fallback path
constexpr size_t   kWriteChunkSize  = 32768;       // 32 KB per flash write (PSRAM path)
constexpr const char* kImageTempPath    = "/saved_map.rgb565.tmp";
constexpr const char* kMetadataTempPath = "/saved_map.meta.tmp";

// ── Internal helpers ──────────────────────────────────────────────────────────

void removeIfExists(fs::FS& fs, const char* path) {
    if (fs.exists(path)) fs.remove(path);
}

// Validates that a Metadata struct looks sane before trusting its fields.
bool validMetadata(const Metadata& m) {
    return m.magic      == kMagic &&
           m.version    == kVersion &&
           m.width      == SatelliteMap::kDefaultWidth &&
           m.height     == SatelliteMap::kDefaultHeight &&
           m.imageBytes == static_cast<uint32_t>(m.width) * m.height * sizeof(uint16_t) &&
           m.radiusKm   > 0.0f &&
           m.zoom       >= 1 && m.zoom <= 19 &&
           m.scaleX     > 0.0f && m.scaleY > 0.0f;
}

bool readMetadata(fs::FS& fs, Metadata& metadata) {
    File file = fs.open(kMetadataPath, FILE_READ);
    if (!file || file.size() != sizeof(Metadata)) {
        if (file) file.close();
        return false;
    }
    const bool ok = file.read(reinterpret_cast<uint8_t*>(&metadata),
                               sizeof(metadata)) == sizeof(metadata);
    file.close();
    return ok && validMetadata(metadata);
}

// Tries PSRAM first so the internal heap is kept free for the rest of the
// system.  Falls back to internal heap only if PSRAM is unavailable or full.
lgfx::rgb565_t* allocPixelBuf(size_t pixels) {
    const size_t bytes = pixels * sizeof(lgfx::rgb565_t);
    auto* p = static_cast<lgfx::rgb565_t*>(
        heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!p) {
        p = static_cast<lgfx::rgb565_t*>(
            heap_caps_malloc(bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT));
    }
    return p;
}

} // anonymous namespace

// ─────────────────────────────────────────────────────────────────────────────
// exists()
// ─────────────────────────────────────────────────────────────────────────────

bool exists(fs::FS& fs) {
    Metadata metadata;
    if (!fs.exists(kImagePath) || !fs.exists(kMetadataPath) ||
        !readMetadata(fs, metadata)) {
        return false;
    }
    File image = fs.open(kImagePath, FILE_READ);
    const bool valid = image && image.size() == metadata.imageBytes;
    if (image) image.close();
    return valid;
}

// ─────────────────────────────────────────────────────────────────────────────
// save()
// ─────────────────────────────────────────────────────────────────────────────

bool save(fs::FS& fs,
          double centerLat, double centerLon, float radiusKm,
          const SatelliteMap::Viewport& viewport,
          ProgressFn progressFn) {

    const uint32_t startedAt = millis();
    Serial.printf("[save] begin  heap=%u  psram=%u\n",
                  ESP.getFreeHeap(), ESP.getFreePsram());

    if (!viewport.valid ||
        viewport.width  != SatelliteMap::kDefaultWidth ||
        viewport.height != SatelliteMap::kDefaultHeight) {
        Serial.println("[save] rejected: invalid viewport");
        return false;
    }

    // Always start from a clean slate so a previous partial save cannot
    // interfere with the new write.
    removeIfExists(fs, kImageTempPath);
    removeIfExists(fs, kMetadataTempPath);

    File image = fs.open(kImageTempPath, FILE_WRITE);
    if (!image) {
        Serial.println("[save] ERROR: cannot open temp image file");
        return false;
    }

    const size_t totalBytes =
        static_cast<size_t>(viewport.width) * viewport.height * sizeof(lgfx::rgb565_t);
    // 800 × 480 × 2 = 768 000 bytes — far more than the 512 KB internal heap.
    // This is why PSRAM is required for the fast path.

    size_t bytesWritten = 0;
    bool   writeOk      = false;

    // ── PSRAM path: capture whole frame then write in chunks ──────────────────
    // readRect() copies the RGB panel's PSRAM framebuffer in one DMA transfer.
    // After that call the pixels are safely in our buffer and the display
    // is free — the ProgressFn is called so the caller can show a UI overlay
    // before the flash writes begin.
    auto* frameBuf = static_cast<uint8_t*>(
        heap_caps_malloc(totalBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    if (frameBuf) {
        Serial.printf("[save +%lu ms] PSRAM buffer %u bytes — capturing framebuffer\n",
                      static_cast<unsigned long>(millis() - startedAt),
                      static_cast<unsigned>(totalBytes));

        tft.readRect(0, 0, viewport.width, viewport.height,
                     reinterpret_cast<lgfx::rgb565_t*>(frameBuf));

        Serial.printf("[save +%lu ms] capture done — starting flash write\n",
                      static_cast<unsigned long>(millis() - startedAt));

        // Pixel capture is complete: notify the caller so it can show a
        // "screen will go dark" overlay and turn off the backlight before
        // flash writes disable DCache and cause display flicker.
        if (progressFn) progressFn(0.0f);

        writeOk = true;
        while (bytesWritten < totalBytes) {
            const size_t remaining = totalBytes - bytesWritten;
            const size_t chunk     = remaining < kWriteChunkSize ? remaining : kWriteChunkSize;
            if (image.write(frameBuf + bytesWritten, chunk) != chunk) {
                writeOk = false;
                break;
            }
            bytesWritten += chunk;
            if (progressFn) progressFn(static_cast<float>(bytesWritten) / totalBytes);
        }
        heap_caps_free(frameBuf);

    // ── Strip fallback (no PSRAM available) ───────────────────────────────────
    // Reads and writes the image 8 lines at a time using only ~2.5 KB of
    // internal heap.  There is no ProgressFn signalling in this path, which
    // means the caller has no safe point to draw UI or turn off the backlight;
    // visual artefacts are expected during the write.
    } else {
        Serial.printf("[save +%lu ms] PSRAM unavailable — strip fallback\n",
                      static_cast<unsigned long>(millis() - startedAt));

        const size_t stripPixels = static_cast<size_t>(viewport.width) * kStripLines;
        auto* strip = allocPixelBuf(stripPixels);
        if (!strip) {
            Serial.println("[save] ERROR: strip buffer allocation failed");
            image.close();
            removeIfExists(fs, kImageTempPath);
            return false;
        }

        writeOk = true;
        for (uint16_t y = 0; y < viewport.height && writeOk; y += kStripLines) {
            const uint16_t lines = min<uint16_t>(kStripLines, viewport.height - y);
            const size_t   bytes = static_cast<size_t>(viewport.width) * lines * sizeof(lgfx::rgb565_t);
            tft.readRect(0, y, viewport.width, lines, strip);
            if (image.write(reinterpret_cast<const uint8_t*>(strip), bytes) != bytes) {
                writeOk = false;
            } else {
                bytesWritten += bytes;
            }
        }
        heap_caps_free(strip);
    }

    image.close();
    if (!writeOk) {
        Serial.printf("[save +%lu ms] ERROR writing image\n",
                      static_cast<unsigned long>(millis() - startedAt));
        removeIfExists(fs, kImageTempPath);
        return false;
    }

    // ── Write metadata ────────────────────────────────────────────────────────
    Metadata metadata;
    metadata.magic      = kMagic;
    metadata.version    = kVersion;
    metadata.width      = viewport.width;
    metadata.height     = viewport.height;
    metadata.zoom       = viewport.zoom;
    metadata.imageBytes = static_cast<uint32_t>(bytesWritten);
    metadata.savedAt    = static_cast<int64_t>(time(nullptr));
    metadata.centerLat  = centerLat;
    metadata.centerLon  = centerLon;
    metadata.radiusKm   = radiusKm;
    metadata.westPx     = viewport.westPx;
    metadata.northPx    = viewport.northPx;
    metadata.scaleX     = viewport.scaleX;
    metadata.scaleY     = viewport.scaleY;

    File meta = fs.open(kMetadataTempPath, FILE_WRITE);
    if (!meta ||
        meta.write(reinterpret_cast<const uint8_t*>(&metadata), sizeof(metadata)) !=
            sizeof(metadata)) {
        Serial.println("[save] ERROR writing metadata");
        if (meta) meta.close();
        removeIfExists(fs, kImageTempPath);
        removeIfExists(fs, kMetadataTempPath);
        return false;
    }
    meta.close();

    // ── Atomic rename ─────────────────────────────────────────────────────────
    // Only after both temp files are fully flushed do we replace the live files.
    // A power loss before this point leaves the previous good snapshot intact.
    removeIfExists(fs, kImagePath);
    removeIfExists(fs, kMetadataPath);
    if (!fs.rename(kImageTempPath, kImagePath) ||
        !fs.rename(kMetadataTempPath, kMetadataPath)) {
        Serial.println("[save] ERROR renaming temp files");
        removeIfExists(fs, kImagePath);
        removeIfExists(fs, kMetadataPath);
        removeIfExists(fs, kImageTempPath);
        removeIfExists(fs, kMetadataTempPath);
        return false;
    }

    Serial.printf("[save +%lu ms] complete — %u bytes  center=%.6f,%.6f  radius=%.3f km\n",
                  static_cast<unsigned long>(millis() - startedAt),
                  static_cast<unsigned>(bytesWritten),
                  centerLat, centerLon, radiusKm);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// load()
// ─────────────────────────────────────────────────────────────────────────────

bool load(fs::FS& fs, Metadata& metadata, SatelliteMap::Viewport& viewport) {
    if (!readMetadata(fs, metadata)) return false;

    File image = fs.open(kImagePath, FILE_READ);
    if (!image || image.size() != metadata.imageBytes) {
        if (image) image.close();
        return false;
    }

    const size_t totalBytes = metadata.imageBytes;
    bool success = false;

    // ── PSRAM path: read whole file then push in one call ─────────────────────
    // One pushImage() call is faster and avoids any visible seam between strips.
    auto* frameBuf = static_cast<uint8_t*>(
        heap_caps_malloc(totalBytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));

    if (frameBuf) {
        if (image.read(frameBuf, totalBytes) == totalBytes) {
            tft.startWrite();
            tft.pushImage(0, 0, metadata.width, metadata.height,
                          reinterpret_cast<lgfx::rgb565_t*>(frameBuf));
            tft.endWrite();
            success = true;
        }
        heap_caps_free(frameBuf);

    // ── Strip fallback ────────────────────────────────────────────────────────
    } else {
        const size_t stripPixels = static_cast<size_t>(metadata.width) * kStripLines;
        auto* strip = allocPixelBuf(stripPixels);
        if (strip) {
            tft.startWrite();
            success = true;
            for (uint16_t y = 0; y < metadata.height && success; y += kStripLines) {
                const uint16_t lines = min<uint16_t>(kStripLines, metadata.height - y);
                const size_t   bytes = static_cast<size_t>(metadata.width) * lines * sizeof(lgfx::rgb565_t);
                if (image.read(reinterpret_cast<uint8_t*>(strip), bytes) != bytes) {
                    success = false;
                } else {
                    tft.pushImage(0, y, metadata.width, lines, strip);
                }
            }
            tft.endWrite();
            heap_caps_free(strip);
        }
    }

    image.close();
    if (!success) return false;

    // Reconstruct the Viewport so the caller can enable map interactions.
    viewport.valid   = true;
    viewport.zoom    = metadata.zoom;
    viewport.width   = metadata.width;
    viewport.height  = metadata.height;
    viewport.westPx  = metadata.westPx;
    viewport.northPx = metadata.northPx;
    viewport.scaleX  = metadata.scaleX;
    viewport.scaleY  = metadata.scaleY;

    Serial.printf("[snapshot] restored — center=%.6f,%.6f  radius=%.3f km\n",
                  metadata.centerLat, metadata.centerLon, metadata.radiusKm);
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
// remove()
// ─────────────────────────────────────────────────────────────────────────────

bool remove(fs::FS& fs) {
    // Also delete any leftover temp files from a previously interrupted save.
    removeIfExists(fs, kImageTempPath);
    removeIfExists(fs, kMetadataTempPath);

    const bool hadFiles = fs.exists(kImagePath) || fs.exists(kMetadataPath);
    removeIfExists(fs, kImagePath);
    removeIfExists(fs, kMetadataPath);

    Serial.printf("[snapshot] %s\n", hadFiles ? "deleted" : "nothing to delete");
    return !fs.exists(kImagePath) && !fs.exists(kMetadataPath);
}

} // namespace MapSnapshot
