// ============================================================
//  clima_openmeteo.h  —  Open-Meteo weather backend
//  Replaces the original AEMET (Spanish) weather API with
//  Open-Meteo (https://open-meteo.com), which is:
//    • Completely free for non-commercial use
//    • No API key or registration required
//    • Global coverage — works for Vilnius, Riga, Tallinn
//    • Returns data in JSON via plain HTTP GET
//
//  Drop-in replacement: exposes the same WeatherData struct
//  and fetchWeather() function signature that the rest of
//  the firmware expects.
//
//  Open-Meteo API endpoint used:
//    https://api.open-meteo.com/v1/forecast
//  Parameters requested:
//    current   : temperature_2m, relative_humidity_2m,
//                apparent_temperature, weather_code,
//                wind_speed_10m, wind_gusts_10m,
//                uv_index
//    hourly    : temperature_2m, weather_code,
//                precipitation_probability  (next 24 h)
//    daily     : weather_code, temperature_2m_max,
//                temperature_2m_min, precipitation_sum,
//                wind_speed_10m_max, wind_gusts_10m_max,
//                uv_index_max  (today + 3 days)
//    timezone  : auto (server picks the right TZ)
//    wind_speed_unit: kmh
//
//  WMO weather codes → icon mapping is provided below.
//  Attribution: Open-Meteo data is © Open-Meteo contributors,
//  licensed CC BY 4.0. Display "Open-Meteo" as source on screen.
// ============================================================

#pragma once

#include <Arduino.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <SD.h>                 // for SD cache
#include "config.h"             // project config (locations[])

// ── Data structures ─────────────────────────────────────────

// One hourly slot (current day forecast by hour)
struct HourlySlot {
    uint8_t  hour;          // 0-23
    float    temp;          // °C
    uint8_t  weatherCode;   // WMO code
    uint8_t  precipProb;    // precipitation probability 0-100 %
};

// One daily summary (today = index 0, tomorrow = 1, …)
struct DayForecast {
    char     date[11];      // "YYYY-MM-DD"
    float    tempMax;
    float    tempMin;
    float    precipSum;     // mm
    float    windSpeed;     // km/h max
    float    windGust;      // km/h max
    uint8_t  weatherCode;   // WMO code
    uint8_t  uvMax;         // rounded
};

// Current conditions
struct CurrentWeather {
    float    temp;
    float    feelsLike;
    uint8_t  humidity;
    float    windSpeed;     // km/h
    float    windGust;      // km/h
    uint8_t  weatherCode;
    float    uvIndex;
};

// Full weather data for one location
struct WeatherData {
    bool            valid;
    char            locationName[32];
    float           lat;
    float           lon;
    CurrentWeather  current;
    HourlySlot      hourly[24];   // today's hourly forecast
    uint8_t         hourlyCount;
    DayForecast     daily[4];     // today + 3 days
    uint8_t         dailyCount;
};

// ── WMO weather code → short English description ─────────────
// Reference: https://open-meteo.com/en/docs#weathervariables
inline const char* wmoDescription(uint8_t code) {
    switch (code) {
        case 0:  return "Clear sky";
        case 1:  return "Mainly clear";
        case 2:  return "Partly cloudy";
        case 3:  return "Overcast";
        case 45: return "Foggy";
        case 48: return "Icy fog";
        case 51: return "Light drizzle";
        case 53: return "Drizzle";
        case 55: return "Heavy drizzle";
        case 56: return "Freezing drizzle";
        case 57: return "Heavy frz. drizzle";
        case 61: return "Slight rain";
        case 63: return "Rain";
        case 65: return "Heavy rain";
        case 66: return "Freezing rain";
        case 67: return "Heavy frz. rain";
        case 71: return "Slight snow";
        case 73: return "Snow";
        case 75: return "Heavy snow";
        case 77: return "Snow grains";
        case 80: return "Slight showers";
        case 81: return "Showers";
        case 82: return "Heavy showers";
        case 85: return "Snow showers";
        case 86: return "Heavy snow showers";
        case 95: return "Thunderstorm";
        case 96: return "T-storm w/ hail";
        case 99: return "T-storm heavy hail";
        default: return "Unknown";
    }
}

