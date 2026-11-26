// logic.h - small pure helpers (no Arduino calls) so they can be unit-tested.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// Direction codes are short uppercase strings from the bus tracker: N, S, E, W,
// UP, DWN, CW, CCW... DIR_LEN holds up to 4 characters plus the terminator.
#define DIR_LEN 5

// "34:S" -> id 34, dir "S"; "40:UP" -> id 40, dir "UP". Older saves used GTFS
// direction ids, so "34:0" / "34:1" read as N / S. dir must hold DIR_LEN chars.
static inline bool parseStopKey(const char* key, uint32_t* id, char* dir) {
  if (!key) return false;
  char* end;
  unsigned long v = strtoul(key, &end, 10);
  if (end == key || *end != ':' || v == 0 || v > 0xFFFFFFFFUL) return false;
  const char* d = end + 1;
  char out[DIR_LEN];
  if (d[0] == '0' && d[1] == 0) { out[0] = 'N'; out[1] = 0; }
  else if (d[0] == '1' && d[1] == 0) { out[0] = 'S'; out[1] = 0; }
  else {
    int n = 0;
    for (; d[n]; n++) {
      if (n >= DIR_LEN - 1 || d[n] < 'A' || d[n] > 'Z') return false;
      out[n] = d[n];
    }
    if (n == 0) return false;
    out[n] = 0;
  }
  *id = (uint32_t)v;
  memcpy(dir, out, DIR_LEN);
  return true;
}

// Does a departure's DirectionCode belong to the stop's direction? Exact match,
// or a one-letter code standing for a longer one ("U" for "UP").
static inline bool dirMatches(const char* code, const char* dir) {
  if (!code || !dir || !code[0] || !dir[0]) return false;
  if (strcmp(code, dir) == 0) return true;
  return code[1] == 0 && code[0] == dir[0];
}

// "FBB033" or "#fbb033" -> 0xFBB033. Returns false unless it's exactly 6 hex digits.
static inline bool parseHexColor(const char* s, uint32_t* rgb) {
  if (!s) return false;
  if (*s == '#') s++;
  uint32_t v = 0;
  for (int i = 0; i < 6; i++) {
    char c = s[i];
    int d;
    if (c >= '0' && c <= '9') d = c - '0';
    else if (c >= 'a' && c <= 'f') d = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F') d = c - 'A' + 10;
    else return false;
    v = v << 4 | (uint32_t)d;
  }
  if (s[6] != 0) return false;
  *rgb = v;
  return true;
}

// Is `hour` inside the night window [start, end)? The window may wrap midnight.
// start == end means no night mode.
static inline bool isNight(int hour, int start, int end) {
  if (start == end) return false;
  if (start < end) return hour >= start && hour < end;
  return hour >= start || hour < end;
}

// ------------------------------------------------------------------ which screen
enum ScreenKind { SCREEN_MAIN, SCREEN_SETUP, SCREEN_SETTING_CLOCK, SCREEN_WIFI_OFFLINE };

// While Wi-Fi is down the panel alternates: countdown for MAIN_MS, then
// "WIFI OFFLINE" for OFFLINE_MS. It waits GRACE_MS first so short blips don't flash.
#define OFFLINE_FLASH_MAIN_MS    4000UL
#define OFFLINE_FLASH_OFFLINE_MS 2000UL
#define OFFLINE_FLASH_GRACE_MS   10000UL

struct ScreenInput {
  bool apActive;         // the setup network is up
  bool forceSetup;       // opened with the BOOT button
  bool hasSavedWifi;     // a home network is configured
  bool setupClient;      // a phone is connected to the setup network
  bool connected;        // joined the home network
  bool timeValid;        // the clock has been set
  uint32_t offlineMs;    // how long the home network has been down (0 if connected)
  uint32_t ms;           // millis(), for the flash phase
};

static inline ScreenKind chooseScreen(const ScreenInput& s) {
  // Setup screen only when someone needs it: nothing saved yet, BOOT held,
  // or a phone has joined the setup network.
  if (s.apActive && (!s.hasSavedWifi || s.forceSetup || s.setupClient)) return SCREEN_SETUP;
  if (!s.timeValid) return s.connected ? SCREEN_SETTING_CLOCK : SCREEN_WIFI_OFFLINE;
  if (!s.connected && s.offlineMs >= OFFLINE_FLASH_GRACE_MS &&
      s.ms % (OFFLINE_FLASH_MAIN_MS + OFFLINE_FLASH_OFFLINE_MS) >= OFFLINE_FLASH_MAIN_MS)
    return SCREEN_WIFI_OFFLINE;
  return SCREEN_MAIN;
}
