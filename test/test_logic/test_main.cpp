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

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_stop_key_new_format);
  RUN_TEST(test_stop_key_old_gtfs_directions);
  RUN_TEST(test_stop_key_rejects_bad_input);
  RUN_TEST(test_night_wrapping_midnight);
  RUN_TEST(test_night_same_day);
  RUN_TEST(test_night_disabled_when_equal);
  return UNITY_END();
}
