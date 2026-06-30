/*
 * ============================================================================
 *  M5Paper Color (ESP32-S3) — Multi-mode Station
 *  Locations: Vilnius, Riga, Tallinn | Weather: Open-Meteo (free, no API key)
 * ============================================================================
 *  Up to 5 modes (G1 button, 1 click to rotate; each can be enabled in config "modos"):
 *    1 WEATHER  : Up/Down views -> LOCAL (date/time + sensor) + city forecasts
 *    2 CAROUSEL : full-screen photos from microSD
 *    3 MUSIC    : MP3/M4A/FLAC/WAV player from SD (iPod Shuffle-style controls)
 *    4 BOOKS    : TXT reader (list from /Library, remembers page per book)
 *    5 WI-FI    : starts off; UP turns on / DOWN turns off; joins saved network (IP+QR)
 *                 or creates own AP + QR; web-based SD manager: browse, upload, download,
 *                 edit, delete, ZIP folder, stream photos/video/music (HTTP Range/206),
 *                 PDF viewer, EPUB reader (with libs in /lib on SD).
 *  Hidden mode: G1 double-click = TV-B-Gone (IR blaster), RGB LED feedback.
 *
 *  All configuration lives in /config.json on the microSD (see repo for example).
 *  If the SD or file is missing, built-in defaults are used.
 *
 *  BUTTONS (GPIO, active LOW):
 *    G1=GPIO1 (top): 1 click = change mode | double-click = TV-B-Gone |
 *                    hold = mode action (WEATHER: force WiFi refresh)
 *    UP=GPIO9 / DOWN=GPIO10: mode navigation (volume/track in music, page in book)
 *
 *  FAST START: local screen (RTC time + sensor) is drawn immediately;
 *    WiFi/NTP/weather fetch runs in the background without blocking.
 *  POWER SAVING: light sleep when idle; after 'deep_sleep_minutes' of inactivity
 *    the device powers off via PMIC (M5.Power.powerOff). Wake with POWER button.
 *    E-paper retains the last image when powered off. Music mode stays awake.
 *
 *  HARDWARE: SHT40 0x44 / RTC RX8130CE 0x32 / PMIC M5PM1 0x6E (I2C SDA=3 SCL=2)
 *    SPI e-paper+SD: MOSI=13 MISO=14 SCK=15; EINK_DC=43 CS=44; SD_CS=47
 *    SD/e-paper on PMIC rail L3B -> LDO must be enabled before SD.begin
 *  Libraries: M5Unified, M5GFX, M5UnitENV, M5PM1, ArduinoJson, ESP32-audioI2S, IRremoteESP8266
 *  Arduino IDE: "ESP32S3 Dev Module", Flash 16MB, PSRAM "OPI PSRAM", USB CDC On.
 * ============================================================================
 */

#include <Arduino.h>
#include <M5GFX.h>
#include <M5Unified.h>
#include <M5UnitENV.h>
#ifdef analogRead
#define _ANALOG_READ_SAVED analogRead
#undef analogRead
#endif
#include <M5PM1.h>
#ifdef _ANALOG_READ_SAVED
#define analogRead _ANALOG_READ_SAVED
#undef _ANALOG_READ_SAVED
#endif
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <WebServer.h>      // Wi-Fi mode: web-based SD manager (own AP or STA)
#include <ArduinoJson.h>
#include <esp_sntp.h>
#include <esp_task_wdt.h>   // replaces deprecated <sntp.h> from lwIP
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include <time.h>
#include <vector>
#include "esp_sleep.h"
#include "driver/gpio.h"
#include "Audio.h"        // ESP32-audioI2S (schreibfaul1) v3.4.6
#include <IRremoteESP8266.h>
#include <IRsend.h>
#include "tvbgone_codes.h"   // TV-B-Gone IR codes (hidden mode)
#include "wifi_page.h"       // Wi-Fi mode HTML (raw string in .h to avoid .ino preprocessor issues)

// Forward declarations: Arduino IDE auto-inserts prototypes after #includes
// but before struct definitions; these prevent "not declared" errors.
struct WifiCred; struct Location; struct ClimaDay; struct ClimaCache; struct ZipEnt;

// ============================ DEFAULTS ============================
// Used only when /config.json cannot be read from the SD card.
#define DEF_TZ "EET-2EEST,M3.5.0/3,M10.5.0/4"   // Baltic EET: UTC+2 winter, UTC+3 summer (DST: last Sun Mar/Oct)
#define NTP_SERVER1 "pool.ntp.org"
#define NTP_SERVER2 "time.cloudflare.com"

static constexpr uint32_t WIFI_PER_NET_MS  = 8000;     // connect timeout per network (ms)
static constexpr uint32_t SNTP_TIMEOUT_MS  = 20000;

// Button GPIOs
static constexpr int PIN_BTN_MODE = 1, PIN_BTN_UP = 9, PIN_BTN_DOWN = 10;
// I2C / SPI pins
static constexpr int SHT_SDA_PIN = 3, SHT_SCL_PIN = 2;
static constexpr int SD_SCK_PIN = 15, SD_MISO_PIN = 14, SD_MOSI_PIN = 13, SD_CS_PIN = 47;
#define CONFIG_PATH "/config.json"
#define CLIMA_CACHE_PATH "/.clima.cache"   // weather cache on SD (survives power-off)
#define CLIMA_CACHE_VER  1
#define DEF_FOTOS_DIR  "/Fotos"
#define DEF_MUSICA_DIR "/Music"
#define DEF_AP_SSID  "PaperColor"        // default AP SSID (config: wifi_modo.ap_ssid)
#define DEF_AP_PASS  "papercolor1234"    // default AP password, >= 8 chars (wifi_modo.ap_pass)
#define DEF_WEB_USER "admin"             // default web login username (wifi_modo.user)
#define DEF_WEB_PASS "admin"             // default web login password (wifi_modo.pass)

// ============================ CONFIG (loaded from SD) ============================
struct WifiCred { String ssid, pass; };
struct Location { String name; float lat, lon; };  // coordinates for Open-Meteo

// VLW glyph/font structs (used by the manual VLW renderer further down in this file).
// Defined here, near the top, so Arduino's auto-generated function prototypes
// (which it inserts before any function body, but after #includes) can see these
// custom return/parameter types — otherwise "does not name a type" compile errors.
struct VlwGlyph { uint32_t cp; int16_t h, w, advance, topExtent, leftExtent; uint32_t bitmapOffset; };
struct VlwFontInfo {
  const uint8_t* buf = nullptr;
  int gCount = 0;
  int tableEnd = 0;
  int fontSize = 0;   // nominal font size in px (header field)
  bool ready = false;
  // Precomputed bitmap offset for each glyph index, filled once at parse time.
  // Without this, finding a glyph's bitmap requires summing h*w for every glyph
  // AFTER it (reverse storage order) on EVERY character drawn — for a ~700-char
  // page against a ~250-glyph table, that's >150,000 wasted iterations, slow
  // enough to trip the watchdog timer mid-render. Precomputing once at load
  // time turns each glyph lookup into a single array read.
  std::vector<uint32_t> bitmapOffsets;
};

std::vector<WifiCred> g_wifi;
std::vector<Location> g_locs;
String   g_tz = DEF_TZ;
uint32_t g_carouselMs = 300000;   // 5 min
String   g_fotosDir  = DEF_FOTOS_DIR;
String   g_musicaDir = DEF_MUSICA_DIR;
String   g_librosDir = "/Library";               // books folder (mode 4)
bool     g_photoAutoRotate = true;   // rotate panel to match photo orientation

// Wi-Fi mode (web-based SD manager, own AP or STA)
WebServer g_server(80);
File      g_upFile;                              // file handle used during an upload
// Shared I/O buffer for /dl and ZIP responses. Allocated in PSRAM when Wi-Fi mode
// starts, freed when it stops. Single-threaded server: never two concurrent requests.
#define   WEB_BUF_SZ 32768
uint8_t*  g_webBuf = nullptr;
size_t    g_webBufSz = 0;
bool      g_wifiModeOn = false;
bool      g_apMode = false;                      // true = own AP; false = joined a saved network (STA)
bool      g_wifiStaFetch = false;                // pending: refresh weather while STA is active
String    g_apSsid  = DEF_AP_SSID;               // AP SSID (config: wifi_modo.ap_ssid)
String    g_apPass  = DEF_AP_PASS;               // AP password (>= 8 chars; wifi_modo.ap_pass)
String    g_webUser = DEF_WEB_USER;              // web login username (wifi_modo.user)
String    g_webPass = DEF_WEB_PASS;              // web login password (wifi_modo.pass)

// Mode enum must be declared before any function: Arduino auto-generates
// the nextEnabledMode(Mode) prototype before the first sketch function (numLocs),
// so Mode must already be defined here.
enum Mode { MODE_CLIMA = 0, MODE_CARRUSEL, MODE_MUSICA, MODE_LIBRO, MODE_WIFI, MODE_COUNT };
const char* MODE_NAMES[] = {"CLIMA", "CARRUSEL", "MUSICA", "LIBRO", "WIFI"};
bool g_modeEnabled[MODE_COUNT] = {true, true, true, true, true};  // enable/disable modes (config "modos")

int numLocs()  { return (int)g_locs.size(); }
int numViews() { return numLocs() + 1; }   // vista 0 = LOCAL

// ============================ GLOBAL STATE ============================

M5Canvas   canvas(&M5.Display);
M5PM1      pm1;
SHT4X      sht4;
Preferences prefs;
m5::Button_Class btnMode, btnUp, btnDown;

Mode  g_mode = MODE_CLIMA;
bool  g_needRedraw = true, g_busy = false;
float g_temp = NAN, g_hum = NAN;
bool  sht_ready = false, sd_ready = false;
// Smart refresh of the LOCAL view
uint32_t g_lastInput = 0;          // last button press (used for 5-min idle check)

// Carousel
std::vector<String> g_images;
size_t g_img_idx = 0;
int g_rot = -1;   // current panel rotation (1=landscape, 0=portrait)

// Music (iPod Shuffle style)
Audio  audio;                // audio engine (I2S + decoder)
std::vector<String> g_music;
size_t g_track = 0;
int    g_volume = 12;        // 0..21
bool   g_playing = false;
bool   g_loaded = false;     // true when a track is loaded
bool   g_audioReady = false; // ES8311 + I2S initialised
bool   g_audioPowered = false; // codec/amp powered (only in music mode, for battery saving)
// Battery saving
static constexpr uint32_t IDLE_SLEEP_MS = 4000;       // margin after last button before sleep
uint32_t g_deepSleepMs = 3600000UL;                   // inactivity -> deep sleep (config: deep_sleep_minutes)
uint32_t g_ignoreInputUntil = 0;                      // ignore buttons just after boot/wake
bool     g_bootFetchPending = true;                   // boot: fetch WiFi/NTP/weather after first draw
time_t   g_lastActiveSec = 0;                         // RTC timestamp of last real button press
time_t   g_lastLocalUpdateSec = 0;                    // last LOCAL clock refresh (RTC seconds)
time_t   g_lastCarouselSec = 0;                       // last carousel advance (RTC seconds)
volatile bool g_trackEnded = false;   // set by audio_eof_* callback when a track finishes
String   g_titleFontPath = "/fonts/cyrillic_title.vlw";
bool     g_titleFontReady = false;

// Books (TXT reader)
std::vector<String> g_books;
int      g_bookIdx = 0;
uint32_t g_pageStart = 0, g_nextPageStart = 0;
bool     g_fileIsUtf16 = false;   // true if current book file is UTF-16 encoded
size_t   g_utf16RawStart = 0;     // byte offset in file where UTF-16 content starts (after BOM)
std::vector<uint32_t> g_pageStack;    // page offset history for "previous page"
String   g_bodyFontPath  = "/fonts/cyrillic_body.vlw";
bool     g_bodyFontReady = false;
enum LibState { LIB_LIST, LIB_READING };
LibState g_libState = LIB_LIST;       // on entering book mode: show file selector
int      g_sel = 0;                   // highlighted book in the selector
// Audio pins (schematic C151): ES8311 @ internal I2C 0x18, MCLK=BCLK (no separate MCLK pin)
static constexpr int I2S_BCLK_PIN = 40, I2S_LRCK_PIN = 41, I2S_DOUT_PIN = 38;
static constexpr int PIN_CODEC_EN = 45, PIN_SPK_EN = 46;
static constexpr uint8_t ES8311_ADDR = 0x18;
// IR transmitter (hidden TV-B-Gone mode)
static constexpr int IR_TX_PIN = 48;
IRsend irsend(IR_TX_PIN);

// Weather
int g_view = 0;   // 0=LOCAL, 1..N = city (loc index = g_view-1)
struct ClimaDay {
  char fecha[12]; int wday, tMax, tMin, probLluvia, humMax, humMin; char cielo[40];
  int vientoVel, racha, uvMax;                     // wind km/h, gust km/h, UV index
  char vientoDir[5];                               // wind direction (N, NE, NW...)
  int wmoCode;                                     // WMO weather code (Open-Meteo)
};
struct ClimaHour { int hour, temp; char cielo[32]; int wmoCode; };  // hourly forecast (icon + temp)
struct ClimaCache {
  ClimaDay day[7]; int nDays; bool valid; int updH, updM;
  bool obsValid; float obsTemp, obsHum; char obsTime[6];
  ClimaHour hour[6]; int nHours;                   // proximas horas (HOY)
};
std::vector<ClimaCache> g_clima;

const char* DIAS_C[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
const char* DIAS[]   = {"Sunday","Monday","Tuesday","Wednesday","Thursday","Friday","Saturday"};
const char* MESES[]  = {"January","February","March","April","May","June",
                        "July","August","September","October","November","December"};

// ============================ CONFIG: load / defaults ============================
void loadDefaults() {
  g_wifi.clear();   g_wifi.push_back({"YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD"});
  g_tz = DEF_TZ;
  g_carouselMs = 300000;
  g_fotosDir  = DEF_FOTOS_DIR;
  g_musicaDir = DEF_MUSICA_DIR;
  g_librosDir = "/Library";
  g_photoAutoRotate = true;
  g_deepSleepMs = 3600000UL;   // 60 minutes
  for (int i = 0; i < MODE_COUNT; i++) g_modeEnabled[i] = true;
  g_apSsid = DEF_AP_SSID; g_apPass = DEF_AP_PASS; g_webUser = DEF_WEB_USER; g_webPass = DEF_WEB_PASS;
  g_locs.clear();
  g_locs.push_back({"Vilnius",  54.6872f, 25.2797f});
  g_locs.push_back({"Riga",     56.9460f, 24.1059f});
  g_locs.push_back({"Tallinn",  59.4370f, 24.7536f});
}

bool loadConfig() {
  if (!sd_ready) return false;
  File f = SD.open(CONFIG_PATH);
  if (!f) { Serial.println("No /config.json found (using defaults)."); return false; }
  JsonDocument doc;
  DeserializationError e = deserializeJson(doc, f);
  f.close();
  if (e) { Serial.printf("config.json error: %s (uso defaults)\n", e.c_str()); return false; }

  // Wi-Fi networks
  g_wifi.clear();
  for (JsonObject w : doc["wifi"].as<JsonArray>()) {
    WifiCred c; c.ssid = (const char*)(w["ssid"] | ""); c.pass = (const char*)(w["pass"] | "");
    if (c.ssid.length()) g_wifi.push_back(c);
  }
  // Settings
  if (doc["timezone"].is<const char*>()) g_tz = (const char*)doc["timezone"];
  if (doc["carousel_seconds"].is<int>()) g_carouselMs = (uint32_t)doc["carousel_seconds"] * 1000UL;
  if (doc["fotos_dir"].is<const char*>())  g_fotosDir  = (const char*)doc["fotos_dir"];
  if (doc["musica_dir"].is<const char*>()) g_musicaDir = (const char*)doc["musica_dir"];
  else g_musicaDir = "/Music";
  if (doc["libros_dir"].is<const char*>()) g_librosDir = (const char*)doc["libros_dir"];
  if (doc["photo_auto_rotate"].is<bool>()) g_photoAutoRotate = doc["photo_auto_rotate"];
  if (doc["deep_sleep_minutes"].is<int>()) g_deepSleepMs = (uint32_t)doc["deep_sleep_minutes"] * 60000UL;
  // Active modes (missing key = enabled). If ALL are disabled, re-enable all.
  g_modeEnabled[MODE_CLIMA]    = doc["modos"]["clima"]    | true;
  g_modeEnabled[MODE_CARRUSEL] = doc["modos"]["carrusel"] | true;
  g_modeEnabled[MODE_MUSICA]   = doc["modos"]["musica"]   | true;
  g_modeEnabled[MODE_LIBRO]    = doc["modos"]["libro"]    | true;
  g_modeEnabled[MODE_WIFI]     = doc["modos"]["wifi"]     | true;
  if (doc["wifi_modo"]["ap_ssid"].is<const char*>()) g_apSsid  = (const char*)doc["wifi_modo"]["ap_ssid"];
  if (doc["wifi_modo"]["ap_pass"].is<const char*>()) g_apPass  = (const char*)doc["wifi_modo"]["ap_pass"];
  if (doc["wifi_modo"]["user"].is<const char*>())    g_webUser = (const char*)doc["wifi_modo"]["user"];
  if (doc["wifi_modo"]["pass"].is<const char*>())    g_webPass = (const char*)doc["wifi_modo"]["pass"];
  { bool any = false; for (int i = 0; i < MODE_COUNT; i++) any |= g_modeEnabled[i];
    if (!any) { for (int i = 0; i < MODE_COUNT; i++) g_modeEnabled[i] = true;
                Serial.println("config: all modes disabled -> re-enabling all"); } }
  // Locations
  g_locs.clear();
  for (JsonObject l : doc["locations"].as<JsonArray>()) {
    Location loc;
    loc.name = (const char*)(l["name"] | "");
    loc.lat = l["lat"] | 0.0f;
    loc.lon = l["lon"] | 0.0f;
    if (loc.name.length()) g_locs.push_back(loc);
  }
  // Fallbacks if arrays are empty after parsing
  if (g_wifi.empty() || g_locs.empty()) {
    Serial.println("config.json incomplete; filling with defaults.");
    if (g_wifi.empty()) g_wifi.push_back({"YOUR_WIFI_SSID", "YOUR_WIFI_PASSWORD"});
    if (g_locs.empty()) { g_locs.push_back({"Vilnius", 54.6872f, 25.2797f}); }
  }
  Serial.printf("config.json OK: %u networks, %u locations (Open-Meteo)\n",
                (unsigned)g_wifi.size(), (unsigned)g_locs.size());
  return true;
}

// ============================ SENSOR / SD / NETWORK ============================
bool initSHT40() {
  i2c_port_t p = M5.In_I2C.getPort();
  TwoWire* w = (p == I2C_NUM_1) ? &Wire1 : &Wire;
  return sht4.begin(w, SHT40_I2C_ADDR_44, SHT_SDA_PIN, SHT_SCL_PIN, 400000U);
}
void readSHT40() { if (sht_ready && sht4.update()) { g_temp = sht4.cTemp; g_hum = sht4.humidity; } }

bool isImageFile(const String& n0) {
  String n = n0; n.toLowerCase();
  return n.endsWith(".jpg")||n.endsWith(".jpeg")||n.endsWith(".png")||n.endsWith(".bmp");
}
void loadImageList(const char* dirpath) {
  g_images.clear();
  File dir = SD.open(dirpath);
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return; }
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    if (!f.isDirectory() && isImageFile(f.path())) g_images.push_back(f.path());
    f.close();
  }
  dir.close();
}
bool initSD() {
  SPI.begin(SD_SCK_PIN, SD_MISO_PIN, SD_MOSI_PIN, SD_CS_PIN);
  // max_files=10: recursive ZIP download opens one handle per directory level (default is only 5).
  // SPI clock: try 40 MHz (2x read speed for web/music/video); falls back to 20 MHz if the card
  // or tracks fail. If you see corrupted reads, lower SD_FREQ_HZ to 20000000.
  const uint32_t SD_FREQ_HZ = 40000000;
  if (!SD.begin(SD_CS_PIN, SPI, SD_FREQ_HZ, "/sd", 10)) {
    Serial.println("microSD: 40 MHz failed, retrying at 20 MHz...");
    if (!SD.begin(SD_CS_PIN, SPI, 20000000, "/sd", 10)) { Serial.println("microSD not detected."); return false; }
  }
  Serial.println("microSD mounted.");
  return true;
}
// Scan photos in the configured folder (falls back to root if empty).
void scanImages() {
  if (!sd_ready) return;
  loadImageList(g_fotosDir.c_str());
  if (g_images.empty() && g_fotosDir != "/") loadImageList("/");
  Serial.printf("Photos in %s: %u\n", g_fotosDir.c_str(), (unsigned)g_images.size());
}

