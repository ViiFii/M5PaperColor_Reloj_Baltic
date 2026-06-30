// ============================================================
//  config.h  —  M5Paper Color firmware configuration
//  Updated for: English language + Baltic cities
//  Weather source: Open-Meteo (free, no API key needed)
// ============================================================

#pragma once

#include <Arduino.h>

// ── Location structure ───────────────────────────────────────
// Used by clima_openmeteo.h and the display code.
// Unlike the original AEMET backend (which needed an INE
// municipality code), Open-Meteo uses lat/lon coordinates.
struct LocationConfig {
    const char* name;   // display name
    float       lat;    // WGS84 latitude
    float       lon;    // WGS84 longitude
};

// ── Baltic city locations ────────────────────────────────────
// Add or remove cities here; update NUM_LOCATIONS accordingly.
static const LocationConfig LOCATIONS[] = {
    { "Vilnius",  54.6872,  25.2797 },   // Lithuania
    { "Riga",     56.9460,  24.1059 },   // Latvia
    { "Tallinn",  59.4370,  24.7536 },   // Estonia
};
static const uint8_t NUM_LOCATIONS = 3;

// ── Timezone ─────────────────────────────────────────────────
// Baltic states are EET (UTC+2) / EEST (UTC+3 in summer).
// This POSIX TZ string handles automatic DST switching.
// Format: standard offset DST, start (Mmonth.week.day/time),
//         end (Mmonth.week.day/time)
// EET-2EEST = Eastern European Time, DST begins last Sunday
// of March at 03:00 local, ends last Sunday of October at 04:00.
static const char* TIMEZONE_POSIX = "EET-2EEST,M3.5.0/3,M10.5.0/4";

// ── NTP servers ──────────────────────────────────────────────
static const char* NTP_SERVER_1 = "pool.ntp.org";
static const char* NTP_SERVER_2 = "time.cloudflare.com";

// ── Display / UI strings (English) ───────────────────────────
// These replace the original Spanish labels.
namespace UI {
    // Mode names (shown on mode-switch screen)
    static const char* MODE_WEATHER   = "Weather";
    static const char* MODE_CAROUSEL  = "Photos";
    static const char* MODE_MUSIC     = "Music";
    static const char* MODE_BOOK      = "Book";
    static const char* MODE_WIFI      = "Wi-Fi";

    // Weather view labels
    static const char* LABEL_TODAY    = "Today";
    static const char* LABEL_HUMIDITY = "Humidity";
    static const char* LABEL_WIND     = "Wind";
    static const char* LABEL_GUST     = "Gust";
    static const char* LABEL_UV       = "UV";
    static const char* LABEL_RAIN     = "Rain";
    static const char* LABEL_FEELS    = "Feels";
    static const char* LABEL_MAX      = "Max";
    static const char* LABEL_MIN      = "Min";
    static const char* LABEL_SOURCE   = "Open-Meteo";  // attribution (CC BY 4.0)
    static const char* LABEL_CACHED   = "Cached";
    static const char* LABEL_UPDATING = "Updating...";
    static const char* LABEL_NO_WIFI  = "No Wi-Fi";

    // Weekday names (Sunday = 0)
    static const char* WEEKDAYS[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };

    // Month names (January = 0)
    static const char* MONTHS[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };

    // Battery / status
    static const char* LABEL_BATTERY  = "Bat";
    static const char* LABEL_CHARGING = "Charging";

    // Indoor sensor
    static const char* LABEL_INDOOR_T = "Indoor";
    static const char* LABEL_INDOOR_H = "Humidity";

    // WiFi manager
    static const char* WIFI_CONNECTED = "Connected";
    static const char* WIFI_AP_MODE   = "Access Point";
    static const char* WIFI_IP        = "IP";
    static const char* WIFI_SSID      = "Network";
    static const char* WIFI_SCAN      = "Scanning...";

    // Book reader
    static const char* BOOK_SELECT    = "Select book";
    static const char* BOOK_PAGE      = "Page";

    // Music player
    static const char* MUSIC_PLAYING  = "Playing";
    static const char* MUSIC_PAUSED   = "Paused";
    static const char* MUSIC_VOLUME   = "Volume";

    // TV-B-Gone (hidden mode)
    static const char* TVB_SENDING    = "Sending IR...";
    static const char* TVB_DONE       = "Done";
}

// ── UV index thresholds (WHO scale) ─────────────────────────
inline const char* uvCategory(uint8_t uv) {
    if (uv <= 2)  return "Low";
    if (uv <= 5)  return "Moderate";
    if (uv <= 7)  return "High";
    if (uv <= 10) return "Very High";
    return "Extreme";
}

// ── Compass direction from degrees ───────────────────────────
inline const char* windDirection(float deg) {
    static const char* dirs[] = {
        "N","NNE","NE","ENE","E","ESE","SE","SSE",
        "S","SSW","SW","WSW","W","WNW","NW","NNW"
    };
    return dirs[(int)((deg + 11.25f) / 22.5f) % 16];
}
