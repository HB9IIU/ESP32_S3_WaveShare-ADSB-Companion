#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// nvs_config.h — Persistent settings via ESP32 Non-Volatile Storage (NVS)
//
// NVS namespace layout
//   "wifi"      →  ssid (string), pass (string), saved (bool)
//   "location"  →  lat (float), lon (float), tz (string), saved (bool)
//   "home"      →  lat (double), lon (double), saved (bool)
//   "adsb"      →  url (string), saved (bool)
//
// All helpers are inline so that both boot_manager.h and main.cpp can use them
// without a separate translation unit.
//
// The "saved" bool acts as a validity flag: it is written last so that a power
// loss during a partial write leaves the namespace in the "not yet saved" state
// rather than with stale partial data.
// ═════════════════════════════════════════════════════════════════════════════

#include <Preferences.h>
#include "myconfig.h"

namespace NVSConfig {

// ── WiFi credentials ──────────────────────────────────────────────────────────

struct WiFiCreds {
    char ssid[64];
    char password[64];
    bool valid;          // true only when a complete set of credentials is stored
};

inline WiFiCreds loadWiFi() {
    WiFiCreds c{};
    Preferences p;
    p.begin("wifi", true);   // read-only
    c.valid = p.getBool("saved", false);
    if (c.valid) {
        p.getString("ssid", c.ssid, sizeof(c.ssid));
        p.getString("pass", c.password, sizeof(c.password));
    }
    p.end();
    return c;
}

inline void saveWiFi(const char* ssid, const char* pass) {
    Preferences p;
    p.begin("wifi", false);  // read-write
    p.putString("ssid", ssid);
    p.putString("pass", pass);
    p.putBool("saved", true); // written last: power-loss before this leaves "not saved"
    p.end();
}

inline void clearWiFi() {
    Preferences p;
    p.begin("wifi", false);
    p.clear();
    p.end();
}

// ── Location & timezone ───────────────────────────────────────────────────────
// Populated once on first boot by BootManager::fetchGeoLocation() (ip-api.com).
// Reused on every subsequent boot to skip the geolocation HTTP call.

struct LocationData {
    float lat;
    float lon;
    char  timezone[64];  // IANA timezone name, e.g. "Europe/Madrid"
    bool  valid;         // true only after a successful geolocation fetch
};

inline LocationData loadLocation() {
    LocationData d{};
    Preferences p;
    p.begin("location", true);
    d.valid = p.getBool("saved", false);
    if (d.valid) {
        d.lat = p.getFloat("lat", 0.0f);
        d.lon = p.getFloat("lon", 0.0f);
        p.getString("tz", d.timezone, sizeof(d.timezone));
    }
    p.end();
    return d;
}

inline void saveLocation(float lat, float lon, const char* tz) {
    Preferences p;
    p.begin("location", false);
    p.putFloat("lat", lat);
    p.putFloat("lon", lon);
    p.putString("tz", tz);
    p.putBool("saved", true); // written last: power-loss before this leaves "not saved"
    p.end();
}

inline void clearLocation() {
    Preferences p;
    p.begin("location", false);
    p.clear();
    p.end();
}

// ── Fixed Home reference ─────────────────────────────────────────────────────
// Initialized once from cached IP geolocation, or from the compile-time
// fallback when geolocation is unavailable. Map navigation never changes it.

struct HomeData {
    double lat;
    double lon;
    bool valid;
};

inline HomeData loadHome() {
    HomeData d{};
    Preferences p;
    p.begin("home", true);
    d.valid = p.getBool("saved", false);
    if (d.valid) {
        d.lat = p.getDouble("lat", 0.0);
        d.lon = p.getDouble("lon", 0.0);
    }
    p.end();
    return d;
}

inline void saveHome(double lat, double lon) {
    Preferences p;
    p.begin("home", false);
    p.putDouble("lat", lat);
    p.putDouble("lon", lon);
    p.putBool("saved", true);
    p.end();
}

inline void clearHome() {
    Preferences p;
    p.begin("home", false);
    p.clear();
    p.end();
}

// ── ADS-B server base URL ─────────────────────────────────────────────────────
// Set once by the user via the on-screen settings page; falls back to the
// compile-time ADSB_SERVER_BASE from myconfig.h when nothing has been saved.

inline String loadAdsbServer() {
    Preferences p;
    p.begin("adsb", true);
    const bool saved = p.getBool("saved", false);
    String url;
    if (saved) {
        char buf[128];
        p.getString("url", buf, sizeof(buf));
        url = buf;
    }
    p.end();
    return url.isEmpty() ? String(ADSB_SERVER_BASE) : url;
}

inline void saveAdsbServer(const char* url) {
    Preferences p;
    p.begin("adsb", false);
    p.putString("url", url);
    p.putBool("saved", true);
    p.end();
}

// ── Screensaver idle timeout ──────────────────────────────────────────────────
// Stored as seconds; 0 = disabled.
// Valid cycle values: 0, 10, 30, 60, 300, 900.

inline uint32_t loadScreensaverTimeout() {
    Preferences p;
    p.begin("adsb_ui", true);
    const uint32_t v = p.getUInt("ss_timeout", 60);
    p.end();
    return v;
}

inline void saveScreensaverTimeout(uint32_t seconds) {
    Preferences p;
    p.begin("adsb_ui", false);
    p.putUInt("ss_timeout", seconds);
    p.end();
}

} // namespace NVSConfig
