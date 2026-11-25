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