// VLW Cyrillic fonts, loaded via M5.Display.loadFont(const uint8_t* array, ...).
// This M5GFX version's loadFont(File&) and loadFont(SD, path) overloads both fail to
// compile here (incomplete DataWrapperT<fs::SDFS> specialization / no File overload at
// all). The RAM-buffer overload is implemented directly in M5GFX with no DataWrapper
// involved, so it sidesteps that whole broken code path. We read the .vlw file into a
// PSRAM buffer once at boot and keep it alive for the life of the program.
static uint8_t* g_bodyFontBuf = nullptr;
static size_t   g_bodyFontSize = 0;

static uint8_t* readFileToBuf(const String& path, size_t& outSize) {
  outSize = 0;
  if (!sd_ready) return nullptr;
  File f = SD.open(path.c_str(), FILE_READ);
  if (!f) { Serial.printf("Font missing: %s\n", path.c_str()); return nullptr; }
  size_t sz = f.size();
  if (sz < 20 || sz > 2000000UL) { f.close(); return nullptr; }
  uint8_t* buf = (uint8_t*)heap_caps_malloc(sz, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
  if (!buf) buf = (uint8_t*)malloc(sz);
  if (!buf) { f.close(); return nullptr; }
  if (f.read(buf, sz) != sz) { free(buf); f.close(); return nullptr; }
  f.close();
  outSize = sz;
  Serial.printf("VLW read OK: %s (%u bytes)\n", path.c_str(), (unsigned)sz);
  return buf;
}

void loadFonts() {
  if (!sd_ready) { g_titleFontReady = false; g_bodyFontReady = false; return; }
  g_bodyFontBuf = readFileToBuf(g_bodyFontPath, g_bodyFontSize);
  g_bodyFontReady = (g_bodyFontBuf != nullptr);
  Serial.printf("Body VLWfont:  %s\n", g_bodyFontReady ? "OK" : "FAIL");
  // Title font not currently used for rendering; keep the ready-flag for compatibility.
  g_titleFontReady = SD.exists(g_titleFontPath.c_str());
  Serial.printf("Title VLWfont: %s\n", g_titleFontReady ? "OK" : "FAIL");
}

// List books (.txt) from /Library.
// Scan SD root for a directory whose name matches 'target' case-insensitively.
// Returns the exact on-disk name, or "" if not found.
static String findDirCaseInsensitive(const String& target) {
  File root = SD.open("/");
  if (!root) return "";
  String tLow = target; tLow.toLowerCase();
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    if (!f.isDirectory()) { f.close(); continue; }
    String nm = f.name(); String nmLow = nm; nmLow.toLowerCase();
    if (nmLow == tLow) { String found = "/" + nm; f.close(); root.close(); return found; }
    f.close();
  }
  root.close();
  return "";
}

void scanBooks() {
  g_books.clear();
  if (!sd_ready) { Serial.println("[BOOKS] SD not ready"); return; }

  // Try configured path first; if it fails, search for it case-insensitively.
  File dir = SD.open(g_librosDir.c_str());
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    Serial.printf("[BOOKS] '%s' not found, searching SD root...\n", g_librosDir.c_str());
    // Extract just the leaf name (e.g. "Library" from "/Library")
    String leaf = g_librosDir;
    int sl = leaf.lastIndexOf('/');
    if (sl >= 0) leaf = leaf.substring(sl + 1);
    String found = findDirCaseInsensitive(leaf);
    if (found.length() == 0) {
      Serial.printf("[BOOKS] No directory matching '%s' on SD root\n", leaf.c_str());
      // List SD root so user can see what's actually there
      File root = SD.open("/");
      if (root) {
        Serial.println("[BOOKS] SD root contents:");
        for (File f = root.openNextFile(); f; f = root.openNextFile()) {
          Serial.printf("  %s %s\n", f.isDirectory() ? "[DIR] " : "[FILE]", f.path());
          f.close();
        }
        root.close();
      }
      return;
    }
    Serial.printf("[BOOKS] Using '%s' instead of '%s'\n", found.c_str(), g_librosDir.c_str());
    g_librosDir = found;   // update so future scans and SD.open() use the correct path
    dir = SD.open(g_librosDir.c_str());
    if (!dir || !dir.isDirectory()) { if (dir) dir.close(); Serial.println("[BOOKS] Still failed"); return; }
  }

  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String nm = f.name();  // filename only, no path
    if (nm.length() && nm[0] == '.') { f.close(); continue; }  // skip hidden files
    if (!f.isDirectory()) {
      String n = f.path(); String low = n; low.toLowerCase();
      if (low.endsWith(".txt")) g_books.push_back(n);
    }
    f.close();
  }
  dir.close();
  Serial.printf("Books (.txt) in %s: %u\n", g_librosDir.c_str(), (unsigned)g_books.size());
  for (auto& b : g_books) Serial.printf("  -> %s\n", b.c_str());
}

bool connectWiFi() {
  if (WiFi.status() == WL_CONNECTED) return true;
  WiFi.mode(WIFI_STA);
  for (auto& c : g_wifi) {
    Serial.printf("WiFi: trying '%s'...\n", c.ssid.c_str());
    WiFi.begin(c.ssid.c_str(), c.pass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED) {
      if (millis() - t0 >= WIFI_PER_NET_MS) break;
      delay(200);
    }
    if (WiFi.status() == WL_CONNECTED) {
      Serial.printf("WiFi connected (%s): %s\n", c.ssid.c_str(), WiFi.localIP().toString().c_str());
      return true;
    }
    WiFi.disconnect();
  }
  Serial.println("Could not connect to any saved network.");
  return false;
}
void wifiOff() { WiFi.disconnect(true); WiFi.mode(WIFI_OFF); }

bool syncRtcFromSntp() {
  configTzTime(g_tz.c_str(), NTP_SERVER1, NTP_SERVER2);
  uint32_t t0 = millis();
  while (sntp_get_sync_status() != SNTP_SYNC_STATUS_COMPLETED) {
    if (millis() - t0 >= SNTP_TIMEOUT_MS) return false;
    delay(400);
  }
  time_t now = time(nullptr) + 1;
  while (now > time(nullptr)) delay(10);
  struct tm* lt = localtime(&now);
  if (!lt) return false;
  M5.Rtc.setDateTime(lt);
  Serial.printf("RTC synced via NTP -> %02d:%02d:%02d (TZ=%s)\n",
                lt->tm_hour, lt->tm_min, lt->tm_sec, g_tz.c_str());
  return true;
}

int wdayFromFecha(const char* f) {
  struct tm t = {}; int y, m, d;
  if (sscanf(f, "%d-%d-%d", &y, &m, &d) != 3) return 0;
  t.tm_year = y - 1900; t.tm_mon = m - 1; t.tm_mday = d; t.tm_hour = 12;
  time_t tt = mktime(&t);
  struct tm* r = localtime(&tt);
  return r ? r->tm_wday : 0;
}

// --- Message helpers (used by fetchAllOnline and mode draw functions) ---
void drawCenteredMsg(const char* msg, int y, uint16_t color = BLACK,
                     const lgfx::IFont* font = &fonts::FreeSansBold18pt7b) {
  canvas.setFont(font);
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(color);
  canvas.drawString(msg, M5.Display.width() / 2, y);
}
void showBusy(const char* msg) {
  g_busy = true;
  canvas.fillSprite(WHITE);
  drawCenteredMsg(msg, M5.Display.height() / 2);
  esp_task_wdt_reset(); canvas.pushSprite(0, 0);
}

// Buffer backed by PSRAM, used as a Stream for HTTPClient::writeToStream.
// Receives the full HTTP body (de-chunks it) then lets us parse JSON from memory.
struct PsBuf : public Stream {
  char* data = nullptr; size_t len = 0, cap = 0; bool ok = true;
  ~PsBuf() { if (data) free(data); }
  size_t write(uint8_t b) override { return write(&b, 1); }
  size_t write(const uint8_t* p, size_t n) override {
    if (!ok) return 0;
    if (len + n + 1 > cap) {
      size_t ncap = cap ? cap : 16384;
      while (len + n + 1 > ncap) ncap += ncap / 2 + 1;
      char* nb = (char*)ps_realloc(data, ncap);
      if (!nb) { ok = false; return 0; }
      data = nb; cap = ncap;
    }
    memcpy(data + len, p, n); len += n; data[len] = 0; return n;
  }
  int available() override { return 0; }
  int read() override { return -1; }
  int peek() override { return -1; }
  void flush() override {}
};

// ============================ OPEN-METEO WEATHER ============================
// Free weather API, no API key required, global coverage.
// API: https://api.open-meteo.com/v1/forecast
// License: CC BY 4.0 — display "Open-Meteo" as source on screen.

// WMO weather code -> short English description
const char* wmoDescription(int code) {
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
    case 61: return "Light rain";
    case 63: return "Rain";
    case 65: return "Heavy rain";
    case 66: return "Freezing rain";
    case 71: return "Light snow";
    case 73: return "Snow";
    case 75: return "Heavy snow";
    case 77: return "Snow grains";
    case 80: return "Light showers";
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

// WMO code -> wind direction abbreviation from degrees
const char* windDirStr(int deg) {
  static const char* dirs[] = {"N","NNE","NE","ENE","E","ESE","SE","SSE",
                                "S","SSW","SW","WSW","W","WNW","NW","NNW"};
  return dirs[((deg + 11) / 22) % 16];
}

// Build Open-Meteo forecast URL for a location
String buildOpenMeteoUrl(float lat, float lon) {
  String url = "https://api.open-meteo.com/v1/forecast";
  url += "?latitude="  + String(lat, 4);
  url += "&longitude=" + String(lon, 4);
  url += "&current=temperature_2m,apparent_temperature,relative_humidity_2m";
  url += ",weather_code,wind_speed_10m,wind_gusts_10m,wind_direction_10m,uv_index";
  url += "&hourly=temperature_2m,weather_code,precipitation_probability";
  url += "&daily=weather_code,temperature_2m_max,temperature_2m_min";
  url += ",precipitation_sum,wind_speed_10m_max,wind_gusts_10m_max,wind_direction_10m_dominant";
  url += ",uv_index_max,relative_humidity_2m_max,relative_humidity_2m_min";
  url += "&wind_speed_unit=kmh&timezone=auto&forecast_days=4";
  return url;
}

bool openMeteoFetch(int idx) {
  if (idx < 0 || idx >= numLocs()) return false;
  Location& loc = g_locs[idx];
  if (loc.lat == 0.0f && loc.lon == 0.0f) {
    Serial.printf("Open-Meteo: no lat/lon for %s\n", loc.name.c_str());
    return false;
  }

  String url = buildOpenMeteoUrl(loc.lat, loc.lon);
  Serial.printf("Open-Meteo fetch %s: %s\n", loc.name.c_str(), url.c_str());

  HTTPClient http;
  http.begin(url);
  http.setTimeout(15000);
  int code = http.GET();
  if (code != 200) {
    Serial.printf("Open-Meteo HTTP %d for %s\n", code, loc.name.c_str());
    http.end();
    return false;
  }

  // Buffer body to PSRAM before parsing (avoids IncompleteInput on large JSON)
  PsBuf pb;
  http.writeToStream(&pb);
  http.end();
  if (!pb.ok || pb.len == 0) { Serial.println("Open-Meteo: empty body"); return false; }

  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, pb.data, pb.len);
  if (err) { Serial.printf("Open-Meteo JSON err: %s\n", err.c_str()); return false; }

  ClimaCache& cc = g_clima[idx];
  cc.nDays = 0;
  cc.nHours = 0;
  cc.obsValid = false;

  // ── Current conditions -> stored as obsValid observation ──────────────────
  JsonObject cur = doc["current"];
  if (!cur.isNull()) {
    cc.obsTemp  = (float)(cur["temperature_2m"] | 0.0f);
    cc.obsHum   = cur["relative_humidity_2m"] | 0.0f;
    // Store current time as obsTime (HH:MM from the "time" field "YYYY-MM-DDTHH:MM")
    const char* ct = cur["time"] | "";
    if (strlen(ct) >= 16) strlcpy(cc.obsTime, ct + 11, sizeof(cc.obsTime));
    else cc.obsTime[0] = 0;
    cc.obsValid = true;
    // Also populate day[0] current wind/UV for the "Today" display block
    // We pre-fill these here; they get overwritten by the daily block below if present.
    if (cc.nDays == 0) {
      cc.day[0].vientoVel = (int)roundf((float)(cur["wind_speed_10m"]  | 0.0f));
      cc.day[0].racha     = (int)roundf((float)(cur["wind_gusts_10m"]  | 0.0f));
      cc.day[0].uvMax     = (int)roundf((float)(cur["uv_index"]        | 0.0f));
      int wdir            = (int)roundf(cur["wind_direction_10m"]| 0.0f);
      strlcpy(cc.day[0].vientoDir, windDirStr(wdir), sizeof(cc.day[0].vientoDir));
      cc.day[0].wmoCode   = cur["weather_code"] | 0;
      strlcpy(cc.day[0].cielo, wmoDescription(cc.day[0].wmoCode), sizeof(cc.day[0].cielo));
    }
  }

  // ── Hourly forecast (today, next 6 slots from current hour) ───────────────
  JsonObject hourly = doc["hourly"];
  if (!hourly.isNull()) {
    JsonArray times  = hourly["time"];
    JsonArray temps  = hourly["temperature_2m"];
    JsonArray codes  = hourly["weather_code"];
    JsonArray precip = hourly["precipitation_probability"];
    // Find the current hour index in the array
    int curH = M5.Rtc.getDateTime().time.hours;
    int startIdx = 0;
    for (int i = 0; i < (int)times.size(); i++) {
      const char* t = times[i] | "";
      if (strlen(t) >= 16 && atoi(t + 11) >= curH) { startIdx = i; break; }
    }
    for (int i = 0; i < 6 && (startIdx + i) < (int)times.size(); i++) {
      const char* t = times[startIdx + i] | "";
      if (strlen(t) < 16) continue;
      cc.hour[i].hour    = atoi(t + 11);
      cc.hour[i].temp    = (int)roundf((float)(temps[startIdx + i] | 0.0f));
      cc.hour[i].wmoCode = (int)(codes[startIdx + i] | 0);
      strlcpy(cc.hour[i].cielo,
              wmoDescription(cc.hour[i].wmoCode),
              sizeof(cc.hour[i].cielo));
      cc.nHours++;
    }
  }

  // ── Daily forecast (today + 3 days) ───────────────────────────────────────
  JsonObject daily = doc["daily"];
  if (!daily.isNull()) {
    JsonArray dates   = daily["time"];
    JsonArray maxT    = daily["temperature_2m_max"];
    JsonArray minT    = daily["temperature_2m_min"];
    JsonArray precip  = daily["precipitation_sum"];
    JsonArray wind    = daily["wind_speed_10m_max"];
    JsonArray gusts   = daily["wind_gusts_10m_max"];
    JsonArray wdir    = daily["wind_direction_10m_dominant"];
    JsonArray uvMax   = daily["uv_index_max"];
    JsonArray humMax  = daily["relative_humidity_2m_max"];
    JsonArray humMin  = daily["relative_humidity_2m_min"];
    JsonArray wcodes  = daily["weather_code"];

    for (int i = 0; i < 4 && i < (int)dates.size(); i++) {
      ClimaDay& cd = cc.day[i];
      strlcpy(cd.fecha, dates[i] | "", sizeof(cd.fecha));
      cd.wday      = wdayFromFecha(cd.fecha);
      cd.tMax      = (int)roundf(maxT[i]   | 0.0f);
      cd.tMin      = (int)roundf(minT[i]   | 0.0f);
      cd.humMax    = (int)roundf(humMax[i] | 0.0f);
      cd.humMin    = (int)roundf(humMin[i] | 0.0f);
      cd.uvMax     = (int)roundf(uvMax[i]  | 0.0f);
      // probLluvia: derive from precipitation sum (>0.5mm = likely)
      float ps     = precip[i] | 0.0f;
      cd.probLluvia = (ps > 5.0f) ? 80 : (ps > 1.0f) ? 50 : (ps > 0.1f) ? 20 : 0;
      cd.vientoVel = (int)roundf(wind[i]   | 0.0f);
      cd.racha     = (int)roundf(gusts[i]  | 0.0f);
      int wd       = (int)roundf(wdir[i]   | 0.0f);
      strlcpy(cd.vientoDir, windDirStr(wd), sizeof(cd.vientoDir));
      cd.wmoCode   = wcodes[i] | 0;
      strlcpy(cd.cielo, wmoDescription(cd.wmoCode), sizeof(cd.cielo));
      cc.nDays++;
    }
  }

  auto dt = M5.Rtc.getDateTime();
  cc.updH = dt.time.hours; cc.updM = dt.time.minutes;
  cc.valid = cc.nDays > 0;
  Serial.printf("Open-Meteo %s: %d days, %d hours\n",
                loc.name.c_str(), cc.nDays, cc.nHours);
  return cc.valid;
}

