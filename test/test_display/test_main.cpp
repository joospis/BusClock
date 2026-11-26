// Tests for display.h: what the panel shows and where pixels land.
#include <unity.h>
#include <string.h>
#include "display.h"

void setUp() {}
void tearDown() {}

// A 64x32 framebuffer that also records out-of-range writes.
struct Buffer : Canvas {
  uint16_t px[PANEL_H][PANEL_W];
  int outOfRange = 0;
  Buffer() { memset(px, 0, sizeof px); }
  void set(int x, int y, uint16_t c) override {
    if (x < 0 || x >= PANEL_W || y < 0 || y >= PANEL_H) { outOfRange++; return; }
    px[y][x] = c;
  }
  int count(uint16_t c, int x0 = 0, int y0 = 0, int x1 = PANEL_W, int y1 = PANEL_H) const {
    int n = 0;
    for (int y = y0; y < y1; y++)
      for (int x = x0; x < x1; x++) n += px[y][x] == c;
    return n;
  }
  int lit(int x0 = 0, int y0 = 0, int x1 = PANEL_W, int y1 = PANEL_H) const {
    return (x1 - x0) * (y1 - y0) - count(0, x0, y0, x1, y1);
  }
};

static const time_t NOW = 1795640000;

static MainInput input(const time_t* dep, int n, time_t fetched = NOW - 10) {
  MainInput in = {16, 13, nullptr, 0, NOW, dep, n, fetched, true, 4, 180};
  return in;
}

// ------------------------------------------------------------------ text
void test_text_width() {
  TEST_ASSERT_EQUAL_INT(0, textWidth("", 1));
  TEST_ASSERT_EQUAL_INT(3, textWidth("A", 1));
  TEST_ASSERT_EQUAL_INT(7, textWidth("AB", 1));
  TEST_ASSERT_EQUAL_INT(9, textWidth("A", 3));
  TEST_ASSERT_EQUAL_INT(21, textWidth("12", 3));
}

void test_glyph_pixels() {
  Buffer b;
  drawText(b, 0, 0, "1", 1, 1);   // 1 = {2,6,2,2,7}
  const char* rows[5] = {".#.", "##.", ".#.", ".#.", "###"};
  for (int y = 0; y < 5; y++)
    for (int x = 0; x < 3; x++)
      TEST_ASSERT_EQUAL_UINT16_MESSAGE(rows[y][x] == '#' ? 1 : 0, b.px[y][x], "glyph 1");
  TEST_ASSERT_EQUAL_INT(8, b.lit());
}

void test_scaled_text_and_case() {
  Buffer a, b;
  drawText(a, 0, 0, "abc", 1, 2);
  drawText(b, 0, 0, "ABC", 1, 2);
  TEST_ASSERT_EQUAL_MEMORY(a.px, b.px, sizeof a.px);
  Buffer one;
  drawText(one, 0, 0, "1", 1, 1);
  Buffer two;
  drawText(two, 0, 0, "1", 1, 2);
  TEST_ASSERT_EQUAL_INT(one.lit() * 4, two.lit());
}

void test_unknown_chars_are_blank_and_clipping_is_safe() {
  Buffer b;
  drawText(b, 0, 0, "@#?", 1, 1);
  TEST_ASSERT_EQUAL_INT(0, b.lit());
  drawText(b, 62, 30, "88", 1, 3);   // mostly off-panel
  TEST_ASSERT_EQUAL_INT(0, b.outOfRange);
  TEST_ASSERT_TRUE(b.lit() > 0);
}

// ------------------------------------------------------------------ urgency
void test_urgency_thresholds() {
  TEST_ASSERT_EQUAL_UINT16(C_RED, urgencyColor(0, 4));
  TEST_ASSERT_EQUAL_UINT16(C_RED, urgencyColor(3, 4));
  TEST_ASSERT_EQUAL_UINT16(C_AMBER, urgencyColor(4, 4));
  TEST_ASSERT_EQUAL_UINT16(C_AMBER, urgencyColor(6, 4));
  TEST_ASSERT_EQUAL_UINT16(C_GREEN, urgencyColor(7, 4));
  TEST_ASSERT_EQUAL_UINT16(C_AMBER, urgencyColor(0, 0));
  TEST_ASSERT_EQUAL_UINT16(C_GREEN, urgencyColor(3, 0));
}

