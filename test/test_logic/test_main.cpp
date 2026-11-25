// Tests for logic.h: stop keys and the night-brightness window.
#include <unity.h>
#include "logic.h"

void setUp() {}
void tearDown() {}

void test_stop_key_new_format() {
  uint32_t id = 0; char dir = 0;
  TEST_ASSERT_TRUE(parseStopKey("34:S", &id, &dir));
  TEST_ASSERT_EQUAL_UINT32(34, id); TEST_ASSERT_EQUAL_CHAR('S', dir);
  TEST_ASSERT_TRUE(parseStopKey("8002:N", &id, &dir));
  TEST_ASSERT_EQUAL_UINT32(8002, id); TEST_ASSERT_EQUAL_CHAR('N', dir);
}

void test_stop_key_old_gtfs_directions() {
  uint32_t id = 0; char dir = 0;
  TEST_ASSERT_TRUE(parseStopKey("34:1", &id, &dir));
  TEST_ASSERT_EQUAL_CHAR('S', dir);
  TEST_ASSERT_TRUE(parseStopKey("104:0", &id, &dir));
  TEST_ASSERT_EQUAL_UINT32(104, id); TEST_ASSERT_EQUAL_CHAR('N', dir);
}

void test_stop_key_rejects_bad_input() {
  uint32_t id = 7; char dir = 'X';
  const char* bad[] = {nullptr, "", "34", "34:", ":S", "0:S", "34:E", "34:s", "34:SS", "34S", "abc:S", "34:2"};
  for (const char* k : bad) TEST_ASSERT_FALSE_MESSAGE(parseStopKey(k, &id, &dir), k ? k : "null");
  TEST_ASSERT_EQUAL_UINT32(7, id);     // outputs untouched on failure
  TEST_ASSERT_EQUAL_CHAR('X', dir);
}

void test_night_wrapping_midnight() {  // 23:00 - 07:00
  TEST_ASSERT_TRUE(isNight(23, 23, 7));
  TEST_ASSERT_TRUE(isNight(0, 23, 7));
  TEST_ASSERT_TRUE(isNight(6, 23, 7));
  TEST_ASSERT_FALSE(isNight(7, 23, 7));
  TEST_ASSERT_FALSE(isNight(12, 23, 7));
  TEST_ASSERT_FALSE(isNight(22, 23, 7));
}

void test_night_same_day() {  // 01:00 - 05:00
  TEST_ASSERT_FALSE(isNight(0, 1, 5));
  TEST_ASSERT_TRUE(isNight(1, 1, 5));
  TEST_ASSERT_TRUE(isNight(4, 1, 5));
  TEST_ASSERT_FALSE(isNight(5, 1, 5));
}

void test_night_disabled_when_equal() {
  for (int h = 0; h < 24; h++) TEST_ASSERT_FALSE(isNight(h, 6, 6));
}

// ------------------------------------------------------------------ chooseScreen
// Connected, clock set, setup network closed: the normal state.
static ScreenInput normal() {
  ScreenInput s = {false, false, true, false, true, true, 0, 0};
  return s;
}

void test_screen_normal() {
  TEST_ASSERT_EQUAL_INT(SCREEN_MAIN, chooseScreen(normal()));
}

void test_screen_setup_only_when_needed() {
  ScreenInput s = normal();
  s.apActive = true; s.connected = false; s.offlineMs = 20 * 60 * 1000;  // outage reopened setup
  s.ms = 1000;                                                           // main phase
  TEST_ASSERT_EQUAL_INT(SCREEN_MAIN, chooseScreen(s));                   // countdown stays up
  s.setupClient = true;
  TEST_ASSERT_EQUAL_INT(SCREEN_SETUP, chooseScreen(s));                  // a phone joined setup
  s.setupClient = false; s.forceSetup = true;
  TEST_ASSERT_EQUAL_INT(SCREEN_SETUP, chooseScreen(s));                  // BOOT held
  ScreenInput fresh = {true, false, false, false, false, false, 0, 0};   // first boot
  TEST_ASSERT_EQUAL_INT(SCREEN_SETUP, chooseScreen(fresh));
}

void test_screen_clock_not_set() {
  ScreenInput s = normal();
  s.timeValid = false;
  TEST_ASSERT_EQUAL_INT(SCREEN_SETTING_CLOCK, chooseScreen(s));
  s.connected = false;
  TEST_ASSERT_EQUAL_INT(SCREEN_WIFI_OFFLINE, chooseScreen(s));          // nothing to count down
}

void test_screen_flashes_when_wifi_down() {
  ScreenInput s = normal();
  s.connected = false;
  s.offlineMs = OFFLINE_FLASH_GRACE_MS;
  int mainCount = 0, offCount = 0;
  for (uint32_t ms = 100000; ms < 100000 + 60000; ms += 500) {   // one minute, sampled like render()
    s.ms = ms;
    ScreenKind k = chooseScreen(s);
    TEST_ASSERT_TRUE(k == SCREEN_MAIN || k == SCREEN_WIFI_OFFLINE);
    (k == SCREEN_MAIN ? mainCount : offCount)++;
  }
  TEST_ASSERT_EQUAL_INT(80, mainCount);   // 4 s of every 6 s
  TEST_ASSERT_EQUAL_INT(40, offCount);    // 2 s of every 6 s
  s.ms = 4000;  TEST_ASSERT_EQUAL_INT(SCREEN_WIFI_OFFLINE, chooseScreen(s));
  s.ms = 5999;  TEST_ASSERT_EQUAL_INT(SCREEN_WIFI_OFFLINE, chooseScreen(s));
  s.ms = 6000;  TEST_ASSERT_EQUAL_INT(SCREEN_MAIN, chooseScreen(s));
}

void test_screen_no_flash_during_short_blip() {
  ScreenInput s = normal();
  s.connected = false;
  s.offlineMs = OFFLINE_FLASH_GRACE_MS - 1;
  for (uint32_t ms = 0; ms < 12000; ms += 500) { s.ms = ms; TEST_ASSERT_EQUAL_INT(SCREEN_MAIN, chooseScreen(s)); }
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_stop_key_new_format);
  RUN_TEST(test_stop_key_old_gtfs_directions);
  RUN_TEST(test_stop_key_rejects_bad_input);
  RUN_TEST(test_night_wrapping_midnight);
  RUN_TEST(test_night_same_day);
  RUN_TEST(test_night_disabled_when_equal);
  RUN_TEST(test_screen_normal);
  RUN_TEST(test_screen_setup_only_when_needed);
  RUN_TEST(test_screen_clock_not_set);
  RUN_TEST(test_screen_flashes_when_wifi_down);
  RUN_TEST(test_screen_no_flash_during_short_blip);
  return UNITY_END();
}