// Note: PsBuf is defined before openMeteoFetch (above) because openMeteoFetch uses it.

// Cache of weather data on SD (binary dump of ClimaCache array).
// Survives power-off: last data shown instantly on boot before new fetch.
void saveClima() {
  if (!sd_ready) return;
  File f = SD.open(CLIMA_CACHE_PATH, FILE_WRITE);
  if (!f) return;
  uint8_t hdr[4] = {'C', 'L', CLIMA_CACHE_VER, (uint8_t)numLocs()};
  f.write(hdr, 4);
  for (int i = 0; i < numLocs(); i++) f.write((const uint8_t*)&g_clima[i], sizeof(ClimaCache));
  f.close();
}
void loadClima() {
  if (!sd_ready) return;
  File f = SD.open(CLIMA_CACHE_PATH, FILE_READ);
  if (!f) return;
  uint8_t hdr[4];
  if (f.read(hdr, 4) == 4 && hdr[0] == 'C' && hdr[1] == 'L'
      && hdr[2] == CLIMA_CACHE_VER && hdr[3] == numLocs()) {
    for (int i = 0; i < numLocs(); i++) f.read((uint8_t*)&g_clima[i], sizeof(ClimaCache));
    Serial.println("Weather: cache loaded from SD.");
  }
  f.close();
}

// Fetch NTP + weather for all locations (assumes WiFi already connected).
void fetchWeatherData(bool withNtp) {
  if (withNtp) syncRtcFromSntp();
  for (int i = 0; i < numLocs(); i++) {
    Serial.printf("Fetching %s...\n", g_locs[i].name.c_str());
    openMeteoFetch(i);
    if (i < numLocs() - 1) { esp_task_wdt_reset(); delay(300); }
  }
  saveClima();
}

// Connect WiFi, fetch all, disconnect.
void fetchAllOnline(bool withNtp) {
  if (!connectWiFi()) return;
  fetchWeatherData(withNtp);
  wifiOff();
}

// ============================ DRAWING ============================
// --- Weather icons (use the e-paper colour palette) ---
void fillCloud(int cx, int cy, int s, uint16_t col) {
  canvas.fillCircle(cx - s, cy, (s * 2) / 3, col);
  canvas.fillCircle(cx + s, cy, (s * 2) / 3, col);
  canvas.fillCircle(cx, cy - s / 3, s, col);
  canvas.fillRect(cx - s, cy - s / 3, 2 * s, (s * 2) / 3 + 2, col);
}
void drawWeatherIcon(int cx, int cy, int s, const char* desc) {
  String d = desc ? desc : ""; d.toLowerCase();
  // Matches English descriptions from wmoDescription() (Open-Meteo WMO codes)
  bool storm = d.indexOf("thunder") >= 0 || d.indexOf("t-storm") >= 0;
  bool snow  = !storm && (d.indexOf("snow") >= 0 || d.indexOf("blizzard") >= 0 || d.indexOf("sleet") >= 0);
  bool rain  = !storm && !snow && (d.indexOf("rain") >= 0 || d.indexOf("shower") >= 0 || d.indexOf("drizzle") >= 0);
  bool fog   = d.indexOf("fog") >= 0 || d.indexOf("mist") >= 0 || d.indexOf("haze") >= 0;
  bool partly= d.indexOf("partly") >= 0 || d.indexOf("mainly clear") >= 0;
  bool cloudy= d.indexOf("overcast") >= 0 || d.indexOf("cloudy") >= 0;
  bool hasCloud = storm || snow || rain || cloudy || partly;

  if (fog) {
    for (int i = 0; i < 3; i++) canvas.fillRect(cx - s, cy - s / 2 + i * (s / 2), 2 * s, s / 6 + 1, BLUE);
    return;
  }
  if (!hasCloud || partly) {                       // sun (alone, or peeking behind cloud)
    int sx = hasCloud ? cx - s / 2 : cx;
    int sy = hasCloud ? cy - s / 2 : cy;
    int sr = hasCloud ? s / 2 : (s * 2) / 3;
    if (!hasCloud)
      for (int a = 0; a < 360; a += 45) {
        float r = a * 3.14159f / 180.0f;
        canvas.drawLine(sx + cosf(r) * sr * 1.3f, sy + sinf(r) * sr * 1.3f,
                        sx + cosf(r) * sr * 1.7f, sy + sinf(r) * sr * 1.7f, YELLOW);
      }
    canvas.fillCircle(sx, sy, sr, YELLOW);
  }
  if (hasCloud) {                                  // cloud with black outline
    int ccy = cy + s / 4;
    fillCloud(cx, ccy, (s * 2) / 3, BLACK);
    fillCloud(cx, ccy, (s * 2) / 3 - 3, WHITE);
  }
  int py = cy + s + 2;
  if (rain)  for (int i = -1; i <= 1; i++) canvas.fillCircle(cx + i * (s / 2), py, 2, BLUE);
  if (storm) canvas.fillTriangle(cx - 3, cy + s / 3, cx + 7, cy + s / 3, cx - 1, py + 5, YELLOW);
  if (snow)  for (int i = -1; i <= 1; i++) { int x = cx + i * (s / 2); canvas.drawLine(x - 3, py, x + 3, py, BLUE); canvas.drawLine(x, py - 3, x, py + 3, BLUE); }
}

// UV index colour using the panel palette (Spectra 6): low=green, moderate=yellow, high=red.
uint16_t uvColor(int uv) { return (uv <= 2) ? GREEN : (uv <= 5) ? YELLOW : RED; }

// Hourly forecast strip: hour + small icon + temperature, in N columns.
void drawHourly(ClimaCache& cc, int y0) {
  if (cc.nHours <= 0) return;
  const int W = M5.Display.width();
  int n = cc.nHours > 5 ? 5 : cc.nHours;
  int cw = W / n;
  for (int i = 0; i < n; i++) {
    ClimaHour& h = cc.hour[i];
    int cx = cw * i + cw / 2;
    if (i > 0) canvas.drawFastVLine(cw * i, y0 + 2, 70, BLACK);   // column divider
    char hl[8]; snprintf(hl, sizeof(hl), "%dh", h.hour);
    canvas.setFont(&fonts::FreeSansBold12pt7b); canvas.setTextDatum(top_center); canvas.setTextColor(BLACK);
    canvas.drawString(hl, cx, y0);
    drawWeatherIcon(cx, y0 + 33, 13, h.cielo);
    char tt[8]; snprintf(tt, sizeof(tt), "%dC", h.temp);
    canvas.setTextDatum(bottom_center); canvas.setTextColor(BLACK);
    canvas.drawString(tt, cx, y0 + 73);
  }
}

// Today's highlighted block: icon + temps + UV + sky + wind + rain + humidity + hourly strip.
void drawToday(ClimaCache& cc, int top, int bottom) {
  const int W = M5.Display.width();
  ClimaDay& d = cc.day[0];
  canvas.setFont(&fonts::FreeSansBold18pt7b); canvas.setTextDatum(top_left); canvas.setTextColor(BLACK);
  canvas.drawString("TODAY", 12, top);

  drawWeatherIcon(60, top + 92, 34, d.cielo);

  int rx = 128;
  // Large max/min temperature display
  canvas.setFont(&fonts::FreeSansBold24pt7b); canvas.setTextDatum(top_left);
  char s[8];
  snprintf(s, sizeof(s), "%d", d.tMax);
  canvas.setTextColor(RED);   canvas.drawString(s, rx, top + 14); int wmax = canvas.textWidth(s);
  canvas.setTextColor(BLACK); canvas.drawString(" / ", rx + wmax, top + 14); int wsl = canvas.textWidth(" / ");
  snprintf(s, sizeof(s), "%d", d.tMin);
  canvas.setTextColor(BLUE);  canvas.drawString(s, rx + wmax + wsl, top + 14); int wmin = canvas.textWidth(s);

  // Label centered under the temperature digits, using the same clean
  // sans-serif font as the rest of the UI (matches the LOCAL screen's "C" style
  // instead of the mismatched Japanese-gothic font previously used here for the degree sign).
  canvas.setFont(&fonts::FreeSans9pt7b);
  canvas.setTextDatum(top_center); canvas.setTextColor(BLACK);
  canvas.drawString("max / min C", rx + (wmax + wsl + wmin) / 2, top + 50);
  canvas.setTextDatum(top_left);
  // UV index (right side, colour by level)
  if (d.uvMax >= 0) {
    char uvs[12]; snprintf(uvs, sizeof(uvs), "UV %d", d.uvMax);
    canvas.setTextDatum(top_right); canvas.setTextColor(uvColor(d.uvMax));
    canvas.drawString(uvs, W - 10, top + 50);
    canvas.setTextDatum(top_left); canvas.setTextColor(BLACK);
  }
  // Sky description on one line (icon already illustrates it)
  { char cl[28]; strlcpy(cl, d.cielo, sizeof(cl)); canvas.drawString(cl, rx, top + 74); }
  // Wind (just below sky description)
  if (d.vientoVel >= 0) {
    char vt[36];
    if (d.racha > 0) snprintf(vt, sizeof(vt), "Wind %s %d (g%d) km/h", d.vientoDir, d.vientoVel, d.racha);
    else             snprintf(vt, sizeof(vt), "Wind %s %d km/h", d.vientoDir, d.vientoVel);
    canvas.drawString(vt, rx, top + 98);
  }
  // Rain probability + humidity
  canvas.setTextColor(BLUE);
  char lab[28]; snprintf(lab, sizeof(lab), "Rain %d%%", d.probLluvia);
  canvas.drawString(lab, rx, top + 122);
  if (d.humMax >= 0) { char hr[28]; snprintf(hr, sizeof(hr), "Humidity %d-%d%%", d.humMin, d.humMax); canvas.drawString(hr, rx, top + 144); }

  // Hourly strip at full width, above the upcoming-day rows
  drawHourly(cc, bottom - 80);
}

// Compact rows for upcoming days (icon + day + sky + max/min + rain).
void drawUpcomingRows(ClimaCache& cc, int startIdx, int count, int top, int bottom) {
  const int W = M5.Display.width();
  int rows = 0;
  for (int k = 0; k < count && (startIdx + k) < cc.nDays; k++) rows++;
  if (rows == 0) return;
  int rowH = (bottom - top) / rows;
  for (int k = 0; k < rows; k++) {
    ClimaDay& d = cc.day[startIdx + k];
    int y0 = top + k * rowH, cy = y0 + rowH / 2;
    if (k > 0) canvas.drawFastHLine(10, y0, W - 20, BLACK);  // row divider

    drawWeatherIcon(32, cy, (int)(rowH * 0.26f), d.cielo);

    canvas.setFont(&fonts::FreeSansBold18pt7b); canvas.setTextColor(BLACK); canvas.setTextDatum(middle_left);
    canvas.drawString(DIAS_C[d.wday], 62, cy - 11);
    canvas.setFont(&fonts::FreeSansBold12pt7b);
    char cielo[20]; strlcpy(cielo, d.cielo, sizeof(cielo));
    canvas.drawString(cielo, 62, cy + 13);

    canvas.setFont(&fonts::FreeSansBold18pt7b); canvas.setTextDatum(middle_right);
    char s[8];
    snprintf(s, sizeof(s), "%d", d.tMin);
    canvas.setTextColor(BLUE);  canvas.drawString(s, W - 12, cy - 11); int wmin = canvas.textWidth(s);
    canvas.setTextColor(BLACK); canvas.drawString("/", W - 12 - wmin, cy - 11); int wsl = canvas.textWidth("/");
    snprintf(s, sizeof(s), "%d", d.tMax);
    canvas.setTextColor(RED);   canvas.drawString(s, W - 12 - wmin - wsl, cy - 11);
    canvas.setFont(&fonts::FreeSansBold12pt7b); canvas.setTextColor(BLUE); canvas.setTextDatum(middle_right);
    char lab[16]; snprintf(lab, sizeof(lab), "rain %d%%", d.probLluvia);
    canvas.drawString(lab, W - 12, cy + 13);
  }
}

// --- Battery icon (green/yellow/red by level). Works for both canvas and M5.Display. ---
template<typename G> void drawBatteryIcon(G& g, int x, int y) {
  int bat = M5.Power.getBatteryLevel(); if (bat < 0) bat = 0;
  uint16_t col = (bat >= 60) ? GREEN : (bat >= 25) ? YELLOW : RED;
  const int w = 42, h = 20;
  g.fillRect(x - 2, y - 2, w + 9, h + 4, WHITE);      // clear the icon area
  g.drawRect(x, y, w, h, BLACK);
  g.fillRect(x + w, y + 6, 3, h - 12, BLACK);          // terminal nub
  g.fillRect(x + 2, y + 2, (w - 4) * bat / 100, h - 4, col);
}

// --- LOCAL view fields (same coords for full draw and partial update) ---
static const int LOC_CLK_Y = 150, LOC_WD_Y = 250, LOC_DT_Y = 300, LOC_TP_Y = 415, LOC_HM_Y = 520;
template<typename G> void locClock(G& g, const m5::rtc_datetime_t& dt) {
  char hm[8]; snprintf(hm, sizeof(hm), "%02d:%02d", dt.time.hours, dt.time.minutes);
  g.setFont(&fonts::Font7); g.setTextSize(2); g.setTextColor(BLACK); g.setTextDatum(middle_center);
  g.drawString(hm, M5.Display.width() / 2, LOC_CLK_Y); g.setTextSize(1);
}
template<typename G> void locDate(G& g, const m5::rtc_datetime_t& dt) {
  const int W = M5.Display.width();
  int wd = (dt.date.weekDay >= 0 && dt.date.weekDay <= 6) ? dt.date.weekDay : 0;
  int mo = (dt.date.month >= 1 && dt.date.month <= 12) ? dt.date.month - 1 : 0;
  g.setTextColor(BLACK); g.setTextDatum(middle_center);
  g.setFont(&fonts::FreeSansBold24pt7b); g.drawString(DIAS[wd], W / 2, LOC_WD_Y);
  char d2[40]; snprintf(d2, sizeof(d2), "%s %d, %d", MESES[mo], dt.date.date, dt.date.year);
  g.setFont(&fonts::FreeSansBold18pt7b); g.drawString(d2, W / 2, LOC_DT_Y);
}
template<typename G> void locTemp(G& g) {
  char b[16]; snprintf(b, sizeof(b), isnan(g_temp) ? "--.- C" : "%.1f C", g_temp);
  g.setFont(&fonts::FreeSansBold24pt7b); g.setTextColor(RED); g.setTextDatum(middle_center);
  g.drawString(b, M5.Display.width() / 2, LOC_TP_Y);
}
template<typename G> void locHum(G& g) {
  char b[16]; snprintf(b, sizeof(b), isnan(g_hum) ? "--.- %%" : "%.0f %%", g_hum);
  g.setFont(&fonts::FreeSansBold24pt7b); g.setTextColor(BLUE); g.setTextDatum(middle_center);
  g.drawString(b, M5.Display.width() / 2, LOC_HM_Y);
}

