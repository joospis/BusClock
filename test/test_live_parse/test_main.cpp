// Tests for live_parse.h: reading PVTA InfoPoint responses.
#include <unity.h>
#include <string>
#include "live_parse.h"

void setUp() {}
void tearDown() {}

// /Date(ms-0500)/ for an epoch second value
static std::string date(long long sec) { return "\\/Date(" + std::to_string(sec * 1000) + "-0500)\\/"; }

static std::string dep(long long edt, bool completed = false, bool last = false, long long sdt = -1) {
  std::string s = "{\"ADT\":null,\"Dev\":\"00:00:00\",";
  if (edt >= 0) s += "\"EDT\":\"" + date(edt) + "\",";
  else s += "\"EDT\":null,";
  s += "\"SDT\":\"" + date(sdt >= 0 ? sdt : (edt >= 0 ? edt : 0)) + "\",";
  s += std::string("\"IsCompleted\":") + (completed ? "true" : "false") + ",";
  s += std::string("\"IsLastStopOnTrip\":") + (last ? "true" : "false") + ",";
  s += "\"Trip\":{\"TripDirection\":\"S\",\"InternetServiceDesc\":\"Old Belchertown Road\"}}";
  return s;
}

static std::string routeDir(long route, const char* dir, const std::string& deps) {
  return "{\"Departures\":[" + deps + "],\"Direction\":\"x\",\"DirectionCode\":\"" + dir +
         "\",\"IsDone\":false,\"RouteId\":" + std::to_string(route) + "}";
}

static std::string response(const std::string& dirs) {
  return "[{\"LastUpdated\":\"\\/Date(1795641768127-0500)\\/\",\"RouteDirections\":[" + dirs + "],\"StopId\":34}]";
}

static int parseDeps(const std::string& json, long route, const char* dir, time_t now, time_t* out, int maxOut) {
  JsonDocument filter, doc;
  live_buildFilter(filter);
  DeserializationError err = deserializeJson(doc, json, DeserializationOption::Filter(filter));
  TEST_ASSERT_FALSE_MESSAGE(err, err.c_str());
  return live_extract(doc, route, dir, now, out, maxOut);
}

static const time_t NOW = 1795640000;

// ------------------------------------------------------------------ dates
void test_parseDate_valid() {
  TEST_ASSERT_EQUAL_INT64(1795643100, live_parseDate("/Date(1795643100000-0500)/"));
  TEST_ASSERT_EQUAL_INT64(1795643100, live_parseDate("/Date(1795643100999+0000)/"));  // ms truncated
}

void test_parseDate_malformed() {
  TEST_ASSERT_EQUAL_INT64(0, live_parseDate(nullptr));
  TEST_ASSERT_EQUAL_INT64(0, live_parseDate(""));
  TEST_ASSERT_EQUAL_INT64(0, live_parseDate("2026-11-25T16:45:00"));
  TEST_ASSERT_EQUAL_INT64(0, live_parseDate("/Date(abc)/"));
  TEST_ASSERT_EQUAL_INT64(0, live_parseDate("/Date(-2208910500000-0500)/"));  // TripStartTime style, 1900
}

// ------------------------------------------------------------------ departures
void test_extract_sorted_and_filtered_by_route_and_direction() {
  std::string json = response(
      routeDir(30030, "N", dep(NOW + 60)) + "," +
      routeDir(30030, "S", dep(NOW + 900) + "," + dep(NOW + 300) + "," + dep(NOW + 600)) + "," +
      routeDir(30031, "S", dep(NOW + 120)));
  time_t out[8];
  int n = parseDeps(json, 30030, "S", NOW, out, 8);
  TEST_ASSERT_EQUAL_INT(3, n);
  TEST_ASSERT_EQUAL_INT64(NOW + 300, out[0]);
  TEST_ASSERT_EQUAL_INT64(NOW + 600, out[1]);
  TEST_ASSERT_EQUAL_INT64(NOW + 900, out[2]);

  n = parseDeps(json, 30030, "N", NOW, out, 8);
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL_INT64(NOW + 60, out[0]);
}

void test_extract_skips_completed_last_stop_and_past() {
  std::string json = response(routeDir(30030, "S",
      dep(NOW + 100, true) + "," +          // already left
      dep(NOW + 200, false, true) + "," +   // trip ends here
      dep(NOW - 1) + "," +                  // in the past
      dep(NOW) + "," +                      // leaving right now counts
      dep(NOW + 400)));
  time_t out[8];
  int n = parseDeps(json, 30030, "S", NOW, out, 8);
  TEST_ASSERT_EQUAL_INT(2, n);
  TEST_ASSERT_EQUAL_INT64(NOW, out[0]);
  TEST_ASSERT_EQUAL_INT64(NOW + 400, out[1]);
}

void test_extract_removes_duplicates() {
  std::string json = response(routeDir(30030, "S", dep(NOW + 300) + "," + dep(NOW + 300) + "," + dep(NOW + 600)));
  time_t out[8];
  TEST_ASSERT_EQUAL_INT(2, parseDeps(json, 30030, "S", NOW, out, 8));
}

void test_extract_keeps_earliest_when_full() {
  std::string json = response(routeDir(30030, "S",
      dep(NOW + 500) + "," + dep(NOW + 400) + "," + dep(NOW + 300) + "," + dep(NOW + 200) + "," + dep(NOW + 100)));
  time_t out[2];
  int n = parseDeps(json, 30030, "S", NOW, out, 2);
  TEST_ASSERT_EQUAL_INT(2, n);
  TEST_ASSERT_EQUAL_INT64(NOW + 100, out[0]);
  TEST_ASSERT_EQUAL_INT64(NOW + 200, out[1]);
}

