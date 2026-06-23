#pragma once
#include <Arduino.h>
#include <LittleFS.h>
#include <time.h>
#include "HB9IIUdisplayInit.h"

// Full-screen clock screensaver using TT Bluescreens TTF at 270 px.
// Call open() to activate; call update() each loop() tick — returns true
// once the user taps to dismiss (caller then restores the map).
//
// The VLW font file must be present in LittleFS as /bluescreens270.vlw.
// Generate it with:  python3 tools/make_bluescreens_vlw.py 270

namespace ClockScreensaver {

// ── Font file ─────────────────────────────────────────────────────────────────
constexpr const char* kFontPath  = "/bluescreens270.vlw";
constexpr int32_t    kFontHeight = 195;    // ascent=192, descent=3

// ── Layout — single local clock centred on 800×480 ───────────────────────────
// TL_DATUM: y = top of tallest glyph (VLW font behaviour with LovyanGFX).
constexpr int32_t kClockY  = 100;   // HH:MM top → digits span y=100…295
constexpr int32_t kDateY   = 325;   // date text        (DejaVu18, MC_DATUM)
constexpr int32_t kTzY     = 355;   // UTC offset       (DejaVu18, MC_DATUM)
constexpr int32_t kHintY   = 460;   // "Tap to dismiss" (DejaVu18, MC_DATUM)

// ── Colours ───────────────────────────────────────────────────────────────────
constexpr uint16_t kColBg    = 0x0000;  // true black
constexpr uint16_t kColOn    = 0x8410;  // clock digits: ~50% white
constexpr uint16_t kColLabel = 0x4A69;  // UTC offset label, muted
constexpr uint16_t kColDate  = 0xC618;  // date text gray
constexpr uint16_t kColHint  = 0x2124;  // dismiss hint, very muted

// ── State ─────────────────────────────────────────────────────────────────────
static bool _active       = false;
static int  _lastMin      = -1;
static bool _touchWasDown = false;

// ── Rendering helpers ─────────────────────────────────────────────────────────

static void _drawClock(int hour, int min) {
    tft.fillRect(0, kClockY, 800, kFontHeight, kColBg);

    char full[6];
    snprintf(full, sizeof(full), "%02d:%02d", hour, min);
    const int32_t totalW = tft.textWidth(full);
    int32_t x = 400 - totalW / 2;

    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(kColOn, kColBg);

    char s[2] = {0, 0};
    s[0] = '0' + hour / 10;
    tft.drawString(s, x, kClockY);  x += tft.textWidth(s);
    s[0] = '0' + hour % 10;
    tft.drawString(s, x, kClockY);  x += tft.textWidth(s);
    tft.drawString(":", x, kClockY); x += tft.textWidth(":");
    s[0] = '0' + min / 10;
    tft.drawString(s, x, kClockY);  x += tft.textWidth(s);
    s[0] = '0' + min % 10;
    tft.drawString(s, x, kClockY);
}

static void _drawDate(const struct tm& loc) {
    static const char* kDays[]   = { "Sunday","Monday","Tuesday","Wednesday",
                                      "Thursday","Friday","Saturday" };
    static const char* kMonths[] = { "Jan","Feb","Mar","Apr","May","Jun",
                                      "Jul","Aug","Sep","Oct","Nov","Dec" };
    char buf[48];
    snprintf(buf, sizeof(buf), "%s  %d %s %d",
             kDays[loc.tm_wday], loc.tm_mday,
             kMonths[loc.tm_mon], loc.tm_year + 1900);
    tft.setFont(&fonts::DejaVu18);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(kColDate);
    tft.drawString(buf, 400, kDateY);
}

static void _drawTzLabel(const struct tm& loc, const struct tm& utc) {
    int offMin = (loc.tm_hour * 60 + loc.tm_min) - (utc.tm_hour * 60 + utc.tm_min);
    offMin = ((offMin % 1440) + 1440) % 1440;
    if (offMin > 720) offMin -= 1440;
    const int absMin = offMin < 0 ? -offMin : offMin;
    const int h = absMin / 60, m = absMin % 60;
    char buf[16];
    if (m == 0) snprintf(buf, sizeof(buf), "UTC%+d",      offMin >= 0 ? h : -h);
    else        snprintf(buf, sizeof(buf), "UTC%+d:%02d", offMin >= 0 ? h : -h, m);
    tft.setFont(&fonts::DejaVu18);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(kColLabel);
    tft.drawString(buf, 400, kTzY);
}

// Full initial draw. Returns false if font file is missing (caller must abort).
static bool _fullDraw(const struct tm& loc, const struct tm& utc) {
    tft.fillScreen(kColBg);

    // Static text with DejaVu18 first
    _drawDate(loc);
    _drawTzLabel(loc, utc);

    tft.setFont(&fonts::DejaVu18);
    tft.setTextDatum(MC_DATUM);
    tft.setTextColor(kColHint);
    tft.drawString("Tap anywhere to dismiss", 400, kHintY);

    if (!tft.loadFont(LittleFS, kFontPath)) {
        tft.setFont(&fonts::DejaVu24);
        tft.setTextDatum(MC_DATUM);
        tft.setTextColor(TFT_RED);
        tft.drawString("Font missing:", 400, 220);
        tft.setFont(&fonts::DejaVu18);
        tft.setTextColor(kColDate);
        tft.drawString(kFontPath, 400, 255);
        tft.drawString("Run: pio run -t uploadfs", 400, 285);
        tft.setTextDatum(TL_DATUM);
        return false;
    }

    _drawClock(loc.tm_hour, loc.tm_min);
    tft.setTextDatum(TL_DATUM);
    return true;
}

// Per-minute tick: VLW font already loaded from _fullDraw().
static void _tick(const struct tm& loc) {
    _drawClock(loc.tm_hour, loc.tm_min);
}

// ── Public API ────────────────────────────────────────────────────────────────

inline void open() {
    if (_active) return;
    _active       = true;
    _lastMin      = -1;
    _touchWasDown = false;

    time_t now = time(nullptr);
    struct tm loc, utc;
    localtime_r(&now, &loc);
    gmtime_r(&now, &utc);
    if (!_fullDraw(loc, utc)) {
        _active = false;  // font missing — abort cleanly
        return;
    }
    _lastMin = loc.tm_min;
}

// Call every loop(). Returns true on tap-to-dismiss — caller must restore map.
inline bool update() {
    if (!_active) return false;

    int32_t tx, ty;
    if (tft.getTouch(&tx, &ty)) {
        _touchWasDown = true;
    } else if (_touchWasDown) {
        _active = false;
        tft.unloadFont();
        return true;  // dismissed
    }

    time_t now = time(nullptr);
    struct tm loc;
    localtime_r(&now, &loc);
    if (loc.tm_min != _lastMin) {
        _lastMin = loc.tm_min;
        _tick(loc);
    }

    return false;
}

inline bool isActive() { return _active; }

} // namespace ClockScreensaver
