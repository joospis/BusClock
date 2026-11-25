/*
  BusClock - PVTA Route 30 countdown sign
  ESP32 + 64x32 HUB75 LED panel (P5, 1/16 scan)

  Screen layout (matches the mockup):
    +----------------------------------------------------------------+
    |                          4:13                                  |  clock
    +-------------------------------------------+--------------------+
    |                                           |        >>          |
    |                 7 MIN                     |        22M         |  next bus | the one after
    +-------------------------------------------+--------------------+

  Libraries (Arduino Library Manager):
    - "ESP32 HUB75 LED MATRIX PANEL DMA Display" by Faptastic (mrfaptastic)
      (it also installs "Adafruit GFX Library")
    - "ArduinoJson" by Benoit Blanchon, version 7.x
  Board: ESP32 Dev Module (classic ESP32 or ESP32-S3; not C3/C6)

  First boot: the panel shows WIFI SETUP. On your phone, join the Wi-Fi network
  "BusClock" (password 30303030). A setup page opens (or browse to
  http://192.168.4.1). Pick your home Wi-Fi and your stop, then Save.
  Afterwards the same page is at http://busclock.local on your home network.
  Hold the BOOT button for 3 seconds at any time to bring the setup network back.

  Everything comes from PVTA's bus tracker (InfoPoint): departures every 30
  seconds, and the route's stop list once a day. Nothing is stored on the
  ESP32, so schedule changes show up on their own. If the tracker can't be
  reached, the clock keeps counting down to the departures it last received
  and dims the >> icon after 3 minutes so you can tell.
*/

#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <time.h>
#include <utility>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <ESP32-HUB75-MatrixPanel-I2S-DMA.h>
#include "live_parse.h"

// ------------------------------------------------------------------ settings
#define AP_SSID      "BusClock"
#define AP_PASS      "30303030"        // 8+ characters; shown on the panel during setup
#define HOSTNAME     "busclock"        // http://busclock.local
#define TZ_INFO      "EST5EDT,M3.2.0,M11.1.0"  // America/New_York
#define BOOT_BUTTON  0                 // hold 3 s to open the setup network
#define DEFAULT_STOP "34:S"            // Puffton Village Apts, southbound (toward Old Belchertown Rd)

// PVTA InfoPoint (bus tracker). Stop keys are "<StopId>:<N|S>".
#define ROUTE_ID         30030         // InfoPoint's id for Route 30 (not the GTFS route_id)
#define ROUTE_NAME       "30"
#define LIVE_BASE        "https://bustracker.pvta.com/InfoPoint/rest/"
#define LIVE_INTERVAL_MS 30000UL       // minimum time between requests to PVTA
static_assert(LIVE_INTERVAL_MS >= 30000UL, "Poll PVTA at most every 30 seconds");
#define LIVE_MAX_AGE_S   180           // after this long without fresh data, dim the >> icon
#define STOPS_REFRESH_MS (24UL * 3600 * 1000)  // reload the stop list daily
#define MAX_STOPS        64

// HUB75 wiring (ESP32 GPIO numbers). These are the library's defaults for a
// classic ESP32 dev board. A 64x32 1/16-scan panel doesn't use the E line.
#define PIN_R1  25
#define PIN_G1  26
#define PIN_B1  27
#define PIN_R2  14
#define PIN_G2  12
#define PIN_B2  13
#define PIN_A   23
#define PIN_B   19
#define PIN_C    5
#define PIN_D   17
#define PIN_E   -1
#define PIN_LAT  4
#define PIN_OE  15
#define PIN_CLK 16

#define PANEL_W 64
#define PANEL_H 32

// Colors (from the mockup). LEDs look brighter than a screen; tweak to taste.
#define RGB565(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
static const uint16_t C_CLOCK   = RGB565(0xDD, 0xDD, 0xDD);
static const uint16_t C_LABEL   = RGB565(0xDD, 0xDD, 0xDD);
static const uint16_t C_LINE    = RGB565(0x33, 0x33, 0x33);
static const uint16_t C_CHEVRON = RGB565(0x77, 0x77, 0x77);   // live data
static const uint16_t C_CHEV_OLD = RGB565(0x2A, 0x2A, 0x2A);  // showing cached data
static const uint16_t C_NEXT    = RGB565(0x99, 0x99, 0x99);
static const uint16_t C_DIM     = RGB565(0x77, 0x77, 0x77);
static const uint16_t C_GREEN   = RGB565(0x33, 0xEE, 0x55);
static const uint16_t C_AMBER   = RGB565(0xFF, 0xAA, 0x00);
static const uint16_t C_RED     = RGB565(0xFF, 0x33, 0x33);