// ------------------------------------------------------------------ main view
void test_clock_12_hour() {
  time_t d[1] = {NOW + 600};
  MainInput in = input(d, 1);
  in.hour = 0;  in.minute = 5;  TEST_ASSERT_EQUAL_STRING("12:05", mainView(in).clock);
  in.hour = 12; in.minute = 0;  TEST_ASSERT_EQUAL_STRING("12:00", mainView(in).clock);
  in.hour = 16; in.minute = 13; TEST_ASSERT_EQUAL_STRING("4:13", mainView(in).clock);
  in.hour = 9;  in.minute = 59; TEST_ASSERT_EQUAL_STRING("9:59", mainView(in).clock);
}

void test_minutes_and_next_bus() {
  time_t d[2] = {NOW + 7 * 60 + 59, NOW + 22 * 60};
  MainView v = mainView(input(d, 2));
  TEST_ASSERT_EQUAL_STRING("7", v.big);       // rounds down
  TEST_ASSERT_EQUAL_STRING("MIN", v.unit);
  TEST_ASSERT_EQUAL_UINT16(C_GREEN, v.bigColor);
  TEST_ASSERT_EQUAL_STRING("22M", v.next);
  TEST_ASSERT_TRUE(v.fresh);
  TEST_ASSERT_EQUAL_UINT16(C_CHEVRON, v.chevColor);
}

void test_only_one_bus() {
  time_t d[1] = {NOW + 120};
  MainView v = mainView(input(d, 1));
  TEST_ASSERT_EQUAL_STRING("2", v.big);
  TEST_ASSERT_EQUAL_UINT16(C_RED, v.bigColor);
  TEST_ASSERT_EQUAL_STRING("--", v.next);
}

void test_hours() {
  time_t d[2] = {NOW + 100 * 60, NOW + 150 * 60};
  MainView v = mainView(input(d, 2));
  TEST_ASSERT_EQUAL_STRING("2", v.big);       // 100 min rounds to 2 h
  TEST_ASSERT_EQUAL_STRING("HR", v.unit);
  TEST_ASSERT_EQUAL_UINT16(C_GREEN, v.bigColor);
  TEST_ASSERT_EQUAL_STRING("3H", v.next);     // 150 min -> 3 h

  time_t far[2] = {NOW + 200 * 3600, NOW + 300 * 3600};
  v = mainView(input(far, 2));
  TEST_ASSERT_EQUAL_STRING("99", v.big);
  TEST_ASSERT_EQUAL_STRING("99H", v.next);
}

void test_boundary_99_and_100_minutes() {
  time_t d[2] = {NOW + 99 * 60, NOW + 100 * 60};
  MainView v = mainView(input(d, 2));
  TEST_ASSERT_EQUAL_STRING("99", v.big);
  TEST_ASSERT_EQUAL_STRING("MIN", v.unit);
  TEST_ASSERT_EQUAL_STRING("2H", v.next);
}

void test_states_without_buses() {
  MainInput in = input(nullptr, 0);
  in.hasStop = false;
  MainView v = mainView(in);
  TEST_ASSERT_EQUAL_STRING("NO", v.big); TEST_ASSERT_EQUAL_STRING("STOP", v.unit);

  v = mainView(input(nullptr, 0, 0));
  TEST_ASSERT_EQUAL_STRING("--", v.big); TEST_ASSERT_EQUAL_STRING("", v.unit);
  TEST_ASSERT_FALSE(v.fresh);

  v = mainView(input(nullptr, 0, NOW - 10));
  TEST_ASSERT_EQUAL_STRING("NO", v.big); TEST_ASSERT_EQUAL_STRING("BUS", v.unit);

  v = mainView(input(nullptr, 0, NOW - 600));
  TEST_ASSERT_EQUAL_STRING("NO", v.big); TEST_ASSERT_EQUAL_STRING("DATA", v.unit);
  TEST_ASSERT_EQUAL_UINT16(C_DIM, v.bigColor);
}

