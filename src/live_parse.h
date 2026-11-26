// live_parse.h - read PVTA InfoPoint (bus tracker) responses.
// Plain C++ plus ArduinoJson (no Arduino calls) so it can be tested on a computer.
//
// Departures: https://bustracker.pvta.com/InfoPoint/rest/StopDepartures/Get/<stopId>
//   [ { "RouteDirections": [ { "RouteId": 30030, "DirectionCode": "S", "Departures": [
//       { "EDT": "/Date(1795560600000-0500)/", "SDT": "...", "IsCompleted": false,
//         "IsLastStopOnTrip": false, ... }, ... ] }, ... ] } ]
//   EDT = estimated departure time (live), SDT = scheduled. The number inside
//   /Date(...)/ is milliseconds since 1970 UTC; the -0500 suffix is just the zone.
//
// Route: https://bustracker.pvta.com/InfoPoint/rest/RouteDetails/Get/<routeId>
//   { "ShortName": "30", "Color": "FBB033", "Stops": [...], ... }
// (The setup page loads route and stop lists itself, straight from the browser.)
#pragma once
#include <ArduinoJson.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "logic.h"

// ------------------------------------------------------------------ departures
// Keep only the fields we use, so the ~10 KB response parses into a small document.
static inline void live_buildFilter(JsonDocument& filter) {
  filter[0]["RouteDirections"][0]["RouteId"] = true;
  filter[0]["RouteDirections"][0]["DirectionCode"] = true;
  JsonObject d = filter[0]["RouteDirections"][0]["Departures"][0].to<JsonObject>();
  d["EDT"] = true;
  d["SDT"] = true;
  d["IsCompleted"] = true;
  d["IsLastStopOnTrip"] = true;
}

// "/Date(1795560600000-0500)/" -> 1795560600. Returns 0 if malformed.
static inline time_t live_parseDate(const char* s) {
  if (!s) return 0;
  const char* p = strstr(s, "Date(");
  if (!p) return 0;
  long long ms = strtoll(p + 5, nullptr, 10);
  return ms > 0 ? (time_t)(ms / 1000) : 0;
}

// Pull the upcoming departures for one route direction ("S", "E", "UP", ...) out
// of a parsed response. Fills out[] sorted ascending; returns how many.
static inline int live_extract(JsonDocument& doc, long routeId, const char* dir, time_t now,
                               time_t* out, int maxOut) {
  int n = 0;
  for (JsonObject stop : doc.as<JsonArray>()) {
    for (JsonObject rd : stop["RouteDirections"].as<JsonArray>()) {
      if (rd["RouteId"].as<long>() != routeId) continue;
      const char* dc = rd["DirectionCode"] | "";
      if (!dirMatches(dc, dir)) continue;
      for (JsonObject d : rd["Departures"].as<JsonArray>()) {
        if (d["IsCompleted"] | false) continue;
        if (d["IsLastStopOnTrip"] | false) continue;   // bus ends its run here
        time_t t = live_parseDate(d["EDT"] | (const char*)nullptr);
        if (!t) t = live_parseDate(d["SDT"] | (const char*)nullptr);
        if (!t || t < now) continue;
        bool dup = false;
        for (int k = 0; k < n; k++) if (out[k] == t) dup = true;
        if (dup) continue;
        if (n == maxOut && t >= out[n - 1]) continue;
        int j = n < maxOut ? n : maxOut - 1;
        while (j > 0 && out[j - 1] > t) { out[j] = out[j - 1]; j--; }
        out[j] = t;
        if (n < maxOut) n++;
      }
    }
  }
  return n;
}

// ------------------------------------------------------------------ route
// The badge shown next to the clock: PVTA's short name and colors for a route.
#define LIVE_ROUTE_NAME_LEN 8

struct LiveRoute {
  char name[LIVE_ROUTE_NAME_LEN];  // "30", "B43", "G101"
  char color[7];                   // badge color, hex without '#': "FBB033"
};

static inline void live_buildRouteFilter(JsonDocument& filter) {
  filter["ShortName"] = true;
  filter["Color"] = true;
}

static inline void live_copy(char* dst, const char* src, size_t n) {
  strncpy(dst, src ? src : "", n - 1);
  dst[n - 1] = 0;
}

// Read a RouteDetails/Get/<routeId> response. False if it has no short name.
static inline bool live_extractRoute(JsonDocument& doc, LiveRoute* out) {
  const char* name = doc["ShortName"] | "";
  if (!name[0]) return false;
  live_copy(out->name, name, sizeof out->name);
  live_copy(out->color, doc["Color"] | "", sizeof out->color);
  return true;
}