// ------------------------------------------------------------------ state
MatrixPanel_I2S_DMA* panel = nullptr;
WebServer server(80);
DNSServer dns;
Preferences prefs;

String cfgSsid, cfgPass, cfgStop;
uint8_t cfgWalk = 4, cfgBri = 128, cfgNightBri = 20, cfgNightStart = 23, cfgNightEnd = 7, cfgDriver = 0;

// The chosen stop, parsed from cfgStop (guarded by liveMux).
uint32_t stopId = 0;   // 0 = none chosen
char stopDir = 'S';
bool timeValid();
bool apActive = false, forceSetup = false, mdnsStarted = false, wasConnected = false;
uint32_t staConnectedAt = 0, apStartedAt = 0, lastStaTry = 0, disconnectedSince = 0, bootPressedAt = 0, restartAt = 0;
uint8_t appliedBri = 0;

// Live data shared between the fetch task and the display (guarded by liveMux)
portMUX_TYPE liveMux = portMUX_INITIALIZER_UNLOCKED;
time_t liveDeps[8];
int liveCount = 0;
time_t liveFetchedAt = 0;          // last successful fetch (epoch seconds)
char liveError[48] = "";

// Route stop list from InfoPoint (guarded by stopsLock)
SemaphoreHandle_t stopsLock = nullptr;
LiveStop routeStops[MAX_STOPS];
int routeStopCount = 0;
char towardN[LIVE_NAME_LEN] = "", towardS[LIVE_NAME_LEN] = "";
uint32_t stopsLoadedAt = 0;        // millis() of the last successful load, 0 = never

// "34:S" -> id 34, dir 'S'. Older saves used GTFS direction ids (0 = N, 1 = S).
bool parseStopKey(const char* key, uint32_t* id, char* dir) {
  char* end;
  unsigned long v = strtoul(key, &end, 10);
  if (end == key || *end != ':' || v == 0) return false;
  char d = end[1];
  if (d == '0') d = 'N';
  else if (d == '1') d = 'S';
  if ((d != 'N' && d != 'S') || end[2] != 0) return false;
  *id = v; *dir = d;
  return true;
}

String stopKey(uint32_t id, char dir) { return String(id) + ':' + dir; }

// ------------------------------------------------------------------ font (3x5)
static const char FONT_CHARS[] = " !-.:0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
static const uint8_t FONT[][5] = {
  {0,0,0,0,0},{2,2,2,0,2},{0,0,7,0,0},{0,0,0,0,2},{0,2,0,2,0},           // space ! - . :
  {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},           // 0-4
  {7,4,7,1,7},{7,4,7,5,7},{7,1,2,2,2},{7,5,7,5,7},{7,5,7,1,7},           // 5-9
  {2,5,7,5,5},{6,5,6,5,6},{3,4,4,4,3},{6,5,5,5,6},{7,4,6,4,7},{7,4,6,4,4}, // A-F
  {3,4,5,5,3},{5,5,7,5,5},{7,2,2,2,7},{1,1,1,5,2},{5,5,6,5,5},{4,4,4,4,7}, // G-L
  {5,7,7,5,5},{6,5,5,5,5},{2,5,5,5,2},{6,5,6,4,4},{2,5,5,6,3},{6,5,6,5,5}, // M-R
  {3,4,2,1,6},{7,2,2,2,2},{5,5,5,5,7},{5,5,5,5,2},{5,5,7,7,5},{5,5,2,5,5}, // S-X
  {5,5,2,2,2},{7,1,2,4,7}                                                  // Y Z
};
static const uint8_t CHEVRON[5] = {0x48, 0x24, 0x12, 0x24, 0x48};  // 7 px wide, ">>"

void px(int x, int y, uint16_t c) {
  if (x >= 0 && x < PANEL_W && y >= 0 && y < PANEL_H) panel->drawPixel(x, y, c);
}