void test_stale_data_dims_chevron_but_keeps_counting() {
  time_t d[2] = {NOW + 300, NOW + 900};
  MainView v = mainView(input(d, 2, NOW - 180));
  TEST_ASSERT_TRUE(v.fresh);                       // exactly maxAge is still fresh
  v = mainView(input(d, 2, NOW - 181));
  TEST_ASSERT_FALSE(v.fresh);
  TEST_ASSERT_EQUAL_UINT16(C_CHEV_OLD, v.chevColor);
  TEST_ASSERT_EQUAL_STRING("5", v.big);
  TEST_ASSERT_EQUAL_STRING("15M", v.next);
}

// ------------------------------------------------------------------ drawing
void test_draw_main_layout() {
  time_t d[2] = {NOW + 7 * 60, NOW + 22 * 60};
  Buffer b;
  drawMain(b, mainView(input(d, 2)));
  TEST_ASSERT_EQUAL_INT(0, b.outOfRange);
  // dividers
  for (int x = 0; x < PANEL_W; x++) TEST_ASSERT_EQUAL_UINT16(C_LINE, b.px[12][x]);
  for (int y = 13; y < PANEL_H; y++) TEST_ASSERT_EQUAL_UINT16(C_LINE, b.px[y][44]);
  // clock only above the line, countdown only bottom-left, chevron bottom-right
  TEST_ASSERT_TRUE(b.count(C_CLOCK, 0, 0, 64, 12) > 0);
  TEST_ASSERT_EQUAL_INT(0, b.lit(0, 0, 64, 1));                // 1 px margin on top
  TEST_ASSERT_TRUE(b.count(C_GREEN, 0, 13, 44, 32) > 0);
  TEST_ASSERT_EQUAL_INT(0, b.count(C_GREEN, 45, 0, 64, 32));
  TEST_ASSERT_EQUAL_INT(10, b.count(C_CHEVRON, 45, 13, 64, 32));  // ">>" is 2 px x 5 rows
  TEST_ASSERT_TRUE(b.count(C_NEXT, 45, 13, 64, 32) > 0);
}

static const uint16_t ORANGE = RGB565(0xFB, 0xB0, 0x33);

// Columns that contain any pixel of color c in rows y0..y1-1.
static void colRange(const Buffer& b, uint16_t c, int y0, int y1, int* minX, int* maxX) {
  *minX = 99; *maxX = -1;
  for (int y = y0; y < y1; y++)
    for (int x = 0; x < PANEL_W; x++)
      if (b.px[y][x] == c) { if (x < *minX) *minX = x; if (x > *maxX) *maxX = x; }
}

void test_rgb565_conversion() {
  TEST_ASSERT_EQUAL_UINT16(ORANGE, rgb565(0xFBB033));
  TEST_ASSERT_EQUAL_UINT16(0xFFFF, rgb565(0xFFFFFF));
  TEST_ASSERT_EQUAL_UINT16(0, rgb565(0x000000));
  TEST_ASSERT_EQUAL_UINT16(0xF800, rgb565(0xFF0000));
}

void test_badge_width() {
  TEST_ASSERT_EQUAL_INT(11, badgeWidth("3"));      // never narrower than it is tall
  TEST_ASSERT_EQUAL_INT(13, badgeWidth("30"));     // 7 px text + 3 px each side
  TEST_ASSERT_EQUAL_INT(17, badgeWidth("R41"));
  TEST_ASSERT_EQUAL_INT(21, badgeWidth("G101"));
}

void test_badge_is_filled_square_with_cutout_text() {
  time_t d[1] = {NOW + 600};
  MainInput in = input(d, 1);
  in.route = "30"; in.routeColor = ORANGE;
  Buffer b;
  drawMain(b, mainView(in));
  // laid out for the widest clock: badge 13 + gap 3 + 38 = 54 px, centered,
  // so x 5..17 is the badge, with "30" cut out of it in black
  int lit = b.count(ORANGE, 5, 0, 18, 11);
  Buffer glyphs;
  drawText(glyphs, 0, 0, "30", 1, 1);
  TEST_ASSERT_EQUAL_INT(13 * 11 - glyphs.lit(), lit);
  TEST_ASSERT_EQUAL_INT(lit, b.count(ORANGE));     // nothing orange outside it
  TEST_ASSERT_EQUAL_UINT16(0, b.px[3][8]);         // top-left of "3" is black (cut out)
  for (int x = 5; x < 8; x++) TEST_ASSERT_EQUAL_UINT16(ORANGE, b.px[5][x]);    // 3 px padding left
  for (int x = 15; x < 18; x++) TEST_ASSERT_EQUAL_UINT16(ORANGE, b.px[5][x]);  // and right
}

