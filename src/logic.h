// logic.h - small pure helpers (no Arduino calls) so they can be unit-tested.
#pragma once
#include <stdint.h>
#include <stdlib.h>

// "34:S" -> id 34, dir 'S'. Older saves used GTFS direction ids (0 = N, 1 = S).
static inline bool parseStopKey(const char* key, uint32_t* id, char* dir) {
  if (!key) return false;
  char* end;
  unsigned long v = strtoul(key, &end, 10);
  if (end == key || *end != ':' || v == 0 || v > 0xFFFFFFFFUL) return false;
  char d = end[1];
  if (d == '0') d = 'N';
  else if (d == '1') d = 'S';
  if ((d != 'N' && d != 'S') || end[2] != 0) return false;
  *id = (uint32_t)v;
  *dir = d;
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