void test_extract_uses_scheduled_time_without_estimate() {
  std::string json = response(routeDir(30030, "S", dep(-1, false, false, NOW + 700)));
  time_t out[4];
  int n = parseDeps(json, 30030, "S", NOW, out, 4);
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL_INT64(NOW + 700, out[0]);
}

void test_extract_prefers_estimate_over_schedule() {
  std::string json = response(routeDir(30030, "S", dep(NOW + 900, false, false, NOW + 600)));  // running late
  time_t out[4];
  TEST_ASSERT_EQUAL_INT(1, parseDeps(json, 30030, "S", NOW, out, 4));
  TEST_ASSERT_EQUAL_INT64(NOW + 900, out[0]);
}

void test_extract_empty_and_odd_responses() {
  time_t out[4];
  TEST_ASSERT_EQUAL_INT(0, parseDeps("[]", 30030, "S", NOW, out, 4));
  TEST_ASSERT_EQUAL_INT(0, parseDeps(response(routeDir(30030, "S", "")), 30030, "S", NOW, out, 4));
  TEST_ASSERT_EQUAL_INT(0, parseDeps("[{\"RouteDirections\":null}]", 30030, "S", NOW, out, 4));
}

void test_extract_other_direction_codes() {
  std::string json = response(
      routeDir(10043, "E", dep(NOW + 120)) + "," + routeDir(10043, "W", dep(NOW + 240)) + "," +
      routeDir(30034, "UP", dep(NOW + 360)) + "," + routeDir(30035, "U", dep(NOW + 480)));
  time_t out[4];
  TEST_ASSERT_EQUAL_INT(1, parseDeps(json, 10043, "E", NOW, out, 4));
  TEST_ASSERT_EQUAL_INT64(NOW + 120, out[0]);
  TEST_ASSERT_EQUAL_INT(1, parseDeps(json, 10043, "W", NOW, out, 4));
  TEST_ASSERT_EQUAL_INT64(NOW + 240, out[0]);
  TEST_ASSERT_EQUAL_INT(1, parseDeps(json, 30034, "UP", NOW, out, 4));
  TEST_ASSERT_EQUAL_INT(1, parseDeps(json, 30035, "UP", NOW, out, 4));   // "U" stands for "UP"
  TEST_ASSERT_EQUAL_INT(0, parseDeps(json, 30034, "DWN", NOW, out, 4));
}

// ------------------------------------------------------------------ route
// Shaped like RouteDetails/Get/30030, trimmed, with extra fields the filter should drop.
static const char* ROUTE_JSON = R"({
  "Color":"FBB033","TextColor":"000000","ShortName":"30","LongName":"North Amherst / Old Belchertown Rd",
  "Messages":[{"Message":"ignored"}],"Vehicles":[{"VehicleId":1}],
  "Stops":[{"StopId":34,"Name":"Puffton Village Apts"}],
  "RouteStops":[{"Direction":"S","RouteId":30030,"SortOrder":1,"StopId":34}]
})";

static bool parseRoute(const char* json, LiveRoute* r) {
  JsonDocument filter, doc;
  live_buildRouteFilter(filter);
  DeserializationError err = deserializeJson(doc, json, DeserializationOption::Filter(filter));
  TEST_ASSERT_FALSE_MESSAGE(err, err.c_str());
  return live_extractRoute(doc, r);
}

void test_route_badge_fields() {
  LiveRoute r;
  TEST_ASSERT_TRUE(parseRoute(ROUTE_JSON, &r));
  TEST_ASSERT_EQUAL_STRING("30", r.name);
  TEST_ASSERT_EQUAL_STRING("FBB033", r.color);
}

void test_route_filter_keeps_document_small() {
  JsonDocument filter, doc;
  live_buildRouteFilter(filter);
  deserializeJson(doc, ROUTE_JSON, DeserializationOption::Filter(filter));
  TEST_ASSERT_EQUAL_size_t(2, doc.as<JsonObject>().size());   // only ShortName and Color
}

void test_route_missing_or_long_fields() {
  LiveRoute r;
  TEST_ASSERT_FALSE(parseRoute("{}", &r));
  TEST_ASSERT_FALSE(parseRoute(R"({"ShortName":"","Color":"FF0000"})", &r));
  TEST_ASSERT_TRUE(parseRoute(R"({"ShortName":"R41"})", &r));      // colors optional
  TEST_ASSERT_EQUAL_STRING("", r.color);
  TEST_ASSERT_TRUE(parseRoute(R"({"ShortName":"VERYLONGNAME","Color":"FF0000FF"})", &r));
  TEST_ASSERT_EQUAL_STRING("VERYLON", r.name);                       // truncated to fit
  TEST_ASSERT_EQUAL_STRING("FF0000", r.color);
}

int main() {
  UNITY_BEGIN();
  RUN_TEST(test_parseDate_valid);
  RUN_TEST(test_parseDate_malformed);
  RUN_TEST(test_extract_sorted_and_filtered_by_route_and_direction);
  RUN_TEST(test_extract_skips_completed_last_stop_and_past);
  RUN_TEST(test_extract_removes_duplicates);
  RUN_TEST(test_extract_keeps_earliest_when_full);
  RUN_TEST(test_extract_uses_scheduled_time_without_estimate);
  RUN_TEST(test_extract_prefers_estimate_over_schedule);
  RUN_TEST(test_extract_empty_and_odd_responses);
  RUN_TEST(test_extract_other_direction_codes);
  RUN_TEST(test_route_badge_fields);
  RUN_TEST(test_route_filter_keeps_document_small);
  RUN_TEST(test_route_missing_or_long_fields);
  return UNITY_END();
}