// Leftmost and rightmost lit columns in the top section.
static void topExtent(const Buffer& b, int* minX, int* maxX) {
  *minX = 99; *maxX = -1;
  for (int y = 0; y < 12; y++)
    for (int x = 0; x < PANEL_W; x++)
      if (b.px[y][x]) { if (x < *minX) *minX = x; if (x > *maxX) *maxX = x; }
}

void test_badge_stays_put_and_clock_centered_in_its_cell() {
  time_t d[1] = {NOW + 600};
  const char* routes[] = {"7", "30", "B43", "G101"};
  for (const char* r : routes) {
    int bw = badgeWidth(r);
    int x0 = (PANEL_W - (bw + BADGE_GAP + CLOCK_MAX_W)) / 2;
    int cell = x0 + bw + BADGE_GAP;
    TEST_ASSERT_EQUAL_INT_MESSAGE(x0, PANEL_W - (cell + CLOCK_MAX_W), r);   // equal outer margins
    TEST_ASSERT_TRUE_MESSAGE(x0 >= 1, r);
    for (int h = 0; h < 24; h++) {               // "12:xx" (widest) through "1:xx" (narrowest)
      MainInput in = input(d, 1);
      in.route = r; in.routeColor = ORANGE;
      in.hour = h; in.minute = 59;
      Buffer b;
      drawMain(b, mainView(in));
      int minX, maxX;
      colRange(b, ORANGE, 0, 11, &minX, &maxX);
      TEST_ASSERT_EQUAL_INT_MESSAGE(x0, minX, r);            // badge never moves
      TEST_ASSERT_EQUAL_INT_MESSAGE(x0 + bw - 1, maxX, r);
      colRange(b, C_CLOCK, 0, 12, &minX, &maxX);             // time inside its cell, centered
      int before = minX - cell, after = cell + CLOCK_MAX_W - 1 - maxX;
      TEST_ASSERT_TRUE_MESSAGE(before >= 0 && after >= 0, r);
      TEST_ASSERT_TRUE_MESSAGE(before == after || before + 1 == after, r);
    }
  }
}

// Columns inside [x0, x1) of rows 0-10 that contain an unlit pixel.
static void holeRange(const Buffer& b, int x0, int x1, int* minX, int* maxX) {
  *minX = 99; *maxX = -1;
  for (int y = 0; y < BADGE_H; y++)
    for (int x = x0; x < x1; x++)
      if (b.px[y][x] == 0) { if (x < *minX) *minX = x; if (x > *maxX) *maxX = x; }
}

void test_badge_text_color() {
  // cut out (unlit) on every route color PVTA uses, including the dark ones...
  const uint32_t cutout[] = {0xFBB033, 0xEF4E91, 0x00AEEC, 0x00A94E, 0xFF0000, 0x00467E, 0x7E0A6D,
                             0x27276E, 0xFFDD15, 0x808080, 0x404040};
  for (uint32_t c : cutout) TEST_ASSERT_EQUAL_UINT16_MESSAGE(0, badgeTextColor(rgb565(c)), "should be cut out");
  // ...except a badge too dark to show a hole, like X90's black
  const uint32_t dark[] = {0x000000, 0x3F3F3F, 0x10203F};
  for (uint32_t c : dark) TEST_ASSERT_EQUAL_UINT16_MESSAGE(C_CLOCK, badgeTextColor(rgb565(c)), "should be drawn");
}

