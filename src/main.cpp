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

  First boot: the panel shows JOIN WIFI / BUSCLOCK. On your phone, join the Wi-Fi network
  "BusClock" (no password). A setup page opens (or browse to
  http://192.168.4.1). Pick your home Wi-Fi and your stop, then Save.
  Afterwards the same page is at http://busclock.local on your home network.
  Hold the BOOT button for 3 seconds at any time to bring the setup network back.

  Everything comes from PVTA's bus tracker (InfoPoint): departures every 30
  seconds, and the route's stop list once a day. Nothing is stored on the
  ESP32, so schedule changes show up on their own. If the tracker can't be
  reached, the clock keeps counting down to the departures it last received
  and dims the >> icon after 3 minutes so you can tell.
*/

#include <Arduino.h>
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
#include "logic.h"
#include "display.h"

// ------------------------------------------------------------------ settings
#define AP_SSID      "BusClock"
#define HOSTNAME     "busclock"        // http://busclock.local
#define TZ_INFO      "EST5EDT,M3.2.0,M11.1.0"  // America/New_York
#define BOOT_BUTTON  0                 // hold 3 s to open the setup network

// Defaults until a route and stop are chosen on the setup page: Route 30 at
// Puffton Village Apts, southbound. Route ids are InfoPoint's (30030), not GTFS's.
#define DEFAULT_ROUTE_ID    30030
#define DEFAULT_ROUTE_NAME  "30"
#define DEFAULT_ROUTE_COLOR 0xFBB033   // PVTA's orange for Route 30
#define DEFAULT_STOP        "34:S"     // stop keys are "<StopId>:<N|S>"
#define DEFAULT_STOP_NAME   "Puffton Village Apts toward Old Belchertown Road"

// PVTA InfoPoint (bus tracker)
#define LIVE_BASE        "https://bustracker.pvta.com/InfoPoint/rest/"
#define LIVE_INTERVAL_MS 30000UL       // minimum time between requests to PVTA
static_assert(LIVE_INTERVAL_MS >= 30000UL, "Poll PVTA at most every 30 seconds");
#define LIVE_MAX_AGE_S   180           // after this long without fresh data, dim the >> icon
#define ROUTE_REFRESH_MS (24UL * 3600 * 1000)  // re-check the route's name and colors daily

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

// ------------------------------------------------------------------ state
MatrixPanel_I2S_DMA* panel = nullptr;
WebServer server(80);
DNSServer dns;
Preferences prefs;

String cfgSsid, cfgPass, cfgStop, cfgStopName, cfgRouteName;
uint32_t cfgRoute = DEFAULT_ROUTE_ID, cfgRouteColor = DEFAULT_ROUTE_COLOR;
uint8_t cfgWalk = 4, cfgBri = 128, cfgNightBri = 20, cfgNightStart = 23, cfgNightEnd = 7, cfgDriver = 0;

// The chosen route and stop, parsed from the config (guarded by liveMux).
uint32_t routeId = DEFAULT_ROUTE_ID;
uint32_t stopId = 0;   // 0 = none chosen
char stopDir[DIR_LEN] = "S";

// Route badge as drawn (guarded by liveMux). Starts from the saved config and
// is refreshed from PVTA daily in case the route's colors change.
char badgeName[LIVE_ROUTE_NAME_LEN] = DEFAULT_ROUTE_NAME;
uint16_t badgeColor = 0;
uint32_t routeLoadedAt = 0;        // millis() of the last successful route refresh, 0 = never
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

String stopKey(uint32_t id, const char* dir) { return String(id) + ':' + dir; }

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
String stopLabel() {
  if (!stopId) return "";
  if (cfgStopName.length()) return cfgStopName;
  return "Stop " + String(stopId) + " (" + stopDir + ")";
}

// Copy the saved route name and colors into the badge.
void applyRouteBadge() {
  portENTER_CRITICAL(&liveMux);
  strncpy(badgeName, cfgRouteName.c_str(), sizeof badgeName - 1);
  badgeName[sizeof badgeName - 1] = 0;
  badgeColor = rgb565(cfgRouteColor);
  portEXIT_CRITICAL(&liveMux);
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
  uint32_t id, route;
  char dir[DIR_LEN];
  portENTER_CRITICAL(&liveMux);
  id = stopId; memcpy(dir, stopDir, DIR_LEN); route = routeId;
  portEXIT_CRITICAL(&liveMux);
  if (!id) return;

  JsonDocument filter, doc;
  live_buildFilter(filter);
  if (!fetchJson("StopDepartures/Get/" + String(id), filter, doc)) return;

  time_t now = time(nullptr);
  time_t found[8];
  int n = live_extract(doc, route, dir, now, found, 8);
  portENTER_CRITICAL(&liveMux);
  if (id == stopId && strcmp(dir, stopDir) == 0 && route == routeId) {   // ignore if the choice changed meanwhile
    memcpy(liveDeps, found, sizeof(time_t) * n);
    liveCount = n;
    liveFetchedAt = now;
    liveError[0] = 0;
  }
  portEXIT_CRITICAL(&liveMux);
}

// Refresh the badge's name and colors from PVTA (they rarely change).
void fetchRoute() {
  uint32_t route;
  portENTER_CRITICAL(&liveMux);
  route = routeId;
  portEXIT_CRITICAL(&liveMux);

  JsonDocument filter, doc;
  live_buildRouteFilter(filter);
  if (!fetchJson("RouteDetails/Get/" + String(route), filter, doc)) return;
  LiveRoute r;
  if (!live_extractRoute(doc, &r)) { setLiveError("unknown route"); return; }
  uint32_t bg;
  bool okBg = parseHexColor(r.color, &bg);
  portENTER_CRITICAL(&liveMux);
  if (route == routeId) {
    memcpy(badgeName, r.name, sizeof badgeName);
    if (okBg) badgeColor = rgb565(bg);
    routeLoadedAt = millis() | 1;
  }
  portEXIT_CRITICAL(&liveMux);
  Serial.printf("Route %s: color %s\n", r.name, r.color);
}

// Runs on its own so a slow HTTPS request never freezes the clock or setup page.
// This is the only place that contacts PVTA, and it makes at most one request
// per LIVE_INTERVAL_MS (30 s), counting failed attempts. When the route badge
// needs refreshing it takes every other slot, so departures stay current.
void liveTask(void*) {
  uint32_t last = 0;
  bool attempted = false, lastWasDeps = false;
  for (;;) {
    bool allowed = !attempted || millis() - last >= LIVE_INTERVAL_MS;
    if (allowed && WiFi.status() == WL_CONNECTED && timeValid()) {
      attempted = true;
      last = millis();
      bool needRoute = !routeLoadedAt || millis() - routeLoadedAt > ROUTE_REFRESH_MS;
      bool haveStop;
      portENTER_CRITICAL(&liveMux);
      haveStop = stopId != 0;
      portEXIT_CRITICAL(&liveMux);
      if (needRoute && (lastWasDeps || !haveStop)) {
        fetchRoute();
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
// Drawing lives in display.h; this connects it to the real panel.
struct PanelCanvas : Canvas {
  void set(int x, int y, uint16_t c) override { panel->drawPixel(x, y, c); }
} panelCanvas;

void drawMainNow(time_t now) {
  struct tm lt;
  localtime_r(&now, &lt);
  time_t dep[2], fetched;
  int n = getDepartures(now, dep, 2, &fetched);
  char route[LIVE_ROUTE_NAME_LEN];
  uint16_t bg;
  portENTER_CRITICAL(&liveMux);
  memcpy(route, badgeName, sizeof route);
  bg = badgeColor;
  portEXIT_CRITICAL(&liveMux);
  MainInput in = {lt.tm_hour, lt.tm_min, route, bg, now, dep, n, fetched, stopId != 0, cfgWalk, LIVE_MAX_AGE_S};
  drawMain(panelCanvas, mainView(in));
}

bool timeValid() { return time(nullptr) > 1735689600; }  // after 2025-01-01

void render() {
  bool connected = WiFi.status() == WL_CONNECTED;
  uint32_t ms = millis();
  ScreenInput si = {apActive, forceSetup, cfgSsid.length() > 0, apActive && WiFi.softAPgetStationNum() > 0,
                    connected, timeValid(), connected ? 0 : ms - disconnectedSince, ms};
  uint8_t bri = cfgBri;
  if (si.timeValid) {
    time_t now = time(nullptr);
    struct tm lt;
    localtime_r(&now, &lt);
    if (isNight(lt.tm_hour, cfgNightStart, cfgNightEnd)) bri = cfgNightBri;
  }

  panel->clearScreen();
  switch (chooseScreen(si)) {
    case SCREEN_SETUP:         drawSetupScreen(panelCanvas, AP_SSID, WiFi.softAPIP().toString().c_str()); break;
    case SCREEN_SETTING_CLOCK: drawMessage(panelCanvas, "SETTING", "CLOCK..."); break;
    case SCREEN_WIFI_OFFLINE:  drawMessage(panelCanvas, "WIFI", "OFFLINE"); break;
    case SCREEN_MAIN:          drawMainNow(time(nullptr)); break;
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
  cfgStopName = prefs.getString("stopname", cfgStop == DEFAULT_STOP ? DEFAULT_STOP_NAME : "");
  cfgRoute = prefs.getUInt("route", DEFAULT_ROUTE_ID);
  cfgRouteName = prefs.getString("rname", DEFAULT_ROUTE_NAME);
  cfgRouteColor = prefs.getUInt("rcolor", DEFAULT_ROUTE_COLOR);
  cfgWalk = prefs.getUChar("walk", 4);
  cfgBri = prefs.getUChar("bri", 128);
  cfgNightBri = prefs.getUChar("nbri", 20);
  cfgNightStart = prefs.getUChar("nstart", 23);
  cfgNightEnd = prefs.getUChar("nend", 7);
  cfgDriver = prefs.getUChar("driver", 0);
  prefs.end();
  if (!parseStopKey(cfgStop.c_str(), &stopId, stopDir)) stopId = 0;
  else cfgStop = stopKey(stopId, stopDir);   // normalize old "34:1" keys to "34:S"
  routeId = cfgRoute;
  applyRouteBadge();
}

void saveConfig() {
  prefs.begin("busclock", false);
  prefs.putString("ssid", cfgSsid);
  prefs.putString("pass", cfgPass);
  prefs.putString("stop", cfgStop);
  prefs.putString("stopname", cfgStopName);
  prefs.putUInt("route", cfgRoute);
  prefs.putString("rname", cfgRouteName);
  prefs.putUInt("rcolor", cfgRouteColor);
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

String hex6(uint32_t rgb) {
  char b[8];
  snprintf(b, sizeof b, "%06lX", (unsigned long)(rgb & 0xFFFFFF));
  return String(b);
}

void handleConfig() {
  String j = "{\"ssid\":\"" + jsonEscape(cfgSsid) + "\"";
  j += ",\"route\":" + String(cfgRoute) + ",\"routeName\":\"" + jsonEscape(cfgRouteName) + "\"";
  j += ",\"routeColor\":\"" + hex6(cfgRouteColor) + "\"";
  j += ",\"stop\":\"" + jsonEscape(cfgStop) + "\",\"stopName\":\"" + jsonEscape(stopLabel()) + "\"";
  j += ",\"walk\":" + String(cfgWalk) + ",\"bri\":" + cfgBri + ",\"nbri\":" + cfgNightBri;
  j += ",\"nstart\":" + String(cfgNightStart) + ",\"nend\":" + cfgNightEnd + ",\"driver\":" + cfgDriver + "}";
  server.send(200, "application/json", j);
}

void handleStatus() {
  bool connected = WiFi.status() == WL_CONNECTED;
  String j = "{\"connected\":" + String(connected ? "true" : "false");
  j += ",\"ssid\":\"" + jsonEscape(connected ? WiFi.SSID() : cfgSsid) + "\"";
  j += ",\"ip\":\"" + (connected ? WiFi.localIP().toString() : String("")) + "\"";
  j += ",\"stop\":\"";
  j += jsonEscape(stopLabel());
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
  j += ",\"route\":\"" + jsonEscape(cfgRouteName) + "\"}";
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

  // Route: the page sends PVTA's id, short name and colors together. Ignore it
  // unless all of them are valid (e.g. the page couldn't reach PVTA).
  uint32_t newRoute = cfgRoute, rgbBg;
  String rname = server.arg("rname");
  long r = server.arg("route").toInt();
  if (r > 0 && rname.length() > 0 && rname.length() < LIVE_ROUTE_NAME_LEN &&
      parseHexColor(server.arg("rcolor").c_str(), &rgbBg)) {
    newRoute = (uint32_t)r;
    cfgRouteName = rname; cfgRouteColor = rgbBg;
  }

  uint32_t newId = 0;
  char newDir[DIR_LEN] = "S";
  String newStop = server.arg("stop");
  if (newStop.length() == 0) { cfgStop = ""; cfgStopName = ""; }
  else if (parseStopKey(newStop.c_str(), &newId, newDir)) {
    cfgStop = stopKey(newId, newDir);
    if (server.hasArg("stopname")) cfgStopName = server.arg("stopname").substring(0, 100);
  }
  else { newId = stopId; memcpy(newDir, stopDir, DIR_LEN); }   // malformed: keep the current stop
  if (cfgStop.length() == 0) newId = 0;

  bool routeChanged = newRoute != cfgRoute;
  cfgRoute = newRoute;
  portENTER_CRITICAL(&liveMux);
  if (newId != stopId || strcmp(newDir, stopDir) != 0 || routeChanged) {
    liveCount = 0; liveFetchedAt = 0; liveError[0] = 0;   // old data no longer applies
  }
  stopId = newId;
  memcpy(stopDir, newDir, DIR_LEN);
  routeId = newRoute;
  if (routeChanged) routeLoadedAt = 0;   // refresh the badge from PVTA soon
  portEXIT_CRITICAL(&liveMux);
  applyRouteBadge();

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
  WiFi.softAP(AP_SSID);   // open network: no password to type during setup
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
      drawMessage(panelCanvas, "JOINING", "WIFI...");
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