// ── WMO code → icon index mapping ───────────────────────────
// Maps WMO codes to the same icon indices the original
// firmware uses for AEMET state codes (0-based):
//   0 = Clear day          1 = Partly cloudy
//   2 = Cloudy             3 = Overcast / fog
//   4 = Light rain         5 = Rain
//   6 = Heavy rain         7 = Drizzle
//   8 = Snow               9 = Thunderstorm
//  10 = Clear night       11 = Partly cloudy night
// (Add more if your icon set has them)
inline uint8_t wmoToIconIndex(uint8_t code, bool isNight = false) {
    if (code == 0)                          return isNight ? 10 : 0;
    if (code <= 2)                          return isNight ? 11 : 1;
    if (code == 3)                          return 2;
    if (code <= 48)                         return 3;  // fog
    if (code <= 55)                         return 7;  // drizzle
    if (code <= 57)                         return 7;  // freezing drizzle
    if (code <= 65)                         return 5;  // rain
    if (code <= 67)                         return 6;  // heavy/freezing rain
    if (code <= 77)                         return 8;  // snow
    if (code <= 82)                         return 5;  // showers
    if (code <= 86)                         return 8;  // snow showers
    if (code >= 95)                         return 9;  // thunderstorm
    return 2;
}

// ── SD cache helpers ─────────────────────────────────────────
// The firmware saves the last fetched JSON to SD so it can
// display it instantly after power-on even without WiFi.

static const char* CACHE_DIR  = "/weather";
static const char* CACHE_EXT  = ".json";

static String cacheFilePath(uint8_t locIndex) {
    return String(CACHE_DIR) + "/loc" + locIndex + CACHE_EXT;
}

static bool saveCacheToSD(uint8_t locIndex, const String& json) {
    if (!SD.exists(CACHE_DIR)) SD.mkdir(CACHE_DIR);
    File f = SD.open(cacheFilePath(locIndex), FILE_WRITE);
    if (!f) return false;
    f.print(json);
    f.close();
    return true;
}

static String loadCacheFromSD(uint8_t locIndex) {
    File f = SD.open(cacheFilePath(locIndex));
    if (!f) return "";
    String s = f.readString();
    f.close();
    return s;
}

// ── JSON → WeatherData parser ────────────────────────────────
static bool parseOpenMeteoJson(const String& json,
                               WeatherData& wd,
                               const char* locationName,
                               float lat, float lon)
{
    // Allocate a generous JSON document; the response is ~6 KB
    DynamicJsonDocument doc(24576);
    DeserializationError err = deserializeJson(doc, json);
    if (err) {
        Serial.printf("[Weather] JSON parse error: %s\n", err.c_str());
        return false;
    }

    strncpy(wd.locationName, locationName, sizeof(wd.locationName) - 1);
    wd.lat = lat;
    wd.lon = lon;

    // ── Current conditions ───────────────────────────────────
    JsonObject cur = doc["current"];
    if (!cur.isNull()) {
        wd.current.temp        = cur["temperature_2m"]        | 0.0f;
        wd.current.feelsLike   = cur["apparent_temperature"]  | 0.0f;
        wd.current.humidity    = cur["relative_humidity_2m"]  | 0;
        wd.current.windSpeed   = cur["wind_speed_10m"]        | 0.0f;
        wd.current.windGust    = cur["wind_gusts_10m"]        | 0.0f;
        wd.current.weatherCode = cur["weather_code"]          | 0;
        wd.current.uvIndex     = cur["uv_index"]              | 0.0f;
    }

    // ── Hourly forecast (next 24 slots for today) ────────────
    JsonObject hourly = doc["hourly"];
    wd.hourlyCount = 0;
    if (!hourly.isNull()) {
        JsonArray times  = hourly["time"];
        JsonArray temps  = hourly["temperature_2m"];
        JsonArray codes  = hourly["weather_code"];
        JsonArray precip = hourly["precipitation_probability"];

        // Find index of today's first hour (00:00 local time)
        // The API returns 168 hourly values (7 days) starting at
        // today 00:00. We take the first 24.
        for (uint8_t i = 0; i < 24 && i < times.size(); i++) {
            const char* t = times[i];
            // "YYYY-MM-DDTHH:00" — extract hour
            if (t && strlen(t) >= 16) {
                wd.hourly[i].hour       = atoi(t + 11);
                wd.hourly[i].temp       = temps[i]  | 0.0f;
                wd.hourly[i].weatherCode= codes[i]  | 0;
                wd.hourly[i].precipProb = precip[i] | 0;
                wd.hourlyCount++;
            }
        }
    }

    // ── Daily forecast (4 days) ──────────────────────────────
    JsonObject daily = doc["daily"];
    wd.dailyCount = 0;
    if (!daily.isNull()) {
        JsonArray dates     = daily["time"];
        JsonArray maxT      = daily["temperature_2m_max"];
        JsonArray minT      = daily["temperature_2m_min"];
        JsonArray precip    = daily["precipitation_sum"];
        JsonArray wind      = daily["wind_speed_10m_max"];
        JsonArray gusts     = daily["wind_gusts_10m_max"];
        JsonArray uvMax     = daily["uv_index_max"];
        JsonArray wcodes    = daily["weather_code"];

        for (uint8_t i = 0; i < 4 && i < dates.size(); i++) {
            strncpy(wd.daily[i].date, dates[i] | "", 10);
            wd.daily[i].date[10]     = '\0';
            wd.daily[i].tempMax      = maxT[i]   | 0.0f;
            wd.daily[i].tempMin      = minT[i]   | 0.0f;
            wd.daily[i].precipSum    = precip[i] | 0.0f;
            wd.daily[i].windSpeed    = wind[i]   | 0.0f;
            wd.daily[i].windGust     = gusts[i]  | 0.0f;
            wd.daily[i].uvMax        = (uint8_t)round((float)(uvMax[i] | 0.0f));
            wd.daily[i].weatherCode  = wcodes[i] | 0;
            wd.dailyCount++;
        }
    }

    wd.valid = true;
    return true;
}