// Full draw of the LOCAL view (on entry / periodic refresh).
void drawWeatherLocal(const m5::rtc_datetime_t& dt) {
  const int W = M5.Display.width();
  canvas.fillSprite(WHITE);
  canvas.setFont(&fonts::FreeSansBold12pt7b); canvas.setTextDatum(top_left); canvas.setTextColor(BLACK);
  char hdr[16]; snprintf(hdr, sizeof(hdr), "LOCAL  1/%d", numViews());
  canvas.drawString(hdr, 12, 10);
  drawBatteryIcon(canvas, W - 53, 8);
  locClock(canvas, dt);
  locDate(canvas, dt);
  canvas.drawFastHLine(40, 355, W - 80, BLACK);
  locTemp(canvas);
  canvas.setFont(&fonts::FreeSansBold18pt7b); canvas.setTextColor(BLACK); canvas.setTextDatum(middle_center);
  canvas.drawString("Temperature", W / 2, 460);
  locHum(canvas);
  canvas.drawString("Humidity", W / 2, 565);
  esp_task_wdt_reset(); canvas.pushSprite(0, 0);
  g_lastLocalUpdateSec = rtcNow();
}

void drawWeatherCity(const m5::rtc_datetime_t& dt, int loc) {
  const int W = M5.Display.width(), H = M5.Display.height();
  canvas.fillSprite(WHITE);
  // Compact header (small font to avoid overlap): name | time v/N
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextDatum(top_left); canvas.setTextColor(BLACK);
  canvas.drawString(g_locs[loc].name.c_str(), 12, 12);
  canvas.setTextDatum(top_right);
  char hv[16]; snprintf(hv, sizeof(hv), "%02d:%02d  %d/%d", dt.time.hours, dt.time.minutes, loc + 2, numViews());
  canvas.drawString(hv, W - 12, 12);
  canvas.drawFastHLine(12, 40, W - 24, BLACK);

  if (g_locs[loc].lat == 0.0f && g_locs[loc].lon == 0.0f) {
    drawCenteredMsg("No location configured", H / 2 - 20, RED, &fonts::FreeSansBold18pt7b);
    drawCenteredMsg("Check lat/lon in /config.json", H / 2 + 16, RED, &fonts::FreeSansBold18pt7b);
    esp_task_wdt_reset(); canvas.pushSprite(0, 0); return;
  }

  ClimaCache& cc = g_clima[loc];
  int top = 48;
  // Current observation (from Open-Meteo current conditions)
  if (cc.obsValid) {
    canvas.setTextDatum(top_left); canvas.setFont(&fonts::FreeSansBold12pt7b); canvas.setTextColor(BLACK);
    char lbl[24]; snprintf(lbl, sizeof(lbl), "Now %s", cc.obsTime);
    canvas.drawString(lbl, 12, 46);
    canvas.setTextDatum(top_right); char v[24];
    snprintf(v, sizeof(v), "%.1fC  %.0f%%", cc.obsTemp, cc.obsHum);
    canvas.setTextColor(RED); canvas.drawString(v, W - 12, 46);
    canvas.drawFastHLine(12, 74, W - 24, BLACK);
    top = 82;
  }

  if (!cc.valid) {
    // Message in 2 lines with small font (to avoid overflow)
    drawCenteredMsg("Hold G1 button", H / 2 - 16, BLACK, &fonts::FreeSansBold12pt7b);
    drawCenteredMsg("to download forecast", H / 2 + 12, BLACK, &fonts::FreeSansBold12pt7b);
    esp_task_wdt_reset(); canvas.pushSprite(0, 0); return;
  }

  // Today's highlighted block (with hourly strip) + upcoming days
  int todayBottom = top + 250;
  drawToday(cc, top, todayBottom);
  canvas.drawFastHLine(10, todayBottom, W - 20, BLACK);
  drawUpcomingRows(cc, 1, 3, todayBottom + 2, H - 22);

  // Footer: update time (right) + battery icon (left)
  canvas.setFont(&fonts::FreeSansBold12pt7b); canvas.setTextColor(BLACK);
  canvas.setTextDatum(bottom_right);
  char upd[24]; snprintf(upd, sizeof(upd), "Open-Meteo %02d:%02d", cc.updH, cc.updM);
  canvas.drawString(upd, W - 8, H - 4);
  drawBatteryIcon(canvas, 10, H - 24);
  esp_task_wdt_reset(); canvas.pushSprite(0, 0);
}

// Draw an image scaled/centred. Loads the file into a PSRAM buffer and uses
// the in-memory drawJpg/drawPng/drawBmp variants (avoids the M5GFX filesystem
// template, which is abstract in this version).
void drawImageFitted(const String& path, int x, int y, int w, int h) {
  File f = SD.open(path);
  if (!f) return;
  size_t sz = f.size();
  if (sz == 0) { f.close(); return; }
  uint8_t* buf = (uint8_t*)ps_malloc(sz);     // preferimos PSRAM
  if (!buf) buf = (uint8_t*)malloc(sz);
  if (!buf) { f.close(); Serial.println("Sin memoria para la imagen"); return; }
  size_t rd = f.read(buf, sz);
  f.close();
  if (rd == sz) {
    String n = path; n.toLowerCase();
    // (x,y) = box corner, (w,h) = box size, scale 0 = auto-fit,
    // datum middle_center = centred within the box.
    if (n.endsWith(".png"))      canvas.drawPng(buf, sz, x, y, w, h, 0, 0, 0.0f, 0.0f, middle_center);
    else if (n.endsWith(".bmp")) canvas.drawBmp(buf, sz, x, y, w, h, 0, 0, 0.0f, 0.0f, middle_center);
    else                         canvas.drawJpg(buf, sz, x, y, w, h, 0, 0, 0.0f, 0.0f, middle_center);
  }
  free(buf);
}

// Change the panel rotation and recreate the sprite at the new size.
void setPanelRotation(int rot) {
  if (rot == g_rot) return;
  g_rot = rot;
  M5.Display.setRotation(rot);
  canvas.deleteSprite();
  canvas.setPsram(true);
  canvas.createSprite(M5.Display.width(), M5.Display.height());
}

// Read image width/height from its header (JPG/PNG/BMP) without full decoding.
bool jpgSize(File& f, int& w, int& h) {
  if (f.read() != 0xFF || f.read() != 0xD8) return false;          // SOI
  while (f.available()) {
    int b = f.read();
    if (b != 0xFF) continue;
    int marker = f.read();
    while (marker == 0xFF) marker = f.read();
    if (marker >= 0xC0 && marker <= 0xCF && marker != 0xC4 && marker != 0xC8 && marker != 0xCC) {
      f.read(); f.read();            // longitud
      f.read();                      // precision
      h = (f.read() << 8) | f.read();
      w = (f.read() << 8) | f.read();
      return true;
    }
    int len = (f.read() << 8) | f.read();
    if (len < 2) return false;
    f.seek(f.position() + len - 2);
  }
  return false;
}
bool getImageSize(const String& path, int& w, int& h) {
  File f = SD.open(path);
  if (!f) return false;
  String n = path; n.toLowerCase();
  bool ok = false;
  if (n.endsWith(".png")) {
    uint8_t b[24];
    if (f.read(b, 24) == 24) {
      w = (b[16]<<24)|(b[17]<<16)|(b[18]<<8)|b[19];
      h = (b[20]<<24)|(b[21]<<16)|(b[22]<<8)|b[23]; ok = true;
    }
  } else if (n.endsWith(".bmp")) {
    uint8_t b[26];
    if (f.read(b, 26) == 26) {
      w = b[18]|(b[19]<<8)|(b[20]<<16)|(b[21]<<24);
      int hh = b[22]|(b[23]<<8)|(b[24]<<16)|(b[25]<<24);
      h = abs(hh); ok = true;
    }
  } else {
    ok = jpgSize(f, w, h);
  }
  f.close();
  return ok && w > 0 && h > 0;
}

void drawCarrusel() {
  if (!(sd_ready && !g_images.empty())) {
    setPanelRotation(1);
    canvas.fillSprite(WHITE);
    drawCenteredMsg("No images on microSD", M5.Display.height() / 2);
    esp_task_wdt_reset(); canvas.pushSprite(0, 0);
    return;
  }
  const String& path = g_images[g_img_idx % g_images.size()];
  // Panel orientation follows the photo (portrait -> rot 0, landscape -> rot 1)
  int rot = 1, iw = 0, ih = 0;
  if (g_photoAutoRotate && getImageSize(path, iw, ih) && ih > iw) rot = 0;
  setPanelRotation(rot);

  const int W = M5.Display.width(), H = M5.Display.height();
  canvas.fillSprite(WHITE);
  drawImageFitted(path, 0, 0, W, H);
  esp_task_wdt_reset(); canvas.pushSprite(0, 0);
}

// ============================ MODE 3: MUSIC (iPod Shuffle style) ============================
// Controls: UP/DOWN click = next/prev track · double-click = volume +/- · hold = play/pause.

bool isAudioFile(const String& n0) {
  String n = n0; n.toLowerCase();
  return n.endsWith(".mp3") || n.endsWith(".m4a") || n.endsWith(".flac") ||
         n.endsWith(".wav") || n.endsWith(".aac");
}
void scanMusic() {
  g_music.clear();
  if (!sd_ready) return;
  File dir = SD.open(g_musicaDir.c_str());
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return; }
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String nm = f.name();  // filename only, no path
    if (nm.length() && nm[0] == '.') { f.close(); continue; }  // skip hidden files
    if (!f.isDirectory() && isAudioFile(f.path())) g_music.push_back(f.path());
    f.close();
  }
  dir.close();
  Serial.printf("Music in %s: %u\n", g_musicaDir.c_str(), (unsigned)g_music.size());
}
// Readable track name (path and extension stripped).
String trackName(const String& path) {
  int slash = path.lastIndexOf('/');
  String n = (slash >= 0) ? path.substring(slash + 1) : path;
  int dot = n.lastIndexOf('.');
  if (dot > 0) n = n.substring(0, dot);
  return n;
}

// Encode a single byte from Windows-1251 (Cyrillic) to a Unicode codepoint.
static uint32_t cp1251ToUnicode(uint8_t c) {
  // ASCII range: unchanged
  if (c < 0x80) return c;
  // CP1251 special characters 0x80-0xBF
  static const uint16_t cp1251_80_BF[] = {
    0x0402,0x0403,0x201A,0x0453,0x201E,0x2026,0x2020,0x2021,  // 0x80-0x87
    0x20AC,0x2030,0x0409,0x2039,0x040A,0x040C,0x040B,0x040F,  // 0x88-0x8F
    0x0452,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,  // 0x90-0x97
    0x0000,0x2122,0x0459,0x203A,0x045A,0x045C,0x045B,0x045F,  // 0x98-0x9F
    0x00A0,0x040E,0x045E,0x0408,0x00A4,0x0490,0x00A6,0x00A7,  // 0xA0-0xA7
    0x0401,0x00A9,0x0404,0x00AB,0x00AC,0x00AD,0x00AE,0x0407,  // 0xA8-0xAF
    0x00B0,0x00B1,0x0406,0x0456,0x0491,0x00B5,0x00B6,0x00B7,  // 0xB0-0xB7
    0x0451,0x2116,0x0454,0x00BB,0x0458,0x0405,0x0455,0x0457,  // 0xB8-0xBF
  };
  if (c >= 0x80 && c <= 0xBF) return cp1251_80_BF[c - 0x80];
  // 0xC0-0xFF: Cyrillic А(U+0410) through я(U+044F) + Ё/ё
  return 0x0410 + (c - 0xC0);
}