int textWidth(const char* s, int k) {
  int n = strlen(s);
  return n ? n * 4 * k - k : 0;
}

void drawText(int x, int y, const char* s, uint16_t c, int k) {
  for (; *s; s++) {
    char ch = toupper((unsigned char)*s);
    const char* p = strchr(FONT_CHARS, ch);
    int g = p ? (int)(p - FONT_CHARS) : 0;
    for (int r = 0; r < 5; r++)
      for (int col = 0; col < 3; col++)
        if (FONT[g][r] & (4 >> col))
          for (int dy = 0; dy < k; dy++)
            for (int dx = 0; dx < k; dx++) px(x + col * k + dx, y + r * k + dy, c);
    x += 4 * k;
  }
}

void drawCentered(int y, const char* s, uint16_t c, int k) {
  drawText((PANEL_W - textWidth(s, k)) / 2, y, s, c, k);
}

// ------------------------------------------------------------------ departures
// Next departures from the last bus tracker response that haven't left yet.
// *fetched is when that response arrived (0 = never); it's fresh if within LIVE_MAX_AGE_S.
int getDepartures(time_t now, time_t* out, int maxOut, time_t* fetched) {
  time_t l[8];
  int ln;
  portENTER_CRITICAL(&liveMux);
  ln = liveCount;
  memcpy(l, liveDeps, sizeof(time_t) * ln);
  *fetched = liveFetchedAt;
  portEXIT_CRITICAL(&liveMux);
  int n = 0;
  for (int i = 0; i < ln && n < maxOut; i++)
    if (l[i] >= now) out[n++] = l[i];
  return n;
}

// "Puffton Village Apts toward Old Belchertown Road" for the status page.
String stopLabel(uint32_t id, char dir) {
  String s;
  if (stopsLock && xSemaphoreTake(stopsLock, pdMS_TO_TICKS(200))) {
    for (int i = 0; i < routeStopCount; i++)
      if (routeStops[i].id == id && routeStops[i].dir == dir) {
        s = String(routeStops[i].name) + " toward " + (dir == 'N' ? towardN : towardS);
        break;
      }
    xSemaphoreGive(stopsLock);
  }
  if (s.length() == 0) s = "Stop " + String(id) + (dir == 'N' ? " northbound" : " southbound");
  return s;
}

void setLiveError(const char* msg) {
  portENTER_CRITICAL(&liveMux);
  strncpy(liveError, msg, sizeof liveError - 1);
  liveError[sizeof liveError - 1] = 0;
  portEXIT_CRITICAL(&liveMux);
  Serial.printf("Live data: %s\n", msg);
}

// GET LIVE_BASE + path and parse the JSON (through `filter`) into doc.
bool fetchJson(const String& path, JsonDocument& filter, JsonDocument& doc) {
  WiFiClientSecure client;
  client.setInsecure();   // public, read-only data; skips certificate checks
  HTTPClient http;
  http.useHTTP10(true);   // no chunked encoding, so we can parse straight from the stream
  http.setTimeout(10000);
  if (!http.begin(client, String(LIVE_BASE) + path)) { setLiveError("connect failed"); return false; }
  http.addHeader("Accept", "application/json");
  int code = http.GET();
  if (code != 200) {
    char m[32];
    snprintf(m, sizeof m, code < 0 ? "network error %d" : "HTTP %d", code);
    setLiveError(m);
    http.end();
    return false;
  }
  DeserializationError err = deserializeJson(doc, http.getStream(), DeserializationOption::Filter(filter));
  http.end();
  if (err) { setLiveError(err.c_str()); return false; }
  return true;
}

void fetchDepartures() {
  uint32_t id;
  char dir;
  portENTER_CRITICAL(&liveMux);
  id = stopId; dir = stopDir;
  portEXIT_CRITICAL(&liveMux);
  if (!id) return;

  JsonDocument filter, doc;
  live_buildFilter(filter);
  if (!fetchJson("StopDepartures/Get/" + String(id), filter, doc)) return;

  time_t now = time(nullptr);
  time_t found[8];
  int n = live_extract(doc, ROUTE_ID, dir, now, found, 8);
  portENTER_CRITICAL(&liveMux);
  if (id == stopId && dir == stopDir) {   // ignore if the stop changed meanwhile
    memcpy(liveDeps, found, sizeof(time_t) * n);
    liveCount = n;
    liveFetchedAt = now;
    liveError[0] = 0;
  }
  portEXIT_CRITICAL(&liveMux);
}