// ── Build the Open-Meteo URL ─────────────────────────────────
static String buildOpenMeteoUrl(float lat, float lon) {
    // Request everything the display needs in one call:
    //   current:  temp, feels-like, humidity, wind, UV, WMO code
    //   hourly:   temp, WMO code, precip probability (168 hours)
    //   daily:    max/min temp, precip sum, wind, UV, WMO code (7 days)
    // forecast_days=7 but we only use 4
    String url = "https://api.open-meteo.com/v1/forecast";
    url += "?latitude="  + String(lat, 4);
    url += "&longitude=" + String(lon, 4);
    url += "&current=temperature_2m,apparent_temperature,relative_humidity_2m";
    url += ",weather_code,wind_speed_10m,wind_gusts_10m,uv_index";
    url += "&hourly=temperature_2m,weather_code,precipitation_probability";
    url += "&daily=weather_code,temperature_2m_max,temperature_2m_min";
    url += ",precipitation_sum,wind_speed_10m_max,wind_gusts_10m_max,uv_index_max";
    url += "&wind_speed_unit=kmh";
    url += "&timezone=auto";
    url += "&forecast_days=7";
    return url;
}

// ── Main fetch function ──────────────────────────────────────
// Call this to fetch weather for one location.
// Returns true on success (also saves to SD cache).
// If WiFi is not available, tries to load from SD cache.
bool fetchWeatherOpenMeteo(uint8_t locIndex,
                           const char* name,
                           float lat, float lon,
                           WeatherData& wd)
{
    wd.valid = false;
    String json;
    bool fromCache = false;

    if (WiFi.status() == WL_CONNECTED) {
        String url = buildOpenMeteoUrl(lat, lon);
        Serial.printf("[Weather] Fetching %s: %s\n", name, url.c_str());

        HTTPClient http;
        http.begin(url);
        http.setTimeout(10000);
        int code = http.GET();

        if (code == HTTP_CODE_OK) {
            json = http.getString();
            http.end();
            saveCacheToSD(locIndex, json);   // persist for offline use
        } else {
            Serial.printf("[Weather] HTTP error %d for %s\n", code, name);
            http.end();
            // Fall through to cache
        }
    }

    if (json.isEmpty()) {
        Serial.printf("[Weather] Loading %s from SD cache\n", name);
        json = loadCacheFromSD(locIndex);
        fromCache = true;
    }

    if (json.isEmpty()) {
        Serial.printf("[Weather] No data available for %s\n", name);
        return false;
    }

    bool ok = parseOpenMeteoJson(json, wd, name, lat, lon);
    if (ok && fromCache) {
        // Mark data source so display can show "Cached" label
        // (optional — use wd.valid as-is; the display code can
        // detect WiFi status independently)
    }
    return ok;
}

// ── Convenience: fetch all configured locations ──────────────
// Fills weatherData[] array defined in the caller.
// numLocations must match config.locations array size.
void fetchAllLocations(WeatherData* weatherData,
                       uint8_t numLocations,
                       const LocationConfig* locations)
{
    for (uint8_t i = 0; i < numLocations; i++) {
        fetchWeatherOpenMeteo(i,
                              locations[i].name,
                              locations[i].lat,
                              locations[i].lon,
                              weatherData[i]);
        // Small delay between requests to be polite to the API
        if (i < numLocations - 1) delay(500);
    }
}