void test_black_badge_draws_the_number() {
  time_t d[1] = {NOW + 600};
  MainInput in = input(d, 1);
  in.route = "X90"; in.routeColor = 0;
  Buffer b;
  drawMain(b, mainView(in));
  int x0 = (PANEL_W - (badgeWidth("X90") + BADGE_GAP + CLOCK_MAX_W)) / 2;
  Buffer glyphs;
  drawText(glyphs, 0, 0, "X90", 1, 1);
  TEST_ASSERT_EQUAL_INT(glyphs.lit(), b.count(C_CLOCK, x0, 0, x0 + badgeWidth("X90"), BADGE_H));
}

void test_badge_cutout_and_clock_moves_right() {
  time_t d[1] = {NOW + 600};
  MainInput in = input(d, 1);
  in.hour = 12; in.minute = 59;                 // widest clock, 38 px
  const uint16_t GREEN = RGB565(0x00, 0xA9, 0x4E);
  in.route = "G101"; in.routeColor = GREEN;
  Buffer b;
  drawMain(b, mainView(in));
  TEST_ASSERT_EQUAL_INT(0, b.outOfRange);
  int minX, maxX;
  // badge 21 + gap 3 + clock 38 = 62 px, centered: badge x 1..21, clock x 25..62
  colRange(b, GREEN, 0, BADGE_H, &minX, &maxX);
  TEST_ASSERT_EQUAL_INT(1, minX);
  TEST_ASSERT_EQUAL_INT(21, maxX);
  holeRange(b, 1, 22, &minX, &maxX);            // number cut out, 3 px in from the badge edges
  TEST_ASSERT_EQUAL_INT(4, minX);
  TEST_ASSERT_EQUAL_INT(18, maxX);
  Buffer glyphs;
  drawText(glyphs, 0, 0, "G101", 1, 1);
  TEST_ASSERT_EQUAL_INT(21 * BADGE_H - glyphs.lit(), b.count(GREEN, 1, 0, 22, BADGE_H));
  colRange(b, C_CLOCK, 0, 12, &minX, &maxX);
  TEST_ASSERT_EQUAL_INT(25, minX);
  TEST_ASSERT_EQUAL_INT(62, maxX);
}

void test_no_badge_keeps_clock_centered() {
  time_t d[1] = {NOW + 600};
  MainInput in = input(d, 1);                   // route = nullptr
  Buffer b;
  drawMain(b, mainView(in));
  int minX, maxX;
  colRange(b, C_CLOCK, 0, 12, &minX, &maxX);    // "4:13" is 30 px: (64-30)/2 = 17
  TEST_ASSERT_EQUAL_INT(17, minX);
  TEST_ASSERT_EQUAL_INT(46, maxX);
  in.route = "";
  Buffer e;
  drawMain(e, mainView(in));
  TEST_ASSERT_EQUAL_MEMORY(b.px, e.px, sizeof b.px);
}

void test_badge_name_truncated_to_four() {
  time_t d[1] = {NOW + 600};
  MainInput in = input(d, 1);
  in.route = "ABCDEFG";
  TEST_ASSERT_EQUAL_STRING("ABCD", mainView(in).route);
}

void test_draw_main_big_text_is_centered() {
  time_t d[1] = {NOW + 7 * 60};
  Buffer b;
  drawMain(b, mainView(input(d, 1)));
  // "7" at scale 3 + 4 px + "MIN" (11 px) = 24 px group, starts at (44-24)/2 = 10
  int minX = 99, maxX = -1;
  for (int y = 13; y < 32; y++)
    for (int x = 0; x < 44; x++)
      if (b.px[y][x] == C_GREEN || b.px[y][x] == C_LABEL) { if (x < minX) minX = x; if (x > maxX) maxX = x; }
  TEST_ASSERT_EQUAL_INT(10, minX);
  TEST_ASSERT_EQUAL_INT(33, maxX);
}

// Height in rows of everything lit in color c.
static int litHeight(const Buffer& b, uint16_t c) {
  int top = -1, bottom = -1;
  for (int y = 0; y < PANEL_H; y++)
    for (int x = 0; x < PANEL_W; x++)
      if (b.px[y][x] == c) { if (top < 0) top = y; bottom = y; }
  return top < 0 ? 0 : bottom - top + 1;
}