// Append a Unicode codepoint as UTF-8 bytes to a String.
static void appendUtf8(String& out, uint32_t cp) {
  if (cp < 0x80)        { out += (char)cp; }
  else if (cp < 0x800)  { out += (char)(0xC0 | (cp >> 6)); out += (char)(0x80 | (cp & 0x3F)); }
  else if (cp < 0x10000){ out += (char)(0xE0 | (cp >> 12)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
  else                  { out += (char)(0xF0 | (cp >> 18)); out += (char)(0x80 | ((cp >> 12) & 0x3F)); out += (char)(0x80 | ((cp >> 6) & 0x3F)); out += (char)(0x80 | (cp & 0x3F)); }
}

// Check if a byte sequence is valid UTF-8.
static bool isValidUtf8(const String& in) {
  int n = in.length();
  for (int i = 0; i < n;) {
    uint8_t c = in[i];
    int extra = 0;
    if      (c < 0x80)            extra = 0;
    else if ((c & 0xE0) == 0xC0)  extra = 1;
    else if ((c & 0xF0) == 0xE0)  extra = 2;
    else if ((c & 0xF8) == 0xF0)  extra = 3;
    else return false;
    for (int j = 1; j <= extra; j++) {
      if (i + j >= n || (in[i + j] & 0xC0) != 0x80) return false;
    }
    i += 1 + extra;
  }
  return true;
}

// Convert filename/text string to clean UTF-8 for display.
// Handles: already-valid UTF-8, Windows-1251 (Cyrillic), Latin-1.
// Heuristic: if not valid UTF-8, check for Cyrillic range bytes (0xC0-0xFF is common in CP1251).
String utf8Fix(const String& in) {
  if (isValidUtf8(in)) return in;  // already valid UTF-8 (includes pure ASCII)

  // Detect encoding: if any byte is in 0x80-0xFF range that is not valid UTF-8,
  // we are already here (isValidUtf8 returned false). Check if bytes suggest
  // CP1251 (Cyrillic, 0xC0-0xFF common) vs Latin-1 (0x80-0xBF only).
  // If any byte is in 0xC0-0xFF, treat entire string as CP1251.
  // Otherwise (0x80-0xBF only) treat as Latin-1 (codepoint == byte value).
  bool hasCyrillic = false;
  for (int i = 0; i < (int)in.length(); i++) {
    uint8_t c = in[i];
    if (c >= 0xC0) { hasCyrillic = true; break; }
  }
  // If no 0xC0+ bytes but we have 0x80-0xBF: could be CP1251 special chars.
  // Conservative: if the majority of high bytes are in 0xC0-0xFF range of
  // the CP1251 Cyrillic block, hasCyrillic will be set. Otherwise Latin-1 is fine.

  String out;
  out.reserve(in.length() * 2);
  for (int i = 0; i < (int)in.length(); i++) {
    uint8_t c = in[i];
    if (c < 0x80) { out += (char)c; continue; }
    if (hasCyrillic) appendUtf8(out, cp1251ToUnicode(c));
    else             appendUtf8(out, c);  // Latin-1: codepoint == byte value
  }
  return out;
}

// Initialise the ES8311 codec (M5Unified PaperColor sequence, MCLK=BCLK) and I2S.
// Codec and amp start POWERED OFF (battery saving); they are only turned on in music mode.
void audioInit() {
  pinMode(PIN_CODEC_EN, OUTPUT); digitalWrite(PIN_CODEC_EN, LOW);
  pinMode(PIN_SPK_EN, OUTPUT);   digitalWrite(PIN_SPK_EN, LOW);
  audio.setPinout(I2S_BCLK_PIN, I2S_LRCK_PIN, I2S_DOUT_PIN);   // MCLK not needed (=BCLK)
  // Track-end detection: v3.0.12 uses global free-function callbacks.
  // audio_eof_mp3/aac/flac/wav are called by the library when a file ends.
  // We set g_trackEnded=true in each; the loop() picks it up.
  g_audioReady = true;
  Serial.println("Audio (I2S) ready; codec off until music mode.");
}
// ESP32-audioI2S v3.0.12 global end-of-track callbacks (free functions, called by library)
void audio_eof_mp3(const char*)  { g_trackEnded = true; }
void audio_eof_aac(const char*)  { g_trackEnded = true; }
void audio_eof_flac(const char*) { g_trackEnded = true; }
void audio_eof_wav(const char*)  { g_trackEnded = true; }
// Power on the ES8311 codec + amp and initialise registers (called when entering music mode).
void audioPowerOn() {
  if (g_audioPowered) return;
  digitalWrite(PIN_CODEC_EN, HIGH); digitalWrite(PIN_SPK_EN, HIGH);
  delay(10);
  static const uint8_t seq[][2] = {
    {0x00, 0x80}, {0x01, 0xB5}, {0x02, 0x18}, {0x0D, 0x01},
    {0x12, 0x00}, {0x13, 0x10}, {0x32, 0xCF}, {0x37, 0x08}
  };
  for (auto& r : seq) M5.In_I2C.writeRegister8(ES8311_ADDR, r[0], r[1], 100000);
  audio.setVolume(g_volume);
  g_audioPowered = true;
  Serial.println("Codec ES8311 powered on.");
}
// Stop playback and power off codec + amp (on leaving music mode / battery saving).
void audioPowerOff() {
  if (g_audioReady && audio.isRunning()) audio.stopSong();
  g_playing = false; g_loaded = false;
  digitalWrite(PIN_CODEC_EN, LOW); digitalWrite(PIN_SPK_EN, LOW);
  g_audioPowered = false;
}

// --- Audio actions (iPod Shuffle style) ---
void musicPlayCurrent() {
  if (g_music.empty() || !g_audioReady) return;
  audioPowerOn();   // ensure codec/amp are on
  Serial.printf("[MUSIC] play %s (vol %d)\n", trackName(g_music[g_track]).c_str(), g_volume);
  audio.connecttoFS(SD, g_music[g_track].c_str());
  audio.setVolume(g_volume);
  g_loaded = true; g_playing = true; g_trackEnded = false;
}
void musicNext() {
  if (g_music.empty()) return;
  g_track = (g_track + 1) % g_music.size();
  musicPlayCurrent(); g_needRedraw = true;
}
void musicPrev() {
  if (g_music.empty()) return;
  g_track = (g_track + g_music.size() - 1) % g_music.size();
  musicPlayCurrent(); g_needRedraw = true;
}
// Volume and play/pause do NOT trigger a screen redraw (avoids audio glitches from e-paper refresh).
void musicVolUp()   { if (g_volume < 21) g_volume++; if (g_audioReady) audio.setVolume(g_volume); Serial.printf("[MUSIC] vol %d\n", g_volume); }
void musicVolDown() { if (g_volume > 0)  g_volume--; if (g_audioReady) audio.setVolume(g_volume); Serial.printf("[MUSIC] vol %d\n", g_volume); }
void musicTogglePlay() {
  if (g_music.empty()) return;
  // first play: track already shown on screen (drawn on mode entry) -> no redraw needed
  if (!g_loaded) { musicPlayCurrent(); }
  else { audio.pauseResume(); g_playing = !g_playing; }
  Serial.printf("[MUSIC] %s\n", g_playing ? "play" : "paused");
}


void drawMusic() {
  const int W = M5.Display.width(), H = M5.Display.height();
  canvas.fillSprite(WHITE);

  // Header
  canvas.setFont(&fonts::FreeSansBold18pt7b); canvas.setTextDatum(top_left); canvas.setTextColor(BLACK);
  canvas.drawString("MUSIC", 12, 8);
  drawBatteryIcon(canvas, W - 53, 8);
  canvas.drawFastHLine(12, 42, W - 24, BLACK);

  if (g_music.empty()) {
    drawCenteredMsg("No music in", H / 2 - 16, BLACK, &fonts::FreeSansBold18pt7b);
    drawCenteredMsg(g_musicaDir.c_str(), H / 2 + 16, BLACK, &fonts::FreeSansBold18pt7b);
    esp_task_wdt_reset(); canvas.pushSprite(0, 0); return;
  }

  // Current track (n / total)
  canvas.setFont(&fonts::FreeSansBold12pt7b); canvas.setTextDatum(top_right); canvas.setTextColor(BLACK);
  char idx[16]; snprintf(idx, sizeof(idx), "%u / %u", (unsigned)(g_track + 1), (unsigned)g_music.size());
  canvas.drawString(idx, W - 12, 50);

  // Track name
  canvas.setFont(&fonts::FreeSansBold12pt7b);
  canvas.setTextColor(BLACK); canvas.setTextDatum(middle_center);
  {
    String name = utf8Fix(trackName(g_music[g_track]));
    if (canvas.textWidth(name) <= W - 24) {
      canvas.drawString(name, W / 2, H / 2);
    } else {
      int half = name.length() / 2;
      int sp = name.indexOf(' ', half); if (sp < 0) sp = half;
      canvas.drawString(name.substring(0, sp), W / 2, H / 2 - 28);
      canvas.drawString(name.substring(sp + 1), W / 2, H / 2 + 28);
    }
  }

  // Control hints (2 lines to avoid clipping)
  canvas.setFont(&fonts::FreeSansBold12pt7b); canvas.setTextColor(BLACK); canvas.setTextDatum(bottom_center);
  canvas.drawString("Click: volume    x2: track", W / 2, H - 34);
  canvas.drawString("Hold: play / pause", W / 2, H - 10);

  esp_task_wdt_reset(); canvas.pushSprite(0, 0);
}

// ============================ MODE 4: BOOKS (TXT reader) ============================
// Streams and paginates the text file without loading it entirely.
// Renders with the body font (Cyrillic/accents). Saves book+page in NVS. TXT in UTF-8.
static constexpr int LIB_MARGIN = 14;   // side margin (pixels)
static constexpr int LIB_TOP    = 56;   // text start Y (below header)
static constexpr int LIB_LINE_H = 28;   // fallback line height (built-in font)

// Unique NVS key per book (FNV-1a hash of path) to remember the last read page.
String bookKey() {
  uint32_t h = 2166136261u;
  const String& p = g_books[g_bookIdx];
  for (size_t i = 0; i < p.length(); i++) { h ^= (uint8_t)p[i]; h *= 16777619u; }
  char k[12]; snprintf(k, sizeof(k), "b%08lX", (unsigned long)h);
  return String(k);
}
void bookSavePos() {
  if (g_books.empty()) return;
  prefs.putUInt(bookKey().c_str(), g_pageStart);
  prefs.putInt("lastbook", g_bookIdx);
}

// Draw the page starting at g_pageStart and compute g_nextPageStart.
// ───────────────────────────────────────────────────────────────────────────
// Manual VLW glyph rendering: M5GFX's built-in VLWfont/loadFont() path has proven
// unreliable on this hardware (produces corrupted "dots" or fully invisible text
// for antialiased Cyrillic glyphs — likely an EPD-specific quirk in M5GFX's
// alpha-blend/readback code). The VLW file format itself is simple and fully
// documented; we parse the glyph table and blit each glyph's bitmap directly
// with drawPixel() calls, bypassing M5GFX's font/antialiasing pipeline entirely.
// drawPixel() is the single most basic, universally-reliable primitive — no
// blending, no readback, no antialiasing math that could go wrong.
// (VlwGlyph / VlwFontInfo structs are defined near the top of the file, alongside
// the other sketch-wide structs, so Arduino's auto-generated function prototypes
// — which it inserts before any function bodies — can see the types they return.)
// ───────────────────────────────────────────────────────────────────────────

VlwFontInfo vlwParseHeader(const uint8_t* buf) {
  VlwFontInfo info;
  if (!buf) return info;
  info.buf = buf;
  info.gCount   = ((int)buf[0]<<24)|((int)buf[1]<<16)|((int)buf[2]<<8)|buf[3];
  info.fontSize = ((int)buf[4]<<24)|((int)buf[5]<<16)|((int)buf[6]<<8)|buf[7];
  info.tableEnd = 20 + info.gCount * 24;
  if (info.gCount <= 0 || info.gCount >= 5000) return info;

  // Bitmaps are stored in FORWARD glyph-table order (glyph[0] first).
  // offset[i] = tableEnd + sum(h*w for j in 0..i-1).  Simple forward pass.
  info.bitmapOffsets.resize(info.gCount);
  uint32_t cumulative = (uint32_t)info.tableEnd;
  for (int i = 0; i < info.gCount; i++) {
    info.bitmapOffsets[i] = cumulative;
    int off = 20 + i * 24;
    int32_t gh = ((int32_t)buf[off+4]<<24)|((int32_t)buf[off+5]<<16)|((int32_t)buf[off+6]<<8)|buf[off+7];
    int32_t gw = ((int32_t)buf[off+8]<<24)|((int32_t)buf[off+9]<<16)|((int32_t)buf[off+10]<<8)|buf[off+11];
    if (gh > 0 && gw > 0) cumulative += (uint32_t)(gh * gw);
  }
  info.ready = true;
  return info;
}

// Binary search the glyph table for a codepoint, filling in metrics + bitmap offset.
bool vlwFindGlyph(const VlwFontInfo& f, uint32_t cp, VlwGlyph& out) {
  if (!f.ready) return false;
  const uint8_t* b = f.buf;
  int lo = 0, hi = f.gCount - 1, foundIdx = -1;
  while (lo <= hi) {
    int mid = (lo + hi) / 2;
    int off = 20 + mid * 24;
    uint32_t c = ((uint32_t)b[off]<<24)|((uint32_t)b[off+1]<<16)|((uint32_t)b[off+2]<<8)|b[off+3];
    if (c == cp) { foundIdx = mid; break; }
    if (cp < c) hi = mid - 1; else lo = mid + 1;
  }
  if (foundIdx < 0) return false;

  int off = 20 + foundIdx * 24;
  out.cp         = cp;
  out.h          = (int16_t)(((int32_t)b[off+4]<<24)|((int32_t)b[off+5]<<16)|((int32_t)b[off+6]<<8)|b[off+7]);
  out.w          = (int16_t)(((int32_t)b[off+8]<<24)|((int32_t)b[off+9]<<16)|((int32_t)b[off+10]<<8)|b[off+11]);
  out.advance    = (int16_t)(((int32_t)b[off+12]<<24)|((int32_t)b[off+13]<<16)|((int32_t)b[off+14]<<8)|b[off+15]);
  out.topExtent  = (int16_t)(((int32_t)b[off+16]<<24)|((int32_t)b[off+17]<<16)|((int32_t)b[off+18]<<8)|b[off+19]);
  out.leftExtent = (int16_t)(((int32_t)b[off+20]<<24)|((int32_t)b[off+21]<<16)|((int32_t)b[off+22]<<8)|b[off+23]);
  // O(1) lookup — precomputed in vlwParseHeader()
  out.bitmapOffset = f.bitmapOffsets[foundIdx];
  return true;
}

// Decode one UTF-8 codepoint starting at index i in s. Advances i past the sequence.
// Decode one UTF-8 codepoint from a raw byte pointer. Advances ptr past the sequence.
uint32_t vlwDecodeUtf8Raw(const uint8_t* data, int& i, int n) {
  uint8_t b = data[i];
  uint32_t cp; int bytes;
  if      (b < 0x80)            { cp = b;         bytes = 1; }
  else if ((b & 0xE0) == 0xC0)  { cp = b & 0x1F;   bytes = 2; }
  else if ((b & 0xF0) == 0xE0)  { cp = b & 0x0F;   bytes = 3; }
  else                          { cp = b & 0x07;   bytes = 4; }
  for (int j = 1; j < bytes && i + j < n; j++) cp = (cp << 6) | (data[i + j] & 0x3F);
  i += bytes;
  return cp;
}

// (Word-wrap measurement and glyph rendering are inlined directly in drawLibro()
// using vlwDecodeUtf8Raw + vlwFindGlyph on the raw chunk bytes — see below. This
// avoids building intermediate Strings, which is both faster and was essential to
// fixing a UTF-8 chunk-boundary truncation bug that corrupted Cyrillic rendering.)

void drawLibro() {
  const int W = M5.Display.width(), H = M5.Display.height();

  // Book reader draws DIRECTLY to M5.Display (not through the canvas sprite).
  // M5GFX's built-in VLWfont/loadFont() renderer has proven unreliable on this
  // hardware (corrupted "dots" or fully invisible antialiased text). We parse the
  // VLW header/glyph table ourselves and blit glyph bitmaps with drawPixel(),
  // bypassing M5GFX's font system entirely for the body text.
  VlwFontInfo vlwFont = vlwParseHeader(g_bodyFontBuf);
  bool vlwOk = g_bodyFontReady && vlwFont.ready;
  if (!vlwOk) Serial.println("[BOOK] VLW header parse FAILED, using fallback font");

  M5.Display.startWrite();
  M5.Display.fillScreen(WHITE);

  M5.Display.setTextDatum(top_left);
  M5.Display.setTextColor(BLACK);

  if (g_books.empty()) {
    M5.Display.setFont(&fonts::FreeSansBold18pt7b);
    M5.Display.setTextDatum(middle_center);
    M5.Display.drawString("No books (.txt) in", W / 2, H / 2 - 16);
    M5.Display.drawString(g_librosDir.c_str(),   W / 2, H / 2 + 16);
    M5.Display.endWrite();
    return;
  }

  // Header: book title + battery.
  M5.Display.setFont(&fonts::FreeSansBold12pt7b);
  M5.Display.setTextDatum(top_left);
  String name = utf8Fix(trackName(g_books[g_bookIdx]));
  M5.Display.drawString(name, LIB_MARGIN, 10);
  drawBatteryIcon(M5.Display, W - 53, 8);
  M5.Display.drawFastHLine(LIB_MARGIN, 44, W - 2 * LIB_MARGIN, BLACK);

  // Read a chunk from the current position and word-wrap it.
  const int maxW = W - 2 * LIB_MARGIN;
  if (!vlwOk) M5.Display.setFont(&fonts::FreeSans9pt7b);
  int fh = vlwOk ? vlwFont.fontSize : M5.Display.fontHeight();
  if (fh < 10 || fh > 80) fh = 22;
  const int lineH = fh + 6;
  const int linesPerPage = (H - LIB_TOP - 34) / lineH;

  File f = SD.open(g_books[g_bookIdx].c_str());
  bool opened = (bool)f;
  size_t fsz = 0;
  String chunk;
  static const size_t CHUNK = 7000;
  static uint8_t rawBuf[7004];   // function-scope so g_nextPageStart calc can access it
  size_t rawLen = 0;
  if (opened) {
    fsz = f.size();
    if (g_pageStart > fsz) g_pageStart = 0;       // invalid offset -> reset to start
    f.seek(g_pageStart);
    // Read into a raw byte buffer (avoids Arduino String truncating at null bytes,
    // which breaks UTF-16 files where every ASCII char is followed by 0x00).
    rawLen = 0;
    while (rawLen < CHUNK && f.available()) rawBuf[rawLen++] = (uint8_t)f.read();
    f.close();

    // CRITICAL: trim rawBuf back to the last complete UTF-8 sequence boundary.
    // Reading a fixed 7000-byte chunk can cut a multi-byte UTF-8 character in half
    // at the boundary (Cyrillic is 2 bytes/char). A truncated sequence makes
    // isValidUtf8() return false for the WHOLE chunk, causing utf8Fix() to wrongly
    // run CP1251 conversion on already-valid UTF-8 text — corrupting every Cyrillic
    // character on the page (the root cause of the encoding garbling bug).
    // Walk back from the end: if the last byte is a UTF-8 lead byte expecting more
    // continuation bytes than are present, or a continuation byte whose lead byte
    // got cut off, trim those trailing partial bytes (they'll be re-read at the
    // start of the next page since we don't advance g_pageStart for them).
    if (rawLen > 0) {
      // Don't touch UTF-16 files — boundary trimming only applies to UTF-8/CP1251/ASCII.
      // (UTF-16 detection happens below; for now just check trailing bytes look like
      // a possible UTF-8 lead byte with insufficient continuation bytes following.)
      int trim = 0;
      // Look at up to the last 3 bytes for an incomplete multi-byte sequence.
      for (int back = 1; back <= 3 && (int)rawLen - back >= 0; back++) {
        uint8_t b = rawBuf[rawLen - back];
        int need = 0;
        if      ((b & 0xE0) == 0xC0) need = 1;  // 2-byte sequence, need 1 more
        else if ((b & 0xF0) == 0xE0) need = 2;  // 3-byte sequence, need 2 more
        else if ((b & 0xF8) == 0xF0) need = 3;  // 4-byte sequence, need 3 more
        else continue;  // not a lead byte at this position, keep looking further back
        // 'back-1' continuation bytes are present after this lead byte (within rawBuf).
        if (need > back - 1) trim = back;  // incomplete — trim from here
        break;  // found the nearest lead byte; decision made
      }
      if (trim > 0) rawLen -= trim;
    }
    rawBuf[rawLen] = 0;

    // Detect and convert UTF-16 LE (FF FE) or UTF-16 BE (FE FF) at page start.
    // Windows Notepad saves as UTF-16 LE by default on many locales.
    bool isUtf16LE = false, isUtf16BE = false;
    size_t rawStart = 0;
    if (g_pageStart == 0 && rawLen >= 2) {
      if (rawBuf[0] == 0xFF && rawBuf[1] == 0xFE) { isUtf16LE = true; rawStart = 2; }
      else if (rawBuf[0] == 0xFE && rawBuf[1] == 0xFF) { isUtf16BE = true; rawStart = 2; }
    }
    // For pages after the first, inherit encoding from the BOM-less continuation.
    // We detect UTF-16 by the characteristic null-byte pattern (every other byte = 0x00
    // for ASCII-range text). This heuristic is reliable for English/Cyrillic content.
    if (!isUtf16LE && !isUtf16BE && g_pageStart > 0 && rawLen >= 4) {
      int nullOdd = 0, nullEven = 0;
      for (size_t i = rawStart; i + 1 < rawLen && i < rawStart + 40; i += 2) {
        if (rawBuf[i + 1] == 0) nullOdd++;    // LE: char at i, null at i+1
        if (rawBuf[i]     == 0) nullEven++;   // BE: null at i, char at i+1
      }
      if (nullOdd  >= 8) isUtf16LE = true;
      if (nullEven >= 8) isUtf16BE = true;
    }
    // Persist encoding detection for g_nextPageStart calculation.
    if (g_pageStart == 0) {
      g_fileIsUtf16  = isUtf16LE || isUtf16BE;
      g_utf16RawStart = rawStart;
    }

    chunk.reserve(rawLen);
    if (isUtf16LE || isUtf16BE) {
      // Convert UTF-16 -> UTF-8 codepoint by codepoint.
      for (size_t i = rawStart; i + 1 < rawLen; i += 2) {
        uint16_t lo = isUtf16LE ? rawBuf[i]     : rawBuf[i + 1];
        uint16_t hi = isUtf16LE ? rawBuf[i + 1] : rawBuf[i];
        uint32_t cp = (hi << 8) | lo;
        // Handle surrogate pairs (U+D800-U+DFFF) for characters above U+FFFF.
        if (cp >= 0xD800 && cp <= 0xDBFF && i + 3 < rawLen) {
          uint16_t lo2 = isUtf16LE ? rawBuf[i + 2] : rawBuf[i + 3];
          uint16_t hi2 = isUtf16LE ? rawBuf[i + 3] : rawBuf[i + 2];
          uint32_t low = (hi2 << 8) | lo2;
          if (low >= 0xDC00 && low <= 0xDFFF) {
            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
            i += 2; // consumed extra surrogate unit
          }
        }
        // Encode codepoint as UTF-8.
        if (cp < 0x80) {
          if (cp == 0) continue; // skip embedded nulls
          chunk += (char)cp;
        } else if (cp < 0x800) {
          chunk += (char)(0xC0 | (cp >> 6));
          chunk += (char)(0x80 | (cp & 0x3F));
        } else if (cp < 0x10000) {
          chunk += (char)(0xE0 | (cp >> 12));
          chunk += (char)(0x80 | ((cp >> 6) & 0x3F));
          chunk += (char)(0x80 | (cp & 0x3F));
        } else {
          chunk += (char)(0xF0 | (cp >> 18));
          chunk += (char)(0x80 | ((cp >> 12) & 0x3F));
          chunk += (char)(0x80 | ((cp >> 6) & 0x3F));
          chunk += (char)(0x80 | (cp & 0x3F));
        }
      }
    } else {
      // UTF-8 / ASCII / CP1251: copy raw bytes, skip nulls to avoid String truncation.
      for (size_t i = rawStart; i < rawLen; i++) {
        if (rawBuf[i] != 0) chunk += (char)rawBuf[i];
      }
      // Skip UTF-8 BOM (EF BB BF) if present at page start.
      if (g_pageStart == 0 && chunk.length() >= 3 &&
          (uint8_t)chunk[0] == 0xEF && (uint8_t)chunk[1] == 0xBB && (uint8_t)chunk[2] == 0xBF) {
        chunk = chunk.substring(3);
      }
      // Convert to clean UTF-8 (handles CP1251 Cyrillic and Latin-1).
      chunk = utf8Fix(chunk);
    }
    // NOTE: vlwSanitize() is intentionally NOT applied to `chunk` here, because doing so
    // can change its byte length (e.g. « 2 bytes -> " 1 byte), which would desync the
    // pos/ri byte-offset math used below for g_nextPageStart. The renderer's own
    // glyph-not-found fallback (advance by half the font size) handles missing glyphs.
  }
  Serial.printf("[BOOK] %s open=%d size=%u start=%u read=%u vlwOk=%d\n",
                g_books[g_bookIdx].c_str(), opened ? 1 : 0,
                (unsigned)fsz, (unsigned)g_pageStart, (unsigned)chunk.length(), (int)vlwOk);

  if (!opened || chunk.length() == 0) {
    M5.Display.setFont(&fonts::FreeSansBold18pt7b);
    M5.Display.setTextDatum(middle_center);
    M5.Display.setTextColor(RED);
    M5.Display.drawString(opened ? "Empty file or end" : "Could not open file", W / 2, H / 2);
    M5.Display.endWrite();
    return;
  }

  Serial.printf("[BOOK] fh=%d lineH=%d linesPerPage=%d maxW=%d font=%s\n",
                fh, lineH, linesPerPage, maxW, vlwOk ? "VLW-manual-blit" : "FreeSans9pt7b-fallback");

  // Render directly from raw chunk bytes — no String building at all.
  // This eliminates any possibility of Arduino String high-byte corruption.
  const uint8_t* chunkRaw = (const uint8_t*)chunk.c_str();

  int pos = 0, len = chunk.length(), y = LIB_TOP, linesDrawn = 0;
  while (linesDrawn < linesPerPage && pos < len) {
    if (chunkRaw[pos] == '\r') { pos++; continue; }
    if (chunkRaw[pos] == '\n') { pos++; y += lineH; linesDrawn++; continue; }

    // Word-wrap: find the range [lineStart, lineEnd) that fits within maxW
    int lineStart = pos, lineEnd = pos, linePos = pos;
    while (linePos < len && chunkRaw[linePos] != '\n') {
      int ws = linePos;
      while (ws < len && chunkRaw[ws] != ' ' && chunkRaw[ws] != '\n') ws++;
      if (ws == linePos) { linePos++; continue; }

      // Measure candidate width (existing line + space + new word)
      int tw = 0;
      if (lineEnd > lineStart) {
        int ti = lineStart;
        while (ti < lineEnd) {
          VlwGlyph g;
          if (vlwFindGlyph(vlwFont, vlwDecodeUtf8Raw(chunkRaw, ti, len), g)) tw += g.advance;
          else tw += fh / 2;
        }
        tw += vlwFont.fontSize / 4;  // approx space width
      }
      int wi = linePos;
      while (wi < ws) {
        VlwGlyph g;
        if (vlwFindGlyph(vlwFont, vlwDecodeUtf8Raw(chunkRaw, wi, len), g)) tw += g.advance;
        else tw += fh / 2;
      }

      if (tw <= maxW || lineEnd == lineStart) {
        lineEnd = ws; linePos = ws;
        if (linePos < len && chunkRaw[linePos] == ' ') {
          linePos++;
          while (linePos < len && (chunkRaw[linePos] & 0xC0) == 0x80) linePos++;
        }
      } else break;
    }

    // Draw directly from raw bytes — no String, no encoding issues
    if (vlwOk && lineEnd > lineStart) {
      esp_task_wdt_reset();
      int cx = LIB_MARGIN, ri = lineStart;
      while (ri < lineEnd) {
        uint32_t cp = vlwDecodeUtf8Raw(chunkRaw, ri, lineEnd);
        VlwGlyph g;
        if (!vlwFindGlyph(vlwFont, cp, g)) { cx += fh / 2; continue; }
        if (g.h > 0 && g.w > 0) {
          int gx = cx + g.leftExtent;
          int gy = y + fh - g.topExtent;
          const uint8_t* bm = vlwFont.buf + g.bitmapOffset;
          for (int p = 0, px = g.h * g.w; p < px; p++)
            if (bm[p] > 80) M5.Display.drawPixel(gx + (p % g.w), gy + (p / g.w), BLACK);
        }
        cx += g.advance;
      }
    } else if (!vlwOk) {
      M5.Display.drawString(chunk.substring(lineStart, lineEnd), LIB_MARGIN, y);
    }

    pos = linePos;
    if (pos < len && chunkRaw[pos] == '\n') pos++;
    y += lineH; linesDrawn++;
  }
  // Compute raw file offset for start of next page.
  if (g_fileIsUtf16) {
    size_t ci = 0;   // byte index into chunk (UTF-8)
    size_t ri = g_utf16RawStart;  // byte index into rawBuf
    while (ci < (size_t)pos && ri + 1 < rawLen) {
      uint8_t b = (uint8_t)chunk[ci];
      int utf8bytes = (b < 0x80) ? 1 : (b < 0xE0) ? 2 : (b < 0xF0) ? 3 : 4;
      int rawBytes = (utf8bytes == 4) ? 4 : 2;
      ci += utf8bytes;
      ri += rawBytes;
    }
    g_nextPageStart = g_pageStart + ri;
  } else {
    g_nextPageStart = g_pageStart + pos;
  }

  // Hint bar at the bottom.
  M5.Display.setFont(&fonts::FreeSans9pt7b);
  M5.Display.setTextColor(BLACK);
  M5.Display.setTextDatum(bottom_center);
  M5.Display.drawString("UP/DN page   hold G1: list   G1: exit", W / 2, H - 4);

  esp_task_wdt_reset();
  M5.Display.endWrite();
  M5.Display.display();   // force EPD refresh of the drawn frame
}

void pageNext() {
  if (g_books.empty()) return;
  File f = SD.open(g_books[g_bookIdx].c_str()); size_t fsz = f ? f.size() : 0; if (f) f.close();
  if (g_nextPageStart <= g_pageStart || g_nextPageStart >= fsz) return;   // end of book
  g_pageStack.push_back(g_pageStart);
  g_pageStart = g_nextPageStart;
  bookSavePos(); g_needRedraw = true;
}
void pagePrev() {
  if (g_books.empty() || g_pageStack.empty()) return;
  g_pageStart = g_pageStack.back(); g_pageStack.pop_back();
  bookSavePos(); g_needRedraw = true;
}
// Move selection in the book list.
void listMove(int d) {
  if (g_books.empty()) return;
  int n = g_books.size();
  g_sel = (g_sel + d % n + n) % n;
  g_needRedraw = true;
}
// Open the selected book at its last saved page.
void bookOpenSelected() {
  if (g_books.empty()) return;
  g_bookIdx = g_sel;
  g_pageStart = prefs.getUInt(bookKey().c_str(), 0);   // last saved position for THIS book
  g_pageStack.clear();
  g_fileIsUtf16 = false;   // encoding re-detected on first page draw
  g_libState = LIB_READING;
  bookSavePos(); g_needRedraw = true;
}

// File selector (list from /Library).
void drawLibroList() {
  const int W = M5.Display.width(), H = M5.Display.height();
  canvas.fillSprite(WHITE);

  Serial.printf("[LIST] W=%d H=%d canvasW=%d canvasH=%d books=%u\n",
                W, H, (int)canvas.width(), (int)canvas.height(),
                (unsigned)g_books.size());

  // Header (matches MUSIC: bold title + battery icon + divider)
  canvas.setFont(&fonts::FreeSansBold18pt7b); canvas.setTextDatum(top_left); canvas.setTextColor(BLACK);
  canvas.drawString("BOOKS", 12, 8);
  drawBatteryIcon(canvas, W - 53, 8);
  canvas.drawFastHLine(12, 42, W - 24, BLACK);
  canvas.setFont(&fonts::FreeSans9pt7b);

  if (g_books.empty()) {
    drawCenteredMsg("No books (.txt) in", H / 2 - 16, BLACK, &fonts::FreeSansBold18pt7b);
    drawCenteredMsg(g_librosDir.c_str(), H / 2 + 16, BLACK, &fonts::FreeSansBold18pt7b);
    esp_task_wdt_reset(); canvas.pushSprite(0, 0); return;
  }

  const int rowH = 34, top = 56;
  int visible = (H - top - 30) / rowH;
  int n = g_books.size();
  int first = 0;                                   // scroll window centred on selection
  if (n > visible) { first = g_sel - visible / 2; if (first < 0) first = 0; if (first > n - visible) first = n - visible; }
  for (int i = 0; i < visible && (first + i) < n; i++) {
    int idx = first + i, y = top + i * rowH;
    if (idx == g_sel) canvas.fillRect(LIB_MARGIN - 4, y - 2, W - 2 * LIB_MARGIN + 8, rowH - 2, BLUE);
    canvas.setTextColor(idx == g_sel ? WHITE : BLACK);
    canvas.setTextDatum(top_left);
    canvas.drawString(utf8Fix(trackName(g_books[idx])), LIB_MARGIN, y);
  }
  canvas.setFont(&fonts::FreeSansBold12pt7b); canvas.setTextColor(BLACK); canvas.setTextDatum(bottom_center);
  canvas.drawString("UP/DN select   x2: open   G1: exit", W / 2, H - 10);
  Serial.printf("[LIST] pushing sprite W=%d H=%d\n", W, H);
  esp_task_wdt_reset(); canvas.pushSprite(0, 0);
  Serial.println("[LIST] pushSprite done");
}

// ============================ MODE 5: WI-FI (web-based SD manager) ============================
// Starts an own AP ("nomad" style) or joins a saved network, plus a web server with login
// to browse, download, upload, edit, and delete files on the microSD.
// Only active in this mode; Wi-Fi is off in all other modes.
// The web page (WIFI_HTML) lives in wifi_page.h to avoid .ino preprocessor issues.

bool webAuth() {
  // Basic auth disabled — device is on local AP with no internet exposure.
  // Re-enable by uncommenting the lines below if needed.
  // if (g_server.authenticate(g_webUser.c_str(), g_webPass.c_str())) return true;
  // g_server.requestAuthentication(); return false;
  return true;
}
static String jsonEsc(const String& s) {
  String o; for (size_t i = 0; i < s.length(); i++) { char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; } else if (c == '\n') o += "\\n"; else o += c; }
  return o;
}
static String webContentType(const String& p) {
  String n = p; n.toLowerCase();
  if (n.endsWith(".jpg") || n.endsWith(".jpeg")) return "image/jpeg";
  if (n.endsWith(".png")) return "image/png";
  if (n.endsWith(".gif")) return "image/gif";
  if (n.endsWith(".bmp")) return "image/bmp";
  if (n.endsWith(".webp")) return "image/webp";
  if (n.endsWith(".mp4") || n.endsWith(".m4v")) return "video/mp4";
  if (n.endsWith(".mov")) return "video/quicktime";
  if (n.endsWith(".webm")) return "video/webm";
  if (n.endsWith(".mkv")) return "video/x-matroska";
  if (n.endsWith(".mp3")) return "audio/mpeg";
  if (n.endsWith(".m4a")) return "audio/mp4";        // AAC in MP4 container
  if (n.endsWith(".aac")) return "audio/aac";
  if (n.endsWith(".wav")) return "audio/wav";
  if (n.endsWith(".flac")) return "audio/flac";
  if (n.endsWith(".ogg") || n.endsWith(".oga")) return "audio/ogg";
  if (n.endsWith(".pdf"))  return "application/pdf";          // native browser PDF viewer
  if (n.endsWith(".epub")) return "application/epub+zip";     // read by the epub.js reader
  if (n.endsWith(".svg"))  return "image/svg+xml";            // rendered natively by browser
  if (n.endsWith(".html") || n.endsWith(".htm")) return "text/html; charset=utf-8";  // rendered natively by browser
  if (n.endsWith(".js"))   return "application/javascript; charset=utf-8";  // for <script> tags (EPUB reader in /lib)
  if (n.endsWith(".txt")||n.endsWith(".json")||n.endsWith(".csv")||n.endsWith(".md")||n.endsWith(".log")||
      n.endsWith(".cfg")||n.endsWith(".ino")||n.endsWith(".h")||n.endsWith(".c")||n.endsWith(".xml"))
    return "text/plain; charset=utf-8";
  return "application/octet-stream";
}

void handleRoot() { if (!webAuth()) return; g_server.send_P(200, "text/html", WIFI_HTML); }

void handleList() {
  if (!webAuth()) return;
  String dir = g_server.hasArg("dir") ? g_server.arg("dir") : "/";
  if (!dir.startsWith("/")) dir = "/" + dir;
  if (dir.length() > 1 && dir.endsWith("/")) dir = dir.substring(0, dir.length() - 1);
  File d = SD.open(dir.c_str(), FILE_READ);
  String out = "[";
  if (d && d.isDirectory()) {
    bool first = true;
    for (File f = d.openNextFile(); f; f = d.openNextFile()) {
      String nm = f.name(); int sl = nm.lastIndexOf('/'); if (sl >= 0) nm = nm.substring(sl + 1);
      if (nm.length() && nm[0] == '.') { f.close(); continue; }   // ocultar .thumbnails, .clima.cache, etc.
      if (!first) out += ","; first = false;
      out += "{\"name\":\"" + jsonEsc(nm) + "\",\"size\":" + String((uint32_t)f.size()) +
             ",\"dir\":" + (f.isDirectory() ? "true" : "false") + "}";
      f.close();
    }
  }
  if (d) d.close();
  out += "]";
  g_server.send(200, "application/json", out);
}

// Serve a file. Supports HTTP Range (206) for video streaming/seeking and large downloads.
// ?dl=1 forces download (Content-Disposition); otherwise served inline (image/video in browser).
void handleDl() {
  if (!webAuth()) return;
  String path = g_server.arg("path");
  if (!path.startsWith("/")) path = "/" + path;
  File f = SD.open(path.c_str(), FILE_READ);
  if (!f || f.isDirectory()) { if (f) f.close(); g_server.send(404, "text/plain", "not found"); return; }
  size_t fileSize = f.size();
  String type = webContentType(path);
  bool isMedia = type.startsWith("audio/") || type.startsWith("video/");

  if (g_server.hasArg("dl")) {
    String base = path; int sl = base.lastIndexOf('/'); if (sl >= 0) base = base.substring(sl + 1);
    g_server.sendHeader("Content-Disposition", "attachment; filename=\"" + base + "\"");
  } else if (type.startsWith("image/")) {
    // Browser cache for images: re-entering a folder does not re-request anything
    // from the device (crucial for folders with hundreds/thousands of photos).
    g_server.sendHeader("Cache-Control", "public, max-age=604800");   // 7 days
  }
  g_server.sendHeader("Accept-Ranges", "bytes");

  // Range: bytes=start-end  (requires collectHeaders("Range") in startWifiMode)
  size_t startByte = 0, endByte = (fileSize > 0) ? fileSize - 1 : 0;
  bool partial = false;
  if (g_server.hasHeader("Range")) {
    String r = g_server.header("Range");
    if (r.startsWith("bytes=")) {
      int dash = r.indexOf('-');
      if (dash >= 6) startByte = (size_t) r.substring(6, dash).toInt();
      String es = r.substring(dash + 1); es.trim();
      if (es.length()) endByte = (size_t) es.toInt();
      if (endByte >= fileSize) endByte = (fileSize > 0) ? fileSize - 1 : 0;
      if (endByte >= startByte + (2 * 1024 * 1024)) endByte = startByte + (2 * 1024 * 1024) - 1;  // cap at 2 MB per response
      partial = (fileSize > 0 && startByte <= endByte && startByte < fileSize);
    }
  } else if (isMedia && fileSize > 0 && !g_server.hasArg("dl")) {
    // Audio/video WITHOUT a Range header: respond with 206 for the first chunk anyway.
    // This tells the browser we support Range, so it starts streaming immediately
    // rather than downloading the entire file first (which caused 30-40 s delays).
    endByte = (fileSize - 1 >= 2 * 1024 * 1024) ? (2 * 1024 * 1024 - 1) : fileSize - 1;
    partial = true;
  }

  uint8_t* buf = g_webBuf; size_t cap = g_webBufSz;   // shared PSRAM buffer (allocated in startWifiMode)
  if (partial && buf) {
    size_t len = endByte - startByte + 1;
    f.seek(startByte);
    g_server.sendHeader("Content-Range", "bytes " + String((uint32_t)startByte) + "-" +
                        String((uint32_t)endByte) + "/" + String((uint32_t)fileSize));
    g_server.setContentLength(len);
    g_server.send(206, type, "");
    WiFiClient client = g_server.client();
    size_t rem = len;
    while (rem > 0 && client.connected()) {
      size_t n = f.read(buf, rem < cap ? rem : cap);
      if (n == 0) break;
      client.write(buf, n);
      rem -= n;
    }
  } else {
    g_server.streamFile(f, type);     // full file (images, small files)
  }
  f.close();
}

// Thumbnail: serves <folder>/.thumbnails/<name-no-ext>.jpg if it exists; otherwise the original.
// Thumbnails are generated by tools/make_thumbs.py when preparing the SD card.
void handleThumb() {
  if (!webAuth()) return;
  String path = g_server.arg("path");
  int sl = path.lastIndexOf('/');
  String dir = (sl >= 0) ? path.substring(0, sl) : "";
  String name = (sl >= 0) ? path.substring(sl + 1) : path;
  int dot = name.lastIndexOf('.'); if (dot > 0) name = name.substring(0, dot);
  String tp = dir + "/.thumbnails/" + name + ".jpg";
  String serve = SD.exists(tp) ? tp : path;
  File f = SD.open(serve, FILE_READ);
  if (!f || f.isDirectory()) { if (f) f.close(); g_server.send(404, "text/plain", "not found"); return; }
  // Aggressive cache: thumbnails rarely change. Re-entering a 1000-photo folder
  // is served from browser cache without any requests (hard-refresh with Ctrl+F5 if regenerated).
  g_server.sendHeader("Cache-Control", "public, max-age=604800");   // 7 days
  g_server.streamFile(f, webContentType(serve));
  f.close();
}

// Recursive delete: empties the folder (files + subfolders including .thumbnails) then removes it.
bool rmrf(const String& path) {
  File f = SD.open(path);
  if (!f) return false;
  if (!f.isDirectory()) { f.close(); return SD.remove(path); }
  for (File c = f.openNextFile(); c; c = f.openNextFile()) {
    String cp = c.path(); c.close();
    rmrf(cp);
  }
  f.close();
  return SD.rmdir(path);
}
// Delete the thumbnail associated with a file (if it exists), to avoid orphan files.
void removeThumb(const String& path) {
  int sl = path.lastIndexOf('/'); String dir = (sl >= 0) ? path.substring(0, sl) : "";
  String name = (sl >= 0) ? path.substring(sl + 1) : path;
  int dot = name.lastIndexOf('.'); if (dot > 0) name = name.substring(0, dot);
  String tp = dir + "/.thumbnails/" + name + ".jpg";
  if (SD.exists(tp)) SD.remove(tp);
}
void handleRm() {
  if (!webAuth()) return;
  String path = g_server.arg("path");
  if (!path.startsWith("/")) path = "/" + path;
  File f = SD.open(path);
  if (!f) { g_server.send(404, "text/plain", "not found"); return; }
  bool isd = f.isDirectory(); f.close();
  bool ok;
  if (isd) ok = rmrf(path);
  else { ok = SD.remove(path); removeThumb(path); }
  g_server.send(ok ? 200 : 500, "text/plain", ok ? "ok" : "error");
}

void handleMkdir() {
  if (!webAuth()) return;
  String mkpath = g_server.arg("path");
  if (!mkpath.startsWith("/")) mkpath = "/" + mkpath;
  bool ok = SD.mkdir(mkpath.c_str());
  g_server.send(ok ? 200 : 500, "text/plain", ok ? "ok" : "error");
}

void handleSave() {
  if (!webAuth()) return;
  File f = SD.open(g_server.arg("path"), FILE_WRITE);
  if (!f) { g_server.send(500, "text/plain", "could not open"); return; }
  f.print(g_server.arg("data"));
  f.close();
  g_server.send(200, "text/plain", "ok");
}

void handleUpload() {
  HTTPUpload& u = g_server.upload();
  if (u.status == UPLOAD_FILE_START) {
    String dir = g_server.hasArg("dir") ? g_server.arg("dir") : "/";
    if (!dir.startsWith("/")) dir = "/" + dir;
    if (!dir.endsWith("/")) dir += "/";
    String path = dir + u.filename;
    if (g_upFile) g_upFile.close();
    g_upFile = SD.open(path.c_str(), FILE_WRITE);
    Serial.printf("[WIFI] uploading %s\n", path.c_str());
  } else if (u.status == UPLOAD_FILE_WRITE) {
    if (g_upFile) g_upFile.write(u.buf, u.currentSize);
  } else if (u.status == UPLOAD_FILE_END) {
    if (g_upFile) g_upFile.close();
  }
}

// --- Download a folder as ZIP (store method, no compression, streamed) ---
// Uses "data descriptor" (flag 0x08) so CRC/size are not needed before sending data.
struct ZipEnt { String name; uint32_t crc, size, off; };
static uint32_t s_crcTab[256]; static bool s_crcInit = false;
static uint32_t zipCrc(uint32_t crc, const uint8_t* d, size_t n) {
  if (!s_crcInit) { for (uint32_t i = 0; i < 256; i++) { uint32_t c = i;
      for (int k = 0; k < 8; k++) c = (c & 1) ? (0xEDB88520 ^ (c >> 1)) : (c >> 1); s_crcTab[i] = c; } s_crcInit = true; }
  for (size_t i = 0; i < n; i++) crc = s_crcTab[(crc ^ d[i]) & 0xFF] ^ (crc >> 8);
  return crc;
}
static void zU16(WiFiClient& c, uint16_t v, uint32_t& pos) { uint8_t b[2] = {(uint8_t)v, (uint8_t)(v >> 8)}; c.write(b, 2); pos += 2; }
static void zU32(WiFiClient& c, uint32_t v, uint32_t& pos) { uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)}; c.write(b, 4); pos += 4; }
static void zStr(WiFiClient& c, const String& s, uint32_t& pos) { c.write((const uint8_t*)s.c_str(), s.length()); pos += s.length(); }

