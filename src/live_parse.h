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
// Stops: https://bustracker.pvta.com/InfoPoint/rest/RouteDetails/Get/<routeId>
//   { "ShortName": "30", "Stops": [ { "StopId": 34, "Name": "Puffton Village Apts" }, ... ],
//     "RouteStops": [ { "Direction": "S", "StopId": 34, "SortOrder": 1 }, ... ] }
#pragma once
#include <ArduinoJson.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

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

// Pull the upcoming departures for one route direction ('N' or 'S') out of a
// parsed response. Fills out[] sorted ascending; returns how many.
static inline int live_extract(JsonDocument& doc, long routeId, char dirCode, time_t now,
                               time_t* out, int maxOut) {
  int n = 0;
  for (JsonObject stop : doc.as<JsonArray>()) {
    for (JsonObject rd : stop["RouteDirections"].as<JsonArray>()) {
      if (rd["RouteId"].as<long>() != routeId) continue;
      const char* dc = rd["DirectionCode"] | "";
      if (dc[0] != dirCode) continue;
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

// ------------------------------------------------------------------ stops
#define LIVE_NAME_LEN 41

struct LiveStop {
  uint32_t id;
  char dir;                  // 'N' or 'S'
  uint16_t order;            // position along the route
  char name[LIVE_NAME_LEN];
};

static inline void live_buildStopsFilter(JsonDocument& filter) {
  filter["Stops"][0]["StopId"] = true;
  filter["Stops"][0]["Name"] = true;
  filter["RouteStops"][0]["Direction"] = true;
  filter["RouteStops"][0]["StopId"] = true;
  filter["RouteStops"][0]["SortOrder"] = true;
}

// Fill out[] with every stop/direction on the route, in route order. The last
// stop of each direction is left out (buses end their trip there). Returns how many.
static inline int live_extractStops(JsonDocument& doc, LiveStop* out, int maxOut) {
  JsonArray stops = doc["Stops"].as<JsonArray>();
  int n = 0;
  for (JsonObject rs : doc["RouteStops"].as<JsonArray>()) {
    if (n >= maxOut) break;
    const char* d = rs["Direction"] | "";
    if (d[0] != 'N' && d[0] != 'S') continue;
    LiveStop s;
    s.id = rs["StopId"].as<uint32_t>();
    s.dir = d[0];
    s.order = rs["SortOrder"].as<uint16_t>();
    s.name[0] = 0;
    for (JsonObject st : stops) {
      if (st["StopId"].as<uint32_t>() != s.id) continue;
      strncpy(s.name, st["Name"] | "", LIVE_NAME_LEN - 1);
      s.name[LIVE_NAME_LEN - 1] = 0;
      break;
    }
    if (!s.id || !s.name[0]) continue;
    int j = n++;
    while (j > 0 && out[j - 1].order > s.order) { out[j] = out[j - 1]; j--; }
    out[j] = s;
  }
  // drop the final stop of each direction
  for (char dir : {'N', 'S'}) {
    int last = -1;
    for (int i = 0; i < n; i++) if (out[i].dir == dir) last = i;
    if (last < 0) continue;
    for (int i = last; i < n - 1; i++) out[i] = out[i + 1];
    n--;
  }
  return n;
}

// Name of the last stop in a direction ("toward ..."), or "" if unknown.
static inline const char* live_towardName(JsonDocument& doc, char dir) {
  JsonArray stops = doc["Stops"].as<JsonArray>();
  uint32_t lastId = 0;
  uint16_t best = 0;
  for (JsonObject rs : doc["RouteStops"].as<JsonArray>()) {
    const char* d = rs["Direction"] | "";
    uint16_t o = rs["SortOrder"].as<uint16_t>();
    if (d[0] == dir && o >= best) { best = o; lastId = rs["StopId"].as<uint32_t>(); }
  }
  for (JsonObject st : stops)
    if (st["StopId"].as<uint32_t>() == lastId) return st["Name"] | "";
  return "";
}