void test_fit_scale() {
  TEST_ASSERT_EQUAL_INT(2, fitScale("BUSCLOCK"));   // 8 chars = 62 px at 2x
  TEST_ASSERT_EQUAL_INT(2, fitScale("CLOCK..."));
  TEST_ASSERT_EQUAL_INT(1, fitScale("CONNECTI.")); // 9 chars = 70 px at 2x
  TEST_ASSERT_EQUAL_INT(1, fitScale("192.168.4.1"));
}

void test_setup_screen() {
  Buffer b;
  drawSetupScreen(b, "BusClock", "192.168.4.1");
  TEST_ASSERT_EQUAL_INT(0, b.outOfRange);
  TEST_ASSERT_TRUE(b.count(C_AMBER) > 0);
  TEST_ASSERT_EQUAL_INT(10, litHeight(b, C_CLOCK));   // network name at double size
  TEST_ASSERT_EQUAL_INT(5, litHeight(b, C_NEXT));     // IP stays small
  Buffer longName;
  drawSetupScreen(longName, "MY-LONG-NETWORK", "192.168.4.1");
  TEST_ASSERT_EQUAL_INT(0, longName.outOfRange);
  TEST_ASSERT_EQUAL_INT(5, litHeight(longName, C_CLOCK));  // falls back to small
}

void test_messages_are_large_when_they_fit() {
  Buffer m;
  drawMessage(m, "JOINING", "WIFI...");
  TEST_ASSERT_EQUAL_INT(0, m.outOfRange);
  TEST_ASSERT_EQUAL_INT(10, litHeight(m, C_CLOCK));
  TEST_ASSERT_EQUAL_INT(10, litHeight(m, C_NEXT));
  // the two lines don't touch
  for (int y = 14; y < 18; y++) TEST_ASSERT_EQUAL_INT(0, m.lit(0, y, PANEL_W, y + 1));

  Buffer one;
  drawMessage(one, "HELLO", nullptr);
  TEST_ASSERT_EQUAL_INT(0, one.count(C_NEXT));
  TEST_ASSERT_EQUAL_INT(10, litHeight(one, C_CLOCK));

  Buffer longer;
  drawMessage(longer, "CONNECTING", "WIFI...");
  TEST_ASSERT_EQUAL_INT(0, longer.outOfRange);
  TEST_ASSERT_EQUAL_INT(5, litHeight(longer, C_CLOCK));    // too wide for 2x
}

// Every message the firmware shows fits at double size.
void test_firmware_messages_fit_large() {
  const char* lines[] = {"JOINING", "WIFI...", "SETTING", "CLOCK...", "WIFI", "OFFLINE", "BusClock"};
  for (const char* s : lines) TEST_ASSERT_EQUAL_INT_MESSAGE(2, fitScale(s), s);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_text_width);
  RUN_TEST(test_glyph_pixels);
  RUN_TEST(test_scaled_text_and_case);
  RUN_TEST(test_unknown_chars_are_blank_and_clipping_is_safe);
  RUN_TEST(test_urgency_thresholds);
  RUN_TEST(test_clock_12_hour);
  RUN_TEST(test_minutes_and_next_bus);
  RUN_TEST(test_only_one_bus);
  RUN_TEST(test_hours);
  RUN_TEST(test_boundary_99_and_100_minutes);
  RUN_TEST(test_states_without_buses);
  RUN_TEST(test_stale_data_dims_chevron_but_keeps_counting);
  RUN_TEST(test_draw_main_layout);
  RUN_TEST(test_draw_main_big_text_is_centered);
  RUN_TEST(test_rgb565_conversion);
  RUN_TEST(test_badge_width);
  RUN_TEST(test_badge_is_filled_square_with_cutout_text);
  RUN_TEST(test_badge_text_color);
  RUN_TEST(test_black_badge_draws_the_number);
  RUN_TEST(test_badge_cutout_and_clock_moves_right);
  RUN_TEST(test_badge_stays_put_and_clock_centered_in_its_cell);
  RUN_TEST(test_no_badge_keeps_clock_centered);
  RUN_TEST(test_badge_name_truncated_to_four);
  RUN_TEST(test_fit_scale);
  RUN_TEST(test_setup_screen);
  RUN_TEST(test_messages_are_large_when_they_fit);
  RUN_TEST(test_firmware_messages_fit_large);
  return UNITY_END();
}