static void zipAddDir(WiFiClient& c, const String& full, const String& rel, uint32_t& pos, std::vector<ZipEnt>& cd, uint8_t* buf, size_t bufSz) {
  File dir = SD.open(full);
  if (!dir || !dir.isDirectory()) { if (dir) dir.close(); return; }
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) {
    String fp = f.path(); String bn = fp; int sl = bn.lastIndexOf('/'); if (sl >= 0) bn = bn.substring(sl + 1);
    bool isdir = f.isDirectory(); f.close();
    if (bn.length() && bn[0] == '.') continue;   // exclude hidden dirs (.thumbnails, .clima.cache)
    if (!c.connected()) break;
    if (isdir) { zipAddDir(c, fp, rel + bn + "/", pos, cd, buf, bufSz); continue; }
    String name = rel + bn;
    uint32_t off = pos;
    zU32(c, 0x04034b50, pos); zU16(c, 20, pos); zU16(c, 0x0008, pos); zU16(c, 0, pos);   // sig, ver, flag(descriptor), metodo(store)
    zU16(c, 0, pos); zU16(c, 0, pos);                                                     // hora, fecha
    zU32(c, 0, pos); zU32(c, 0, pos); zU32(c, 0, pos);                                     // crc, comp, uncomp (written in the data descriptor)
    zU16(c, name.length(), pos); zU16(c, 0, pos); zStr(c, name, pos);
    uint32_t crc = 0xFFFFFFFF, sz = 0;
    File ff = SD.open(fp, FILE_READ);
    if (ff) { while (ff.available()) { int n = ff.read(buf, bufSz); if (n <= 0) break;
        c.write(buf, n); pos += n; crc = zipCrc(crc, buf, n); sz += n; if (!c.connected()) break; } ff.close(); }
    crc ^= 0xFFFFFFFF;
    zU32(c, 0x08074b50, pos); zU32(c, crc, pos); zU32(c, sz, pos); zU32(c, sz, pos);       // data descriptor
    cd.push_back({name, crc, sz, off});
    if (!c.connected()) break;
  }
  dir.close();
}

