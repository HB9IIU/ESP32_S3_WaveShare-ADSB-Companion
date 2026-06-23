#include "aircraft_tracks.h"

#include <esp32-hal-psram.h>
#include "satellite_map.h"

namespace AircraftTracks {
namespace {

constexpr uint8_t kPointSize = 2;
constexpr uint32_t kRecordIntervalMs = 10000;
constexpr uint32_t kHistoryAgeMs = 10 * 60 * 1000;
constexpr float kMinimumCoordinateChange = 0.00005f;

}  // namespace

bool Renderer::begin(size_t trackCapacity, size_t pointsPerTrack,
                     size_t renderedPointCapacity) {
    end();
    if (trackCapacity == 0 || pointsPerTrack == 0 ||
        pointsPerTrack > UINT8_MAX || renderedPointCapacity == 0) {
        return false;
    }

    trackCapacity_ = trackCapacity;
    pointsPerTrack_ = pointsPerTrack;
    renderedPointCapacity_ = renderedPointCapacity;

    points_ = static_cast<Point*>(
        ps_calloc(trackCapacity_ * pointsPerTrack_, sizeof(Point)));
    counts_ = static_cast<uint8_t*>(
        ps_calloc(trackCapacity_, sizeof(uint8_t)));
    heads_ = static_cast<uint8_t*>(
        ps_calloc(trackCapacity_, sizeof(uint8_t)));
    lastRecordMs_ = static_cast<uint32_t*>(
        ps_calloc(trackCapacity_, sizeof(uint32_t)));
    layers_ = static_cast<Layer*>(
        ps_calloc(renderedPointCapacity_, sizeof(Layer)));

    if (!ready()) {
        end();
        return false;
    }
    return true;
}

void Renderer::end() {
    erase();
    free(points_);
    free(counts_);
    free(heads_);
    free(lastRecordMs_);
    free(layers_);
    points_ = nullptr;
    counts_ = nullptr;
    heads_ = nullptr;
    lastRecordMs_ = nullptr;
    layers_ = nullptr;
    trackCapacity_ = 0;
    pointsPerTrack_ = 0;
    renderedPointCapacity_ = 0;
    renderedPointCount_ = 0;
}

void Renderer::resetTrack(size_t trackIndex) {
    if (!ready() || trackIndex >= trackCapacity_) return;
    counts_[trackIndex] = 0;
    heads_[trackIndex] = 0;
    lastRecordMs_[trackIndex] = 0;
}

void Renderer::record(size_t trackIndex, double latitude, double longitude,
                      uint32_t now) {
    if (!ready() || trackIndex >= trackCapacity_) return;

    const uint8_t count = counts_[trackIndex];
    if (count > 0) {
        Point* latest = pointAt(trackIndex, count - 1);
        if (now - lastRecordMs_[trackIndex] < kRecordIntervalMs) return;
        if (fabsf(static_cast<float>(latitude) - latest->latitude) <
                kMinimumCoordinateChange &&
            fabsf(static_cast<float>(longitude) - latest->longitude) <
                kMinimumCoordinateChange) {
            return;
        }
    }

    uint8_t writeIndex = 0;
    if (count < pointsPerTrack_) {
        writeIndex =
            (heads_[trackIndex] + count) % pointsPerTrack_;
        counts_[trackIndex] = count + 1;
    } else {
        writeIndex = heads_[trackIndex];
        heads_[trackIndex] =
            (heads_[trackIndex] + 1) % pointsPerTrack_;
    }

    Point& point =
        points_[trackIndex * pointsPerTrack_ + writeIndex];
    point.latitude = static_cast<float>(latitude);
    point.longitude = static_cast<float>(longitude);
    point.recordedMs = now;
    lastRecordMs_[trackIndex] = now;
}

void Renderer::erase() {
    if (!ready()) return;
    for (size_t i = renderedPointCount_; i > 0; --i) {
        Layer& layer = layers_[i - 1];
        if (!layer.active) continue;
        SatelliteMap::restoreBackground(layer.x, layer.y, kPointSize, kPointSize);
        layer.active = false;
    }
    renderedPointCount_ = 0;
}

void Renderer::draw(const SatelliteMap::Viewport& viewport,
                    const size_t* trackIndices, const uint16_t* colors,
                    size_t trackCount, int32_t topLimit, int32_t bottomLimit,
                    uint32_t now) {
    if (!ready() || !viewport.valid || !trackIndices || !colors) return;

    for (size_t path = 0;
         path < trackCount &&
         renderedPointCount_ < renderedPointCapacity_;
         ++path) {
        const size_t trackIndex = trackIndices[path];
        if (trackIndex >= trackCapacity_) continue;

        const uint8_t count = counts_[trackIndex];
        for (uint8_t pointIndex = 0;
             pointIndex < count &&
             renderedPointCount_ < renderedPointCapacity_;
             ++pointIndex) {
            Point* point = pointAt(trackIndex, pointIndex);
            if (!point || now - point->recordedMs > kHistoryAgeMs) continue;

            int32_t x = 0;
            int32_t y = 0;
            if (!SatelliteMap::latLonToScreen(
                    viewport, point->latitude, point->longitude, x, y)) {
                continue;
            }

            const int32_t left = x - kPointSize / 2;
            const int32_t top = y - kPointSize / 2;
            if (left < 0 || top < topLimit ||
                left + kPointSize > viewport.width ||
                top + kPointSize > bottomLimit) {
                continue;
            }

            Layer& layer = layers_[renderedPointCount_];
            layer.x = left;
            layer.y = top;
            layer.active = true;
            tft.fillRect(left, top, kPointSize, kPointSize, colors[path]);
            ++renderedPointCount_;
        }
    }
}

Renderer::Point* Renderer::pointAt(size_t trackIndex, size_t logicalIndex) {
    if (trackIndex >= trackCapacity_ ||
        logicalIndex >= counts_[trackIndex]) {
        return nullptr;
    }
    const size_t physicalIndex =
        (heads_[trackIndex] + logicalIndex) % pointsPerTrack_;
    return &points_[trackIndex * pointsPerTrack_ + physicalIndex];
}

}  // namespace AircraftTracks
