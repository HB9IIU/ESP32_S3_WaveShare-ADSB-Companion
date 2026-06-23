#include "aircraft_renderer.h"

#include <pgmspace.h>

#include "aircraft_assets.h"
#include "satellite_map.h"

namespace Aircraft {

bool Renderer::begin(size_t capacity, uint8_t maximumSymbolSize) {
    end();
    if (capacity == 0 || maximumSymbolSize == 0) return false;

    layers_ = new Layer[capacity];
    if (!layers_) return false;

    capacity_ = capacity;
    maximumSymbolSize_ = maximumSymbolSize;
    return true;
}

void Renderer::end() {
    erase();
    delete[] layers_;
    layers_ = nullptr;
    capacity_ = 0;
    maximumSymbolSize_ = 0;
}

void Renderer::erase() {
    if (!ready()) return;
    for (size_t i = capacity_; i > 0; --i) {
        Layer& layer = layers_[i - 1];
        if (!layer.active) continue;
        SatelliteMap::restoreBackground(layer.x, layer.y, layer.size, layer.size);
        layer.active = false;
    }
}

void Renderer::draw(const State* aircraft, size_t count) {
    if (!ready() || !aircraft) return;
    count = min(count, capacity_);

    for (size_t i = 0; i < count; ++i) {
        const uint8_t sz =
            min<uint8_t>(aircraft[i].symbolSize, maximumSymbolSize_);
        const int32_t left = lroundf(aircraft[i].x) - sz / 2;
        const int32_t top  = lroundf(aircraft[i].y) - sz / 2;

        Layer& layer = layers_[i];
        layer.x          = static_cast<int16_t>(left);
        layer.y          = static_cast<int16_t>(top);
        layer.size       = sz;
        layer.color      = aircraft[i].color;
        layer.headingDeg = static_cast<int16_t>(lroundf(aircraft[i].headingDeg));
        layer.category   = aircraft[i].category;
        layer.active     = true;
        drawScaledMask(aircraft[i], left, top, sz);
    }
}

void Renderer::update(const State* aircraft, size_t count) {
    if (!ready() || !aircraft) return;
    count = min(count, capacity_);

    // Erase layers that are no longer needed.  After each erase, immediately
    // redraw any remaining active layer whose visible area overlaps the erased
    // rectangle — otherwise that neighbor is left with an invisible hole.
    for (size_t i = capacity_; i > count; --i) {
        Layer& dying = layers_[i - 1];
        if (!dying.active) continue;
        const int32_t ex = dying.x, ey = dying.y, esz = dying.size;
        SatelliteMap::restoreBackground(ex, ey, esz, esz);
        dying.active = false;

        for (size_t j = 0; j < count; ++j) {
            const Layer& jl = layers_[j];
            if (!jl.active) continue;
            if (jl.x < ex + esz && jl.x + jl.size > ex &&
                jl.y < ey + esz && jl.y + jl.size > ey) {
                drawScaledMask(aircraft[j], jl.x, jl.y, jl.size);
            }
        }
    }

    // Per-aircraft update.  Each symbol is absent for only the duration of one
    // restoreBackground() call before being drawn at its new position.
    //
    // Two repair passes guard against neighbour-overlap artifacts:
    //   1. After erasing i's old rectangle: redraw every other active aircraft j
    //      (both j<i already at new pos, and j>i still at old pos) whose drawn
    //      area overlaps the erased region.
    //   2. After drawing i at its new position: redraw every not-yet-processed
    //      aircraft j>i whose drawn area overlaps the freshly painted new rect,
    //      so that i's new pixels do not permanently punch a hole in j's symbol.
    for (size_t i = 0; i < count; ++i) {
        const uint8_t sz =
            min<uint8_t>(aircraft[i].symbolSize, maximumSymbolSize_);
        const int32_t left = lroundf(aircraft[i].x) - sz / 2;
        const int32_t top  = lroundf(aircraft[i].y) - sz / 2;
        const int16_t hdg  =
            static_cast<int16_t>(lroundf(aircraft[i].headingDeg));

        Layer& layer = layers_[i];

        // Nothing changed — keep the existing pixels.
        if (layer.active                               &&
            layer.x          == static_cast<int16_t>(left) &&
            layer.y          == static_cast<int16_t>(top)  &&
            layer.size       == sz                         &&
            layer.color      == aircraft[i].color          &&
            layer.headingDeg == hdg                        &&
            layer.category   == aircraft[i].category) {
            continue;
        }

        // ── erase old position ────────────────────────────────────────────────
        if (layer.active) {
            const int32_t ox = layer.x, oy = layer.y, osz = layer.size;
            SatelliteMap::restoreBackground(ox, oy, osz, osz);

            // Repair pass 1: any neighbour whose drawn area overlapped the
            // erased rectangle (both already-processed j<i at their new position
            // and not-yet-processed j>i still at their old position).
            for (size_t j = 0; j < count; ++j) {
                if (j == i) continue;
                const Layer& jl = layers_[j];
                if (!jl.active) continue;
                if (jl.x < ox + osz && jl.x + jl.size > ox &&
                    jl.y < oy + osz && jl.y + jl.size > oy) {
                    drawScaledMask(aircraft[j], jl.x, jl.y, jl.size);
                }
            }
        }

        // ── draw at new position ──────────────────────────────────────────────
        layer.x          = static_cast<int16_t>(left);
        layer.y          = static_cast<int16_t>(top);
        layer.size       = sz;
        layer.color      = aircraft[i].color;
        layer.headingDeg = hdg;
        layer.category   = aircraft[i].category;
        layer.active     = true;
        drawScaledMask(aircraft[i], left, top, sz);

        // Repair pass 2: not-yet-processed neighbours j>i whose old drawn area
        // overlaps i's freshly painted new rectangle.  Their own loop iteration
        // will either skip them (no state change, already re-painted here) or
        // erase-and-redraw them at their correct new position.
        for (size_t j = i + 1; j < count; ++j) {
            const Layer& jl = layers_[j];
            if (!jl.active) continue;
            if (jl.x < left + sz && jl.x + jl.size > left &&
                jl.y < top  + sz && jl.y + jl.size > top) {
                drawScaledMask(aircraft[j], jl.x, jl.y, jl.size);
            }
        }
    }
}

void Renderer::drawScaledMask(const State& aircraft, int32_t left, int32_t top,
                               uint8_t size) {
    const MaskSet& set = maskSetFor(aircraft.category);
    const uint8_t* mask =
        maskForHeading(set, static_cast<int>(lroundf(aircraft.headingDeg)));

    for (int32_t dstY = 0; dstY < size; ++dstY) {
        const int32_t srcY = dstY * set.height / size;
        int32_t dstX = 0;

        while (dstX < size) {
            const int32_t srcX = dstX * set.width / size;
            const int32_t byteIndex = srcY * set.stride + (srcX >> 3);
            const uint8_t bits = pgm_read_byte(mask + byteIndex);
            const bool on = bits & (0x80 >> (srcX & 7));
            if (!on) {
                ++dstX;
                continue;
            }

            const int32_t runStart = dstX;
            do {
                ++dstX;
                if (dstX >= size) break;
                const int32_t nextSrcX = dstX * set.width / size;
                const int32_t nextByte =
                    srcY * set.stride + (nextSrcX >> 3);
                const uint8_t nextBits = pgm_read_byte(mask + nextByte);
                if (!(nextBits & (0x80 >> (nextSrcX & 7)))) break;
            } while (true);

            tft.drawFastHLine(left + runStart, top + dstY,
                              dstX - runStart, aircraft.color);
        }
    }
}

}  // namespace Aircraft