void handleZip() {
  if (!webAuth()) return;
  String dir = g_server.hasArg("dir") ? g_server.arg("dir") : "/";
  if (dir.length() > 1 && dir.endsWith("/")) dir = dir.substring(0, dir.length() - 1);
  String zn = dir; int sl = zn.lastIndexOf('/'); zn = (sl >= 0) ? zn.substring(sl + 1) : zn; if (zn.length() == 0) zn = "microSD";
  WiFiClient client = g_server.client();
  // Total length unknown -> respond with Connection: close (browser reads until close).
  client.print(String("HTTP/1.1 200 OK\r\nContent-Type: application/zip\r\n") +
               "Content-Disposition: attachment; filename=\"" + zn + ".zip\"\r\nConnection: close\r\n\r\n");
  uint32_t pos = 0; std::vector<ZipEnt> cd;
  if (!g_webBuf) { client.stop(); return; }                  // no buffer — should not happen in Wi-Fi mode
  zipAddDir(client, dir, "", pos, cd, g_webBuf, g_webBufSz);
  uint32_t cdoff = pos;
  for (auto& e : cd) {
    zU32(client, 0x02014b50, pos); zU16(client, 20, pos); zU16(client, 20, pos); zU16(client, 0x0008, pos); zU16(client, 0, pos);
    zU16(client, 0, pos); zU16(client, 0, pos);
    zU32(client, e.crc, pos); zU32(client, e.size, pos); zU32(client, e.size, pos);
    zU16(client, e.name.length(), pos); zU16(client, 0, pos); zU16(client, 0, pos);
    zU16(client, 0, pos); zU16(client, 0, pos); zU32(client, 0, pos);
    zU32(client, e.off, pos); zStr(client, e.name, pos);
  }
  uint32_t cdsize = pos - cdoff;
  zU32(client, 0x06054b50, pos); zU16(client, 0, pos); zU16(client, 0, pos);
  zU16(client, cd.size(), pos); zU16(client, cd.size(), pos);
  zU32(client, cdsize, pos); zU32(client, cdoff, pos); zU16(client, 0, pos);
  client.stop();
  Serial.printf("[WIFI] ZIP %s: %u files, %u bytes\n", zn.c_str(), (unsigned)cd.size(), (unsigned)pos);
}

void startWifiMode() {
  audioPowerOff();
  g_bootFetchPending = false;               // skip boot fetch while Wi-Fi mode is active
  if (!g_webBuf) {                          // I/O buffer in PSRAM (falls back to internal RAM)
    g_webBuf = (uint8_t*)ps_malloc(WEB_BUF_SZ); g_webBufSz = g_webBuf ? WEB_BUF_SZ : 0;
    if (!g_webBuf) { g_webBuf = (uint8_t*)malloc(8192); g_webBufSz = g_webBuf ? 8192 : 0; }
  }
  // 1) Try to join a saved network (STA). 2) If unavailable/fails, start own AP.
  WiFi.mode(WIFI_STA);
  g_apMode = !connectWiFi();
  if (g_apMode) {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(g_apSsid.c_str(), g_apPass.length() >= 8 ? g_apPass.c_str() : nullptr);
  } else {
    g_wifiStaFetch = true;   // joined a network: schedule weather refresh (done in loop after draw)
  }
  g_server.on("/", handleRoot);
  g_server.on("/list", handleList);
  g_server.on("/dl", handleDl);
  g_server.on("/thumb", handleThumb);
  g_server.on("/rm", handleRm);
  g_server.on("/mkdir", handleMkdir);
  g_server.on("/zip", handleZip);
  g_server.on("/save", HTTP_POST, handleSave);
  g_server.on("/up", HTTP_POST, []() { g_server.send(200, "text/plain", "ok"); }, handleUpload);
  g_server.onNotFound([]() { g_server.send(404, "text/plain", "404"); });
  static const char* HK[] = { "Range" };       // without this, hasHeader("Range") always returns false
  g_server.collectHeaders(HK, 1);
  g_server.begin();
  g_wifiModeOn = true;
  if (g_apMode)
    Serial.printf("[WIFI] AP '%s' -> http://192.168.4.1  login=%s\n", g_apSsid.c_str(), g_webUser.c_str());
  else
    Serial.printf("[WIFI] STA '%s' -> http://%s  login=%s\n",
                  WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), g_webUser.c_str());
}

void stopWifiMode() {
  if (!g_wifiModeOn) return;
  g_server.stop();
  if (g_upFile) g_upFile.close();        // close any in-progress upload (keeps SD handles clean)
  WiFi.softAPdisconnect(true);           // tear down the AP (if active)
  WiFi.disconnect(true);                 // and disconnect STA (if joined)
  WiFi.mode(WIFI_OFF);                   // Wi-Fi fully off
  if (g_webBuf) { free(g_webBuf); g_webBuf = nullptr; g_webBufSz = 0; }   // release I/O buffer
  g_wifiModeOn = false; g_apMode = false; g_wifiStaFetch = false;
  Serial.println("[WIFI] stopped, SD free.");
}

// Escape SSID/password for the WiFi QR string (\  ;  ,  :  " are escaped with \).
static String wifiQrEsc(const String& s) {
  String o; for (size_t i = 0; i < s.length(); i++) { char c = s[i];
    if (c=='\\'||c==';'||c==','||c==':'||c=='"') o += '\\'; o += c; }
  return o;
}
void drawWifi() {
  const int W = M5.Display.width(), H = M5.Display.height();
  canvas.fillSprite(WHITE);
  canvas.setFont(&fonts::FreeSansBold18pt7b); canvas.setTextDatum(top_left); canvas.setTextColor(BLACK);
  canvas.drawString("WI-FI", 12, 8);
  drawBatteryIcon(canvas, W - 53, 8);
  canvas.drawFastHLine(12, 42, W - 24, BLACK);

  if (!g_wifiModeOn) {
    // -------- WI-FI OFF --------
    canvas.setTextDatum(top_center);
    canvas.setFont(&fonts::FreeSansBold18pt7b); canvas.setTextColor(BLACK);
    canvas.drawString("WiFi Connection", W / 2, 150);
    canvas.setTextColor(RED);
    canvas.drawString("OFF", W / 2, 195);
    canvas.setFont(&fonts::FreeSans12pt7b); canvas.setTextColor(BLACK);
    canvas.drawString("Joins your WiFi if available;", W / 2, 285);
    canvas.drawString("otherwise creates its own access point.", W / 2, 312);
    canvas.setFont(&fonts::FreeSansBold12pt7b); canvas.setTextColor(BLUE);
    canvas.drawString("Press UP to activate", W / 2, 372);
    canvas.setFont(&fonts::FreeSans9pt7b); canvas.setTextColor(BLACK); canvas.setTextDatum(bottom_center);
    canvas.drawString("G1: exit WiFi mode", W / 2, H - 8);
    esp_task_wdt_reset(); canvas.pushSprite(0, 0);
    return;
  }

  // -------- WI-FI ACTIVE: info + QR (own AP or joined network) --------
  String ip = g_apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
  canvas.setTextDatum(top_center);
  canvas.setFont(&fonts::FreeSansBold18pt7b); canvas.setTextColor(BLACK);
  canvas.drawString(g_apMode ? "Access Point" : "Connected to WiFi", W / 2, 54);
  canvas.setFont(&fonts::FreeSansBold12pt7b); canvas.setTextColor(BLACK);
  canvas.drawString("Network: " + (g_apMode ? g_apSsid : WiFi.SSID()), W / 2, 92);
  if (g_apMode) canvas.drawString("Password: " + g_apPass, W / 2, 116);
  canvas.setTextColor(RED);
  canvas.drawString("http://" + ip, W / 2, 142);
  canvas.setTextColor(BLACK);
  canvas.drawString("web: " + g_webUser + " / " + g_webPass, W / 2, 168);

  // QR: in AP mode -> join the network (WIFI:...); in STA mode -> open the URL directly.
  String qr = g_apMode ? ("WIFI:T:WPA;S:" + wifiQrEsc(g_apSsid) + ";P:" + wifiQrEsc(g_apPass) + ";;")
                       : ("http://" + ip);
  int L = qr.length();
  uint8_t ver = (L <= 25) ? 3 : (L <= 40) ? 4 : (L <= 60) ? 5 : (L <= 84) ? 6 : 8;
  int qw = 200, qx = (W - qw) / 2, qy = 198;
  canvas.fillRect(qx - 8, qy - 8, qw + 16, qw + 16, WHITE);
  canvas.qrcode(qr, qx, qy, qw, ver);
  canvas.setFont(&fonts::FreeSans12pt7b); canvas.setTextColor(BLACK);
  canvas.drawString(g_apMode ? "Scan to join the network" : "Scan (same network) or open URL", W / 2, qy + qw + 14);

  canvas.setFont(&fonts::FreeSans9pt7b); canvas.setTextDatum(bottom_center);
  canvas.drawString("DOWN: turn off    G1: exit", W / 2, H - 8);
  esp_task_wdt_reset(); canvas.pushSprite(0, 0);
}

void drawCurrentMode() {
  auto dt = M5.Rtc.getDateTime();
  switch (g_mode) {
    case MODE_CLIMA:
      if (g_view == 0 || numLocs() == 0) drawWeatherLocal(dt);
      else                               drawWeatherCity(dt, g_view - 1);
      break;
    case MODE_CARRUSEL: drawCarrusel(); break;
    case MODE_MUSICA:   drawMusic(); break;
    case MODE_LIBRO:    if (g_libState == LIB_LIST) drawLibroList(); else drawLibro(); break;
    case MODE_WIFI:     drawWifi(); break;
    default: break;
  }
}

void applyEpdMode() {
  // On the ED2208, modes only change dithering (not speed): chosen by content type.
  // Photos -> quality (best colour); books -> text (sharp text);
  // everything else (weather/music, with colour) -> fast.
  epd_mode_t m;
  switch (g_mode) {
    case MODE_CARRUSEL: m = epd_mode_t::epd_quality; break;
    case MODE_LIBRO:    m = epd_mode_t::epd_text;    break;
    default:            m = epd_mode_t::epd_fast;    break;
  }
  M5.Display.setEpdMode(m);
}

// ============================ HIDDEN MODE: TV-B-Gone (IR) ============================
// Blink the built-in RGB LED (without touching the screen).
void ledBlink(uint8_t r, uint8_t g, uint8_t b, int times) {
  if (!M5.Led.isEnabled()) return;
  M5.Led.setBrightness(120);
  for (int i = 0; i < times; i++) {
    M5.Led.setAllColor(r, g, b); delay(180);
    M5.Led.setAllColor(0, 0, 0); delay(150);
  }
}

