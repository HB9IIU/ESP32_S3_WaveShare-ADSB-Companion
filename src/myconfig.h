#pragma once

// ═════════════════════════════════════════════════════════════════════════════
// myconfig.h — Compile-time user configuration
//
// Edit this file before flashing to customise the device for your location and
// network.  These are hard-coded defaults only; they are superseded at runtime
// by anything already stored in NVS (saved map, IP-geolocated position).
// ═════════════════════════════════════════════════════════════════════════════

// ── Network identity ──────────────────────────────────────────────────────────
// The device advertises itself via mDNS.  Once connected it is reachable at
// http://<WIFI_HOSTNAME>.local on the local network.
#define WIFI_HOSTNAME "hb9iiu"

// ── Map defaults ──────────────────────────────────────────────────────────────
// MAP_CENTER_LAT / MAP_CENTER_LON
//   Last-resort fallback used ONLY when neither a saved map nor an
//   IP-geolocated position is available (e.g. very first boot without internet).
//
// MAP_RADIUS_KM
//   Vertical half-span for the hardcoded fallback coordinates.
//   0.039 km ≈ 39 m — street-level detail.
//
// MAP_GEO_RADIUS_KM
//   Radius applied when the centre is derived from IP geolocation.
//   IP geolocation resolves to city level (~1–10 km accuracy), so a wide
//   50 km radius ensures the area of interest is visible despite a coarse fix.
#define HOME_FALLBACK_LAT 46.4667209
#define HOME_FALLBACK_LON 6.8611399
#define MAP_CENTER_LAT    HOME_FALLBACK_LAT
#define MAP_CENTER_LON    HOME_FALLBACK_LON
#define MAP_RADIUS_KM     50.0f
#define MAP_GEO_RADIUS_KM 50.0f

// ── ADS-B receiver ───────────────────────────────────────────────────────────
// Base address of the Raspberry Pi / ADS-B web server.
// Do not include a trailing slash.
#define ADSB_SERVER_BASE "http://hb9iiu.gotdns.ch:4444"

// tar1090 / dump1090 aircraft feed used by the live aircraft overlay.
#define ADSB_JSON_STREAM_URL \
    ADSB_SERVER_BASE "/tar1090/data/aircraft.json"
