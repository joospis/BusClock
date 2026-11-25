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
  MainInput in = {16, 13, NOW, dep, n, fetched, true, 4, 180};
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

void test_other_screens_stay_on_panel() {
  Buffer b;
  drawSetupScreen(b, "BusClock", "30303030", "192.168.4.1");
  TEST_ASSERT_EQUAL_INT(0, b.outOfRange);
  TEST_ASSERT_TRUE(b.count(C_AMBER) > 0);
  Buffer m;
  drawMessage(m, "CONNECTING", "WIFI...");
  TEST_ASSERT_EQUAL_INT(0, m.outOfRange);
  TEST_ASSERT_TRUE(m.count(C_CLOCK) > 0 && m.count(C_NEXT) > 0);
  Buffer one;
  drawMessage(one, "HELLO", nullptr);
  TEST_ASSERT_EQUAL_INT(0, one.count(C_NEXT));
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
  RUN_TEST(test_other_screens_stay_on_panel);
  return UNITY_END();
}