void fetchStops() {
  JsonDocument filter, doc;
  live_buildStopsFilter(filter);
  if (!fetchJson("RouteDetails/Get/" + String(ROUTE_ID), filter, doc)) return;

  static LiveStop found[MAX_STOPS];
  int n = live_extractStops(doc, found, MAX_STOPS);
  if (n == 0) { setLiveError("empty stop list"); return; }
  xSemaphoreTake(stopsLock, portMAX_DELAY);
  memcpy(routeStops, found, sizeof(LiveStop) * n);
  routeStopCount = n;
  strncpy(towardN, live_towardName(doc, 'N'), LIVE_NAME_LEN - 1);
  strncpy(towardS, live_towardName(doc, 'S'), LIVE_NAME_LEN - 1);
  stopsLoadedAt = millis() | 1;
  xSemaphoreGive(stopsLock);
  Serial.printf("Loaded %d stops for route %s\n", n, ROUTE_NAME);
}

// Runs on its own so a slow HTTPS request never freezes the clock or setup page.
// This is the only place that contacts PVTA, and it makes at most one request
// per LIVE_INTERVAL_MS (30 s), counting failed attempts. When the stop list
// needs (re)loading it takes every other slot, so departures stay current.
void liveTask(void*) {
  uint32_t last = 0;
  bool attempted = false, lastWasDeps = false;
  for (;;) {
    bool allowed = !attempted || millis() - last >= LIVE_INTERVAL_MS;
    if (allowed && WiFi.status() == WL_CONNECTED && timeValid()) {
      attempted = true;
      last = millis();
      bool needStops = !stopsLoadedAt || millis() - stopsLoadedAt > STOPS_REFRESH_MS;
      bool haveStop;
      portENTER_CRITICAL(&liveMux);
      haveStop = stopId != 0;
      portEXIT_CRITICAL(&liveMux);
      if (needStops && (lastWasDeps || !haveStop)) {
        fetchStops();
        lastWasDeps = false;
      } else {
        fetchDepartures();
        lastWasDeps = true;
      }
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}

// ------------------------------------------------------------------ screens
uint16_t urgencyColor(long mins) {
  if (mins < cfgWalk) return C_RED;        // you'd have to run
  if (mins < cfgWalk + 3) return C_AMBER;  // time to head out
  return C_GREEN;
}

void drawMain(time_t now) {
  struct tm lt;
  localtime_r(&now, &lt);

  // Top section: rows 0-11, clock 10 px tall with 1 px above and below.
  char clk[8];
  snprintf(clk, sizeof clk, "%d:%02d", lt.tm_hour % 12 ? lt.tm_hour % 12 : 12, lt.tm_min);
  drawCentered(1, clk, C_CLOCK, 2);

  // Dividers
  for (int x = 0; x < PANEL_W; x++) px(x, 12, C_LINE);
  for (int y = 13; y < PANEL_H; y++) px(44, y, C_LINE);

  time_t dep[2], fetched;
  int n = getDepartures(now, dep, 2, &fetched);
  bool fresh = fetched && now - fetched <= LIVE_MAX_AGE_S;

  // Bottom left (44 x 19): big number + small label, centered as a group.
  char big[6];
  const char* unit;
  uint16_t color;
  if (!stopId) {
    strcpy(big, "NO"); unit = "STOP"; color = C_DIM;
  } else if (!fetched) {
    strcpy(big, "--"); unit = ""; color = C_DIM;          // waiting for the first response
  } else if (n == 0 && !fresh) {
    strcpy(big, "NO"); unit = "DATA"; color = C_DIM;      // offline and cached buses have left
  } else if (n == 0) {
    strcpy(big, "NO"); unit = "BUS"; color = C_DIM;
  } else {
    long m = (long)(dep[0] - now) / 60;
    if (m < 100) {
      snprintf(big, sizeof big, "%ld", m); unit = "MIN"; color = urgencyColor(m);
    } else {
      long h = (m + 30) / 60; if (h > 99) h = 99;
      snprintf(big, sizeof big, "%ld", h); unit = "HR"; color = C_GREEN;
    }
  }
  int gw = textWidth(big, 3) + 4 + textWidth(unit, 1);
  int gx = (44 - gw) / 2;
  drawText(gx, 15, big, color, 3);
  drawText(gx + textWidth(big, 3) + 4, 25, unit, C_LABEL, 1);

  // Bottom right (19 x 19): chevron + the bus after next.
  char nxt[6];
  uint16_t nextColor = C_NEXT, chevColor = fresh ? C_CHEVRON : C_CHEV_OLD;
  if (n >= 2) {
    long m2 = (long)(dep[1] - now) / 60;
    if (m2 < 100) snprintf(nxt, sizeof nxt, "%ldM", m2);
    else snprintf(nxt, sizeof nxt, "%ldH", min((m2 + 30) / 60, 99L));
  } else {
    strcpy(nxt, "--");
  }
  for (int r = 0; r < 5; r++)
    for (int c = 0; c < 7; c++)
      if (CHEVRON[r] & (0x40 >> c)) px(51 + c, 16 + r, chevColor);
  drawText(45 + (19 - textWidth(nxt, 1)) / 2, 24, nxt, nextColor, 1);
}

void drawSetupScreen() {
  drawCentered(2, "WIFI SETUP", C_AMBER, 1);
  drawCentered(10, AP_SSID, C_CLOCK, 1);
  drawCentered(18, "PW " AP_PASS, C_CLOCK, 1);
  drawCentered(26, WiFi.softAPIP().toString().c_str(), C_NEXT, 1);
}

void drawMessage(const char* a, const char* b) {
  drawCentered(b ? 9 : 13, a, C_CLOCK, 1);
  if (b) drawCentered(18, b, C_NEXT, 1);
}

bool timeValid() { return time(nullptr) > 1735689600; }  // after 2025-01-01

bool isNight(int hour) {
  if (cfgNightStart == cfgNightEnd) return false;
  if (cfgNightStart < cfgNightEnd) return hour >= cfgNightStart && hour < cfgNightEnd;
  return hour >= cfgNightStart || hour < cfgNightEnd;
}

void render() {
  bool connected = WiFi.status() == WL_CONNECTED;
  uint8_t bri = cfgBri;
  panel->clearScreen();

  if (apActive && (!connected || forceSetup)) {
    drawSetupScreen();
  } else if (!timeValid()) {
    drawMessage(connected ? "SETTING" : "WIFI", connected ? "CLOCK..." : "OFFLINE");
  } else {
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    if (isNight(lt.tm_hour)) bri = cfgNightBri;
    drawMain(now);
  }

  if (bri != appliedBri) { panel->setBrightness8(bri); appliedBri = bri; }
  panel->flipDMABuffer();
}

// ------------------------------------------------------------------ config
void loadConfig() {
  prefs.begin("busclock", true);
  cfgSsid = prefs.getString("ssid", "");
  cfgPass = prefs.getString("pass", "");
  cfgStop = prefs.getString("stop", DEFAULT_STOP);
  cfgWalk = prefs.getUChar("walk", 4);
  cfgBri = prefs.getUChar("bri", 128);
  cfgNightBri = prefs.getUChar("nbri", 20);
  cfgNightStart = prefs.getUChar("nstart", 23);
  cfgNightEnd = prefs.getUChar("nend", 7);
  cfgDriver = prefs.getUChar("driver", 0);
  prefs.end();
  if (!parseStopKey(cfgStop.c_str(), &stopId, &stopDir)) stopId = 0;
  else cfgStop = stopKey(stopId, stopDir);   // normalize old "34:1" keys to "34:S"
}

void saveConfig() {
  prefs.begin("busclock", false);
  prefs.putString("ssid", cfgSsid);
  prefs.putString("pass", cfgPass);
  prefs.putString("stop", cfgStop);
  prefs.putUChar("walk", cfgWalk);
  prefs.putUChar("bri", cfgBri);
  prefs.putUChar("nbri", cfgNightBri);
  prefs.putUChar("nstart", cfgNightStart);
  prefs.putUChar("nend", cfgNightEnd);
  prefs.putUChar("driver", cfgDriver);
  prefs.end();
}

// ------------------------------------------------------------------ web page
#include "web_page.h"

String jsonEscape(const String& s) {
  String o;
  o.reserve(s.length() + 8);
  for (size_t i = 0; i < s.length(); i++) {
    char c = s[i];
    if (c == '"' || c == '\\') { o += '\\'; o += c; }
    else if ((uint8_t)c < 0x20) { char b[8]; snprintf(b, sizeof b, "\\u%04x", c); o += b; }
    else o += c;
  }
  return o;
}

void handleRoot() { server.send_P(200, "text/html", INDEX_HTML); }

void handleConfig() {
  String j = "{\"route\":\"" ROUTE_NAME "\",\"ssid\":\"" + jsonEscape(cfgSsid) +
             "\",\"stop\":\"" + jsonEscape(cfgStop) + "\",\"stopLabel\":\"" +
             (stopId ? jsonEscape(stopLabel(stopId, stopDir)) : String("")) + "\",\"walk\":" + cfgWalk +
             ",\"bri\":" + cfgBri + ",\"nbri\":" + cfgNightBri + ",\"nstart\":" + cfgNightStart +
             ",\"nend\":" + cfgNightEnd + ",\"driver\":" + cfgDriver + ",\"stops\":[";
  if (xSemaphoreTake(stopsLock, pdMS_TO_TICKS(500))) {
    for (int i = 0; i < routeStopCount; i++) {
      const LiveStop& s = routeStops[i];
      if (i) j += ',';
      j += "{\"key\":\"" + stopKey(s.id, s.dir) + "\",\"name\":\"" + jsonEscape(s.name) +
           "\",\"toward\":\"" + jsonEscape(s.dir == 'N' ? towardN : towardS) + "\"}";
    }
    xSemaphoreGive(stopsLock);
  }
  j += "]}";
  server.send(200, "application/json", j);
}

void handleStatus() {
  bool connected = WiFi.status() == WL_CONNECTED;
  String j = "{\"connected\":" + String(connected ? "true" : "false");
  j += ",\"ssid\":\"" + jsonEscape(connected ? WiFi.SSID() : cfgSsid) + "\"";
  j += ",\"ip\":\"" + (connected ? WiFi.localIP().toString() : String("")) + "\"";
  j += ",\"stop\":\"";
  if (stopId) j += jsonEscape(stopLabel(stopId, stopDir));
  j += "\",\"time\":\"";
  String next = "";
  time_t fetched = 0;
  if (timeValid()) {
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    char b[32];
    strftime(b, sizeof b, "%a %b %d, %I:%M %p", &lt);
    j += b;
    time_t dep[3];
    int n = getDepartures(now, dep, 3, &fetched);
    for (int i = 0; i < n; i++) { if (i) next += ','; next += String((long)((dep[i] - now) / 60)); }
  }
  char err[48];
  portENTER_CRITICAL(&liveMux);
  memcpy(err, liveError, sizeof err);
  portEXIT_CRITICAL(&liveMux);
  j += "\",\"next\":[" + next + "]";
  j += ",\"liveAge\":" + String(fetched ? (long)(time(nullptr) - fetched) : -1L);
  j += ",\"maxAge\":" + String(LIVE_MAX_AGE_S);
  j += ",\"liveError\":\"" + jsonEscape(String(err)) + "\"";
  j += ",\"stopsLoaded\":" + String(stopsLoadedAt ? "true" : "false") + "}";
  server.send(200, "application/json", j);
}

void handleScan() {
  int n = WiFi.scanNetworks();
  // de-duplicate (mesh networks repeat names), keep the strongest, sort by signal
  String names[30];
  int rssi[30];
  bool open[30];
  int count = 0;
  for (int i = 0; i < n; i++) {
    String s = WiFi.SSID(i);
    if (s.length() == 0) continue;
    int k = 0;
    while (k < count && names[k] != s) k++;
    if (k < count) { if (WiFi.RSSI(i) > rssi[k]) rssi[k] = WiFi.RSSI(i); continue; }
    if (count >= 30) continue;
    names[count] = s; rssi[count] = WiFi.RSSI(i); open[count] = WiFi.encryptionType(i) == WIFI_AUTH_OPEN; count++;
  }
  WiFi.scanDelete();
  for (int a = 0; a < count; a++)
    for (int b = a + 1; b < count; b++)
      if (rssi[b] > rssi[a]) { std::swap(names[a], names[b]); std::swap(rssi[a], rssi[b]); std::swap(open[a], open[b]); }
  String j = "[";
  for (int i = 0; i < count; i++) {
    if (i) j += ',';
    j += "{\"ssid\":\"" + jsonEscape(names[i]) + "\",\"rssi\":" + rssi[i] + ",\"open\":" + (open[i] ? "true" : "false") + "}";
  }
  j += "]";
  server.send(200, "application/json", j);
}

uint8_t argByte(const char* name, uint8_t fallback, int lo, int hi) {
  if (!server.hasArg(name)) return fallback;
  long v = server.arg(name).toInt();
  return (uint8_t)constrain(v, lo, hi);
}

void handleSave() {
  String newSsid = server.arg("ssid");
  newSsid.trim();
  String newPass = server.arg("pass");
  bool wifiChanged = false;
  if (newSsid.length() && newSsid != cfgSsid) {
    cfgSsid = newSsid; cfgPass = newPass; wifiChanged = true;   // new network: blank = open network
  } else if (newPass.length()) {
    cfgPass = newPass; wifiChanged = true;                      // same network, new password
  }

  uint32_t newId = 0;
  char newDir = 'S';
  String newStop = server.arg("stop");
  if (newStop.length() == 0) cfgStop = "";
  else if (parseStopKey(newStop.c_str(), &newId, &newDir)) cfgStop = stopKey(newId, newDir);
  else newId = stopId, newDir = stopDir;   // malformed: keep the current stop
  if (cfgStop.length() == 0) newId = 0;
  portENTER_CRITICAL(&liveMux);
  if (newId != stopId || newDir != stopDir) {
    liveCount = 0; liveFetchedAt = 0; liveError[0] = 0;   // old stop's data no longer applies
  }
  stopId = newId;
  stopDir = newDir;
  portEXIT_CRITICAL(&liveMux);

  cfgWalk = argByte("walk", cfgWalk, 0, 30);
  cfgBri = argByte("bri", cfgBri, 1, 255);
  cfgNightBri = argByte("nbri", cfgNightBri, 1, 255);
  cfgNightStart = argByte("nstart", cfgNightStart, 0, 23);
  cfgNightEnd = argByte("nend", cfgNightEnd, 0, 23);
  uint8_t newDriver = argByte("driver", cfgDriver, 0, 2);
  bool driverChanged = newDriver != cfgDriver;
  cfgDriver = newDriver;

  saveConfig();
  appliedBri = 0;  // re-apply brightness on the next frame

  if (wifiChanged || driverChanged) {
    String msg = "Saved. The clock is restarting with the new panel driver.";
    if (wifiChanged)
      msg = "Saved. The clock is restarting and joining " + cfgSsid +
            ". From that network you can reopen this page at http://" HOSTNAME ".local";
    server.send(200, "text/plain", msg);
    restartAt = millis() + 1500;
  } else {
    server.send(200, "text/plain", "Saved.");
  }
}

void handleForget() {
  cfgSsid = ""; cfgPass = "";
  saveConfig();
  server.send(200, "text/plain", "OK");
  restartAt = millis() + 1000;
}

// Captive portal: send every unknown request (phone connectivity checks too) to the setup page.
void handleNotFound() {
  if (apActive && server.client().localIP() == WiFi.softAPIP()) {
    server.sendHeader("Location", "http://" + WiFi.softAPIP().toString() + "/", true);
    server.send(302, "text/plain", "");
  } else {
    server.send(404, "text/plain", "Not found");
  }
}

// ------------------------------------------------------------------ wifi
void startAP() {
  if (apActive) return;
  WiFi.mode(WIFI_AP_STA);
  WiFi.softAP(AP_SSID, AP_PASS);
  delay(100);
  dns.setErrorReplyCode(DNSReplyCode::NoError);
  dns.start(53, "*", WiFi.softAPIP());
  apActive = true;
  apStartedAt = millis();
  Serial.printf("Setup network \"%s\" up at %s\n", AP_SSID, WiFi.softAPIP().toString().c_str());
}

void stopAP() {
  if (!apActive) return;
  dns.stop();
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_STA);
  apActive = false;
  forceSetup = false;
  Serial.println("Setup network closed");
}

void onConnected() {
  Serial.printf("Connected to %s, IP %s\n", WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
  if (!mdnsStarted && MDNS.begin(HOSTNAME)) {
    MDNS.addService("http", "tcp", 80);
    mdnsStarted = true;
  }
}

void manageWifi() {
  bool connected = WiFi.status() == WL_CONNECTED;
  uint32_t ms = millis();
  if (connected && !wasConnected) { onConnected(); staConnectedAt = ms; }
  if (!connected && wasConnected) disconnectedSince = ms;
  wasConnected = connected;

  if (apActive) {
    // Joined the home network and nobody is using the setup network: close it
    // (after 1 minute, or 10 minutes if it was opened with the BOOT button).
    uint32_t grace = forceSetup ? 600000UL : 60000UL;
    if (connected && ms - staConnectedAt > 60000 && ms - apStartedAt > grace && WiFi.softAPgetStationNum() == 0) stopAP();
    // Keep retrying the home network every minute while no phone is connected to setup.
    if (!connected && cfgSsid.length() && WiFi.softAPgetStationNum() == 0 && ms - lastStaTry > 60000) {
      lastStaTry = ms;
      WiFi.begin(cfgSsid.c_str(), cfgPass.c_str());
    }
  } else if (!connected && ms - disconnectedSince > 10UL * 60 * 1000) {
    // Offline for 10 minutes (router changed?): open setup but keep the clock running.
    startAP();
  }

  // BOOT button held for 3 s: open the setup network on demand.
  if (digitalRead(BOOT_BUTTON) == LOW) {
    if (!bootPressedAt) bootPressedAt = ms ? ms : 1;
    else if (ms - bootPressedAt > 3000 && !forceSetup) { forceSetup = true; startAP(); }
  } else {
    bootPressedAt = 0;
  }
}

// ------------------------------------------------------------------ setup / loop
void setupPanel() {
  HUB75_I2S_CFG::i2s_pins pins = {PIN_R1, PIN_G1, PIN_B1, PIN_R2, PIN_G2, PIN_B2,
                                  PIN_A, PIN_B, PIN_C, PIN_D, PIN_E, PIN_LAT, PIN_OE, PIN_CLK};
  HUB75_I2S_CFG mxconfig(PANEL_W, PANEL_H, 1, pins);
  mxconfig.double_buff = true;  // draw off-screen, then flip: no flicker
  if (cfgDriver == 1) mxconfig.driver = HUB75_I2S_CFG::FM6126A;
  else if (cfgDriver == 2) mxconfig.driver = HUB75_I2S_CFG::ICN2038S;
  panel = new MatrixPanel_I2S_DMA(mxconfig);
  panel->begin();
  panel->setBrightness8(cfgBri);
  appliedBri = cfgBri;
  panel->clearScreen();
}

void setup() {
  Serial.begin(115200);
  stopsLock = xSemaphoreCreateMutex();
  pinMode(BOOT_BUTTON, INPUT_PULLUP);
  loadConfig();
  setupPanel();

  configTzTime(TZ_INFO, "pool.ntp.org", "time.nist.gov", "time.google.com");

  server.on("/", HTTP_GET, handleRoot);
  server.on("/api/config", HTTP_GET, handleConfig);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/forget", HTTP_POST, handleForget);
  server.onNotFound(handleNotFound);

  if (cfgSsid.length() == 0) {
    startAP();
  } else {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.setAutoReconnect(true);
    WiFi.begin(cfgSsid.c_str(), cfgPass.c_str());
    uint32_t t0 = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - t0 < 20000) {
      panel->clearScreen();
      drawMessage("CONNECTING", "WIFI...");
      panel->flipDMABuffer();
      delay(250);
    }
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Couldn't join the saved network; opening setup");
      startAP();
      lastStaTry = millis();
    }
  }
  server.begin();
  xTaskCreatePinnedToCore(liveTask, "live", 16384, nullptr, 1, nullptr, 0);
}

void loop() {
  if (apActive) dns.processNextRequest();
  server.handleClient();
  manageWifi();

  if (restartAt && (int32_t)(millis() - restartAt) > 0) ESP.restart();

  static uint32_t lastDraw = 0;
  if (millis() - lastDraw >= 500) {
    lastDraw = millis();
    render();
  }
  delay(2);
}