// G1 double-click: fires a sequence of European TV power-off codes.
// Does NOT use the screen: 2 green blinks at start, red blinks at end.
void tvBGone() {
  ledBlink(0, 255, 0, 2);                   // verde: empezando
  bool cancel = false;
  for (int i = 0; i < numCodigosEU; i++) {
    if (digitalRead(PIN_BTN_MODE) == LOW) { cancel = true; break; }   // G1 cancels
    const TVCode& c = codigosEU[i];
    switch (c.proto) {
      case P_NEC:       irsend.sendNEC(c.code, c.bits);         break;
      case P_SAMSUNG:   irsend.sendSAMSUNG(c.code, c.bits);     break;
      case P_SONY12:    irsend.sendSony(c.code, 12, 2);         break;
      case P_SONY15:    irsend.sendSony(c.code, 15, 2);         break;
      case P_SONY20:    irsend.sendSony(c.code, 20, 2);         break;
      case P_RC5:       irsend.sendRC5(c.code, c.bits);         break;
      case P_RC6:       irsend.sendRC6(c.code, c.bits);         break;
      case P_PANASONIC: irsend.sendPanasonic64(c.code, c.bits); break;
      case P_JVC:       irsend.sendJVC(c.code, c.bits, 1);      break;
    }
    delay(75);
  }
  ledBlink(255, 0, 0, cancel ? 3 : 2);      // red: done (3 blinks if cancelled)
  M5.Led.setAllColor(0, 0, 0);              // LED off
}

// ============================ MODE CONTROL ============================
// Next ENABLED mode after 'from' (exclusive). Returns 'from' if no other mode is enabled.
Mode nextEnabledMode(Mode from) {
  for (int i = 1; i <= MODE_COUNT; i++) {
    Mode m = (Mode)(((int)from + i) % MODE_COUNT);
    if (g_modeEnabled[m]) return m;
  }
  return from;
}
void changeMode() {
  Mode nm = nextEnabledMode(g_mode);
  if (nm == g_mode) return;            // no other mode enabled: nothing to do
  if (g_mode == MODE_WIFI) stopWifiMode();   // leaving Wi-Fi mode: shut down AP and server
  g_mode = nm;
  prefs.putUChar("mode", (uint8_t)g_mode);  // persist mode selection
  Serial.printf("-> Mode %s\n", MODE_NAMES[g_mode]);
  if (g_mode == MODE_CARRUSEL) g_lastCarouselSec = rtcNow();
  else setPanelRotation(0);          // all other modes use portrait orientation
  if (g_mode == MODE_LIBRO) { g_libState = LIB_LIST; g_sel = g_bookIdx; }  // show selector on entry
  if (g_mode == MODE_MUSICA) audioPowerOn();   // power codec/amp only in music mode
  else                       audioPowerOff();  // on exit: stop audio and power off codec/amp
  // Wi-Fi mode starts with AP OFF: the user activates it with UP button (see modeDown).
  applyEpdMode();
  g_needRedraw = true;
}
void modeLongPress() {
  if (g_mode == MODE_CLIMA) {
    showBusy("Updating (WiFi)...");
    fetchAllOnline(true);
    g_busy = false; g_needRedraw = true;
  } else if (g_mode == MODE_LIBRO) {           // return to book selector
    g_libState = LIB_LIST; g_sel = g_bookIdx; g_needRedraw = true;
  }
}
void modeUp() {
  if (g_mode == MODE_CLIMA) { g_view = (g_view + 1) % numViews(); g_needRedraw = true; }
  else if (g_mode == MODE_CARRUSEL && !g_images.empty()) {
    g_img_idx = (g_img_idx + 1) % g_images.size(); g_lastCarouselSec = rtcNow(); g_needRedraw = true;
  }
  else if (g_mode == MODE_WIFI) { if (g_wifiModeOn) stopWifiMode(); g_needRedraw = true; }      // UP button: turn off AP
}
void modeDown() {
  if (g_mode == MODE_CLIMA) { g_view = (g_view + numViews() - 1) % numViews(); g_needRedraw = true; }
  else if (g_mode == MODE_CARRUSEL && !g_images.empty()) {
    g_img_idx = (g_img_idx + g_images.size() - 1) % g_images.size(); g_lastCarouselSec = rtcNow(); g_needRedraw = true;
  }
  else if (g_mode == MODE_WIFI) { if (!g_wifiModeOn) startWifiMode(); g_needRedraw = true; }     // DOWN button: start AP
}

// ============================ POWER MANAGEMENT ============================
// Current RTC time in seconds (immune to sleep, unlike millis()).
time_t rtcNow() {
  auto dt = M5.Rtc.getDateTime();
  struct tm t = {};
  t.tm_year = dt.date.year - 1900; t.tm_mon = dt.date.month - 1; t.tm_mday = dt.date.date;
  t.tm_hour = dt.time.hours; t.tm_min = dt.time.minutes; t.tm_sec = dt.time.seconds;
  return mktime(&t);
}

void enterDeepSleep() {
  // Power-off managed by PMIC (M5PM1). Minimum consumption; does NOT wake on its own:
  // G1/UP/DOWN are ESP GPIOs whose pull-up rail is cut during sleep,
  // so the only reliable wake source is the POWER button (connected to PMIC).
  // E-paper retains the last image even when powered off.
  Serial.println("Deep sleep: inactivity timeout (PMIC power-off). Press POWER to wake.");
  Serial.flush();
  audioPowerOff();
  WiFi.mode(WIFI_OFF);
  delay(50);
  M5.Power.powerOff();      // does not return: PMIC cuts power
}

void lightSleepFor(uint32_t ms) {
  const gpio_num_t btns[] = {(gpio_num_t)PIN_BTN_MODE, (gpio_num_t)PIN_BTN_UP, (gpio_num_t)PIN_BTN_DOWN};
  for (auto p : btns) {
    gpio_sleep_sel_dis(p);                       // keep input+pullup active during light sleep (reliable wake on S3)
    gpio_wakeup_enable(p, GPIO_INTR_LOW_LEVEL);
  }
  esp_sleep_enable_gpio_wakeup();
  esp_sleep_enable_timer_wakeup((uint64_t)ms * 1000ULL);
  esp_light_sleep_start();   // wakes on button or timer; loop continues normally
  // If woken by TIMER (not button), ignore pin glitches for ~250 ms
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_TIMER) g_ignoreInputUntil = millis() + 250;
}

// Idle: light sleep when inactive (wakes on button or next scheduled refresh); deep sleep after timeout.
void powerTick() {
  uint32_t now = millis();
  // Do not sleep if a redraw is pending, busy, in music mode (audio), or in Wi-Fi mode (server running)
  if (g_needRedraw || g_busy || g_mode == MODE_MUSICA || g_mode == MODE_WIFI) { delay(5); return; }
  if (now - g_lastInput < IDLE_SLEEP_MS) { delay(10); return; }   // margin for double-click/hold detection

  // All measured with RTC (seconds), immune to millis() freezing during light sleep
  time_t nowSec = rtcNow();
  long idleSec = (long)(nowSec - g_lastActiveSec);
  if (idleSec >= (long)(g_deepSleepMs / 1000)) { enterDeepSleep(); }   // no retorna

  long wakeSec = 600;   // cap at 10 minutes
  if (g_mode == MODE_CLIMA && g_view == 0) {          // next local clock refresh
    long intervalSec = (idleSec >= 300) ? 300 : 60;
    long dueIn = intervalSec - (long)(nowSec - g_lastLocalUpdateSec);
    if (dueIn < 1) dueIn = 1; if (dueIn < wakeSec) wakeSec = dueIn;
  } else if (g_mode == MODE_CARRUSEL && sd_ready && !g_images.empty()) {  // next carousel photo
    long dueIn = (long)(g_carouselMs / 1000) - (long)(nowSec - g_lastCarouselSec);
    if (dueIn < 1) dueIn = 1; if (dueIn < wakeSec) wakeSec = dueIn;
  }
  lightSleepFor((uint32_t)wakeSec * 1000UL);
}

// ============================ SETUP ============================
void setup() {
  auto cfg = M5.config();
  cfg.clear_display = false;
  cfg.internal_spk  = false;   // ES8311 codec is managed by the audio library, not M5.Speaker
  M5.begin(cfg);
  Serial.begin(115200);
  delay(200);
  Serial.println("\n=== M5Paper Color - Multi-mode Station ===");

  M5.Display.setEpdMode(epd_mode_t::epd_fast);
  setPanelRotation(0);               // portrait UI (400x600), creates sprite

  pinMode(PIN_BTN_MODE, INPUT_PULLUP);
  pinMode(PIN_BTN_UP,   INPUT_PULLUP);
  pinMode(PIN_BTN_DOWN, INPUT_PULLUP);
  btnMode.setHoldThresh(800);
  btnUp.setHoldThresh(700);                  // hold UP/DOWN = play/pause in music mode
  btnDown.setHoldThresh(700);

  showBusy("Starting..."); g_busy = false;

  if (pm1.begin(&M5.In_I2C, M5PM1_DEFAULT_ADDR, M5PM1_I2C_FREQ_100K) == M5PM1_OK) {
    pm1.setLdoEnable(true);
    Serial.println("M5PM1 OK, LDO enabled.");
  } else Serial.println("WARNING: M5PM1 not initialised.");

  if (!M5.Rtc.isEnabled()) Serial.println("WARNING: RTC not found.");

  sht_ready = initSHT40();
  if (!sht_ready) Serial.println("WARNING: SHT40 not found.");
  readSHT40();

  // SD first, then config (from SD), then size the caches
  sd_ready = initSD();
  loadDefaults();
  loadConfig();                          // overrides defaults if /config.json is present
  g_clima.assign(numLocs(), ClimaCache{});
  loadClima();                           // restore last saved forecast (shown instantly on boot)
  scanImages();                          // list photos in the configured folder (/Fotos)
  scanMusic();                           // scan songs from /Music
  scanBooks();                           // scan .txt books from /Library
  loadFonts();                           // load VLW fonts with Cyrillic/accents (title + body)
  audioInit();                           // initialise ES8311 codec + I2S
  irsend.begin();                        // IR transmitter (hidden TV-B-Gone mode)

  // Fast start: do NOT block on WiFi. Draw the local screen first
  // (RTC time + sensor, instant), then fetch WiFi/NTP/weather in the background
  // after the first draw (g_bootFetchPending, handled in loop()).
  g_busy = false;
  Serial.println("[SETUP] irsend done, opening prefs...");
  prefs.begin("papercolor", false);
  Serial.println("[SETUP] prefs OK, reading mode...");
  g_mode = (Mode)prefs.getUChar("mode", MODE_CLIMA);
  if (g_mode >= MODE_COUNT) g_mode = MODE_CLIMA;
  if (!g_modeEnabled[g_mode]) g_mode = nextEnabledMode(g_mode);
  Serial.printf("[SETUP] mode=%d, applying EPD mode...\n", (int)g_mode);
  applyEpdMode();
  Serial.println("[SETUP] EPD mode OK");
  // Book selector starts on the last-read book (each book loads its own saved page on open)
  int lastBook = prefs.getInt("lastbook", 0);
  if (!g_books.empty()) {
    if (lastBook < 0 || lastBook >= (int)g_books.size()) lastBook = 0;
    g_bookIdx = lastBook; g_sel = lastBook;
  }
  g_libState = LIB_LIST;
  Serial.printf("Starting mode: %s\n", MODE_NAMES[g_mode]);
  // Re-print book scan result here so it's visible even if serial monitor connected late
  Serial.printf("[BOOKS] count=%u dir=%s\n", (unsigned)g_books.size(), g_librosDir.c_str());
  for (auto& b : g_books) Serial.printf("  book: %s\n", b.c_str());

  g_lastInput = millis();
  g_lastActiveSec = rtcNow();              // inactivity baseline (RTC)
  g_lastLocalUpdateSec = rtcNow();
  g_lastCarouselSec = rtcNow();
  g_ignoreInputUntil = millis() + 1000;   // ignore the wake/boot button press (~1 s)
  g_needRedraw = true;
}

// ============================ LOOP ============================
void loop() {
  if (g_audioReady) audio.loop();      // feed the decoder (call as often as possible)
  M5.update();

  uint32_t ms = millis();
  btnMode.setRawState(ms, digitalRead(PIN_BTN_MODE) == LOW);
  btnUp.setRawState(ms,   digitalRead(PIN_BTN_UP)   == LOW);
  btnDown.setRawState(ms, digitalRead(PIN_BTN_DOWN) == LOW);

  if (ms >= g_ignoreInputUntil) {   // ignore the wake/boot button press
  if (btnMode.wasDoubleClicked())      tvBGone();        // double-click: fire TV-B-Gone (IR)
  else if (btnMode.wasHold())          modeLongPress();  // hold: mode action
  else if (btnMode.wasSingleClicked()) changeMode();     // 1 confirmed click: change mode

  if (g_mode == MODE_MUSICA) {
    // click = volume +/-, double-click = next/prev track, hold = play/pause
    if (btnUp.wasDoubleClicked())      musicNext();
    else if (btnUp.wasHold())          musicTogglePlay();
    else if (btnUp.wasClicked())       musicVolDown();
    if (btnDown.wasDoubleClicked())    musicPrev();
    else if (btnDown.wasHold())        musicTogglePlay();
    else if (btnDown.wasClicked())     musicVolUp();
  } else if (g_mode == MODE_LIBRO) {
    if (g_libState == LIB_LIST) {
      // selector: single-click moves, double-click opens. (wasSingleClicked prevents
      // moving on the first click of a double-click would trigger a redraw and break detection.)
      if (btnUp.wasDoubleClicked() || btnDown.wasDoubleClicked()) bookOpenSelected();
      else if (btnUp.wasSingleClicked())   listMove(+1);
      else if (btnDown.wasSingleClicked()) listMove(-1);
    } else {
      // reading: UP/DOWN turns page
      if (btnUp.wasClicked())   pageNext();
      if (btnDown.wasClicked()) pagePrev();
    }
  } else {
    if (btnUp.wasPressed())   modeUp();      // other modes: immediate response
    if (btnDown.wasPressed()) modeDown();
  }
  }  // end initial button guard
  // Only a REAL button press resets the inactivity timer, and not within the guard window after wake
  // (so a pin glitch on light-sleep exit does not reset the deep-sleep counter).
  if (ms >= g_ignoreInputUntil &&
      (btnMode.wasPressed() || btnUp.wasPressed() || btnDown.wasPressed())) {
    g_lastInput = ms; g_lastActiveSec = rtcNow();
  }

  // RTC-based timers (millis() does not advance reliably during light sleep)
  time_t nowSec = ((g_mode == MODE_CLIMA && g_view == 0) || g_mode == MODE_CARRUSEL) ? rtcNow() : 0;

  // LOCAL view: 1 full refresh per minute; after 5 min idle, every 5 min (power saving).
  if (g_mode == MODE_CLIMA && g_view == 0 && !g_busy && !g_needRedraw) {
    long idleSec    = (long)(nowSec - g_lastActiveSec);
    long intervalSec = (idleSec >= 300) ? 300 : 60;  // 5-min interval after 5 min idle
    if ((long)(nowSec - g_lastLocalUpdateSec) >= intervalSec) {
      readSHT40();
      g_needRedraw = true;     // drawWeatherLocal actualiza g_lastLocalUpdateSec
    }
  }

  // Automatic carousel advance (RTC-based; wraps by modulo)
  if (g_mode == MODE_CARRUSEL && sd_ready && !g_images.empty()
      && (long)(nowSec - g_lastCarouselSec) >= (long)(g_carouselMs / 1000)) {
    g_lastCarouselSec = nowSec;
    g_img_idx = (g_img_idx + 1) % g_images.size();
    g_needRedraw = true;
  }

  // Track ended -> play next (set by audio_eof_* global callbacks above)
  if (g_trackEnded) { g_trackEnded = false; if (g_playing) musicNext(); }

  // The idle counter for LIGHT SLEEP starts after drawing finishes (not at button press):
  // since a refresh takes ~12 s (> IDLE_SLEEP_MS), without this the device would sleep right after
  // drawing and the next press would only "wake" it without navigating. This keeps it awake
  // for a moment after each refresh.
  if (g_needRedraw && !g_busy) {
    g_needRedraw = false;
    static bool firstDraw = true;
    if (firstDraw) {
      firstDraw = false;
      Serial.printf("[DRAW] first draw: mode=%s books=%u libState=%d\n",
                    MODE_NAMES[g_mode], (unsigned)g_books.size(), (int)g_libState);
    }
    drawCurrentMode(); g_lastInput = millis();
  }

  // Fast start: local screen already drawn; now fetch WiFi+NTP+weather ONCE in the background.
  // No immediate redraw: the local view refreshes on its own cycle (1/5 min, with NTP-synced time)
  // and city weather cards appear when the user navigates to a city.
  if (g_bootFetchPending && g_mode != MODE_WIFI && !g_needRedraw && !g_busy) {
    g_bootFetchPending = false;
    fetchAllOnline(true);
  }

  // Wi-Fi mode with AP/STA active: handle web server (no sleep). Otherwise: power save.
  if (g_mode == MODE_WIFI && g_wifiModeOn) {
    // If joined a network (STA), refresh NTP + weather for all cities once.
    if (g_wifiStaFetch) { g_wifiStaFetch = false; Serial.println("[WIFI] STA: refreshing weather (Open-Meteo)..."); fetchWeatherData(true); }
    g_server.handleClient();
  }
  else if (g_mode == MODE_MUSICA && g_playing) { /* no delay: keep audio smooth */ }
  else powerTick();
}
