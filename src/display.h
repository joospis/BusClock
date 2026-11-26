// display.h - everything drawn on the 64x32 panel. Plain C++ (no Arduino calls)
// so it can be unit-tested and compared against the browser simulator
// (sim/panel.js is a line-for-line port; tools/parity checks they match).
#pragma once
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#define PANEL_W 64
#define PANEL_H 32
#define BADGE_H   11   // route badge: rows 0-10, above the divider on row 12
#define BADGE_PAD  3   // badge color on each side of the route number
#define BADGE_GAP  3   // space between the badge and the widest clock
#define CLOCK_MAX_W 38 // "12:59" at double size
#define BADGE_DARK 0x40 // badge colors whose brightest channel is below this can't show a cut-out

// Colors (from the mockup). LEDs look brighter than a screen; tweak to taste.
#define RGB565(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
static inline uint16_t rgb565(uint32_t rgb) { return RGB565((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF); }
static const uint16_t C_CLOCK    = RGB565(0xDD, 0xDD, 0xDD);
static const uint16_t C_LABEL    = RGB565(0xDD, 0xDD, 0xDD);
static const uint16_t C_LINE     = RGB565(0x33, 0x33, 0x33);
static const uint16_t C_CHEVRON  = RGB565(0x77, 0x77, 0x77);  // fresh data
static const uint16_t C_CHEV_OLD = RGB565(0x2A, 0x2A, 0x2A);  // showing cached data
static const uint16_t C_NEXT     = RGB565(0x99, 0x99, 0x99);
static const uint16_t C_DIM      = RGB565(0x77, 0x77, 0x77);
static const uint16_t C_GREEN    = RGB565(0x33, 0xEE, 0x55);
static const uint16_t C_AMBER    = RGB565(0xFF, 0xAA, 0x00);
static const uint16_t C_RED      = RGB565(0xFF, 0x33, 0x33);

// Anything that can light a pixel: the real panel, a test buffer, ...
struct Canvas {
  virtual void set(int x, int y, uint16_t c) = 0;
  virtual ~Canvas() {}
};

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

static inline void px(Canvas& cv, int x, int y, uint16_t c) {
  if (x >= 0 && x < PANEL_W && y >= 0 && y < PANEL_H) cv.set(x, y, c);
}

// Width in pixels of `s` drawn at scale k (3 px glyphs + 1 px gap, no trailing gap).
static inline int textWidth(const char* s, int k) {
  int n = (int)strlen(s);
  return n ? n * 4 * k - k : 0;
}

// Unknown characters draw as a space. Lowercase is drawn as uppercase.
static inline void drawText(Canvas& cv, int x, int y, const char* s, uint16_t c, int k) {
  for (; *s; s++) {
    char ch = (char)toupper((unsigned char)*s);
    const char* p = ch ? strchr(FONT_CHARS, ch) : nullptr;
    int g = p ? (int)(p - FONT_CHARS) : 0;
    for (int r = 0; r < 5; r++)
      for (int col = 0; col < 3; col++)
        if (FONT[g][r] & (4 >> col))
          for (int dy = 0; dy < k; dy++)
            for (int dx = 0; dx < k; dx++) px(cv, x + col * k + dx, y + r * k + dy, c);
    x += 4 * k;
  }
}

static inline void drawCentered(Canvas& cv, int y, const char* s, uint16_t c, int k) {
  drawText(cv, (PANEL_W - textWidth(s, k)) / 2, y, s, c, k);
}

// ------------------------------------------------------------------ main screen
// Green: you have time. Amber: time to head out. Red: you'd have to run.
static inline uint16_t urgencyColor(long mins, int walk) {
  if (mins < walk) return C_RED;
  if (mins < walk + 3) return C_AMBER;
  return C_GREEN;
}

// What the main screen needs to know.
struct MainInput {
  int hour, minute;       // local time, 24 h
  const char* route;      // route badge text ("30"); null or "" for no badge
  uint16_t routeColor;    // badge color
  time_t now;
  const time_t* dep;      // upcoming departures (>= now), ascending
  int n;                  // how many in dep
  time_t fetched;         // when the bus tracker last answered, 0 = never
  bool hasStop;           // a stop is configured
  int walk;               // minutes to walk to the stop
  int maxAge;             // seconds before data counts as stale
};

// What the main screen shows (separate from drawing so it's easy to test).
struct MainView {
  char clock[8];
  char route[8]; uint16_t routeColor;   // top-left badge, route[0] == 0 for none
  char big[6];  const char* unit;  uint16_t bigColor;   // bottom left
  char next[6]; uint16_t nextColor; uint16_t chevColor; // bottom right
  bool fresh;
};

static inline MainView mainView(const MainInput& in) {
  MainView v;
  snprintf(v.clock, sizeof v.clock, "%d:%02d", in.hour % 12 ? in.hour % 12 : 12, in.minute);
  snprintf(v.route, sizeof v.route, "%.4s", in.route ? in.route : "");   // up to 4 characters fit
  v.routeColor = in.routeColor;
  v.fresh = in.fetched && in.now - in.fetched <= in.maxAge;

  if (!in.hasStop) {
    strcpy(v.big, "NO"); v.unit = "STOP"; v.bigColor = C_DIM;
  } else if (!in.fetched) {
    strcpy(v.big, "--"); v.unit = ""; v.bigColor = C_DIM;       // waiting for the first response
  } else if (in.n == 0 && !v.fresh) {
    strcpy(v.big, "NO"); v.unit = "DATA"; v.bigColor = C_DIM;   // offline and cached buses have left
  } else if (in.n == 0) {
    strcpy(v.big, "NO"); v.unit = "BUS"; v.bigColor = C_DIM;
  } else {
    long m = (long)(in.dep[0] - in.now) / 60;
    if (m < 100) {
      snprintf(v.big, sizeof v.big, "%ld", m); v.unit = "MIN"; v.bigColor = urgencyColor(m, in.walk);
    } else {
      long h = (m + 30) / 60; if (h > 99) h = 99;
      snprintf(v.big, sizeof v.big, "%ld", h); v.unit = "HR"; v.bigColor = C_GREEN;
    }
  }

  v.nextColor = C_NEXT;
  v.chevColor = v.fresh ? C_CHEVRON : C_CHEV_OLD;
  if (in.hasStop && in.n >= 2) {
    long m2 = (long)(in.dep[1] - in.now) / 60;
    if (m2 < 100) snprintf(v.next, sizeof v.next, "%ldM", m2);
    else { long h2 = (m2 + 30) / 60; if (h2 > 99) h2 = 99; snprintf(v.next, sizeof v.next, "%ldH", h2); }
  } else {
    strcpy(v.next, "--");
  }
  return v;
}

// Width of the route badge: the number plus BADGE_PAD on each side, and at
// least as wide as it is tall. "30" -> 13 px, "G101" -> 21 px.
static inline int badgeWidth(const char* route) {
  int w = textWidth(route, 1) + 2 * BADGE_PAD;
  return w < BADGE_H ? BADGE_H : w;
}

// The route number is cut out of the badge (unlit pixels), which reads bolder
// than thin lit strokes. A badge too dark to show a hole (black, like X90)
// gets the number drawn in the clock color instead.
static inline uint16_t badgeTextColor(uint16_t bg) {
  int r = ((bg >> 11) & 0x1F) << 3, g = ((bg >> 5) & 0x3F) << 2, b = (bg & 0x1F) << 3;
  int brightest = r > g ? (r > b ? r : b) : (g > b ? g : b);
  return brightest < BADGE_DARK ? C_CLOCK : 0;
}

// Route badge with its left edge at x: filled in the route's color, number cut out.
static inline void drawBadge(Canvas& cv, int x0, const char* route, uint16_t bg) {
  uint16_t fg = badgeTextColor(bg);
  int w = badgeWidth(route);
  for (int y = 0; y < BADGE_H; y++)
    for (int x = 0; x < w; x++) px(cv, x0 + x, y, bg);
  drawText(cv, x0 + (w - textWidth(route, 1)) / 2, (BADGE_H - 5) / 2, route, fg, 1);
}

static inline void drawMain(Canvas& cv, const MainView& v) {
  // Top section: rows 0-11, clock 10 px tall with 1 px above and below.
  // With a route badge, the layout is fixed by the widest clock: badge + gap +
  // a CLOCK_MAX_W cell, centered, so the margin left of the badge equals the
  // margin right of the cell and the badge never moves. The time is centered
  // in its cell.
  if (v.route[0]) {
    int bw = badgeWidth(v.route);
    int x0 = (PANEL_W - (bw + BADGE_GAP + CLOCK_MAX_W)) / 2;
    int cell = x0 + bw + BADGE_GAP;
    drawBadge(cv, x0, v.route, v.routeColor);
    drawText(cv, cell + (CLOCK_MAX_W - textWidth(v.clock, 2)) / 2, 1, v.clock, C_CLOCK, 2);
  } else {
    drawCentered(cv, 1, v.clock, C_CLOCK, 2);
  }

  // Dividers
  for (int x = 0; x < PANEL_W; x++) px(cv, x, 12, C_LINE);
  for (int y = 13; y < PANEL_H; y++) px(cv, 44, y, C_LINE);

  // Bottom left (44 x 19): big number + small label, centered as a group.
  int gw = textWidth(v.big, 3) + 4 + textWidth(v.unit, 1);
  int gx = (44 - gw) / 2;
  drawText(cv, gx, 15, v.big, v.bigColor, 3);
  drawText(cv, gx + textWidth(v.big, 3) + 4, 25, v.unit, C_LABEL, 1);

  // Bottom right (19 x 19): chevron + the bus after next.
  for (int r = 0; r < 5; r++)
    for (int c = 0; c < 7; c++)
      if (CHEVRON[r] & (0x40 >> c)) px(cv, 51 + c, 16 + r, v.chevColor);
  drawText(cv, 45 + (19 - textWidth(v.next, 1)) / 2, 24, v.next, v.nextColor, 1);
}

// ------------------------------------------------------------------ other screens
// Largest scale (2 or 1) at which s fits across the panel. Double size fits 8 characters.
static inline int fitScale(const char* s) {
  return textWidth(s, 2) <= PANEL_W ? 2 : 1;
}

// Draw s centered in a 10 px tall row starting at y: double size when it fits,
// otherwise normal size centered vertically in the row.
static inline void drawFitted(Canvas& cv, int y, const char* s, uint16_t c) {
  int k = fitScale(s);
  drawCentered(cv, k == 2 ? y : y + 2, s, c, k);
}

// The setup network is open (no password), so the panel only names it.
//   JOIN WIFI      (small, amber)
//   BUSCLOCK       (network name, large when it fits)
//   192.168.4.1    (small; too long for large text)
static inline void drawSetupScreen(Canvas& cv, const char* ssid, const char* ip) {
  drawCentered(cv, 2, "JOIN WIFI", C_AMBER, 1);
  drawFitted(cv, 10, ssid, C_CLOCK);
  drawCentered(cv, 25, ip, C_NEXT, 1);
}

// One line centered, or two lines when b is given. Lines of up to 8
// characters are drawn at double size.
static inline void drawMessage(Canvas& cv, const char* a, const char* b) {
  if (!b) { drawFitted(cv, 11, a, C_CLOCK); return; }
  drawFitted(cv, 4, a, C_CLOCK);
  drawFitted(cv, 18, b, C_NEXT);
}
