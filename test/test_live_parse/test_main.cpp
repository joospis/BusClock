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

static int parseDeps(const std::string& json, long route, char dir, time_t now, time_t* out, int maxOut) {
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
  int n = parseDeps(json, 30030, 'S', NOW, out, 8);
  TEST_ASSERT_EQUAL_INT(3, n);
  TEST_ASSERT_EQUAL_INT64(NOW + 300, out[0]);
  TEST_ASSERT_EQUAL_INT64(NOW + 600, out[1]);
  TEST_ASSERT_EQUAL_INT64(NOW + 900, out[2]);

  n = parseDeps(json, 30030, 'N', NOW, out, 8);
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
  int n = parseDeps(json, 30030, 'S', NOW, out, 8);
  TEST_ASSERT_EQUAL_INT(2, n);
  TEST_ASSERT_EQUAL_INT64(NOW, out[0]);
  TEST_ASSERT_EQUAL_INT64(NOW + 400, out[1]);
}

void test_extract_removes_duplicates() {
  std::string json = response(routeDir(30030, "S", dep(NOW + 300) + "," + dep(NOW + 300) + "," + dep(NOW + 600)));
  time_t out[8];
  TEST_ASSERT_EQUAL_INT(2, parseDeps(json, 30030, 'S', NOW, out, 8));
}

void test_extract_keeps_earliest_when_full() {
  std::string json = response(routeDir(30030, "S",
      dep(NOW + 500) + "," + dep(NOW + 400) + "," + dep(NOW + 300) + "," + dep(NOW + 200) + "," + dep(NOW + 100)));
  time_t out[2];
  int n = parseDeps(json, 30030, 'S', NOW, out, 2);
  TEST_ASSERT_EQUAL_INT(2, n);
  TEST_ASSERT_EQUAL_INT64(NOW + 100, out[0]);
  TEST_ASSERT_EQUAL_INT64(NOW + 200, out[1]);
}

void test_extract_uses_scheduled_time_without_estimate() {
  std::string json = response(routeDir(30030, "S", dep(-1, false, false, NOW + 700)));
  time_t out[4];
  int n = parseDeps(json, 30030, 'S', NOW, out, 4);
  TEST_ASSERT_EQUAL_INT(1, n);
  TEST_ASSERT_EQUAL_INT64(NOW + 700, out[0]);
}

void test_extract_prefers_estimate_over_schedule() {
  std::string json = response(routeDir(30030, "S", dep(NOW + 900, false, false, NOW + 600)));  // running late
  time_t out[4];
  TEST_ASSERT_EQUAL_INT(1, parseDeps(json, 30030, 'S', NOW, out, 4));
  TEST_ASSERT_EQUAL_INT64(NOW + 900, out[0]);
}

void test_extract_empty_and_odd_responses() {
  time_t out[4];
  TEST_ASSERT_EQUAL_INT(0, parseDeps("[]", 30030, 'S', NOW, out, 4));
  TEST_ASSERT_EQUAL_INT(0, parseDeps(response(routeDir(30030, "S", "")), 30030, 'S', NOW, out, 4));
  TEST_ASSERT_EQUAL_INT(0, parseDeps("[{\"RouteDirections\":null}]", 30030, 'S', NOW, out, 4));
}

// ------------------------------------------------------------------ stops
// Shaped like RouteDetails/Get/30030, trimmed, with extra fields the filter should drop.
static const char* ROUTE_JSON = R"({
  "Color":"FBB033","ShortName":"30","Messages":[{"Message":"ignored"}],"Vehicles":[{"VehicleId":1}],
  "Stops":[
    {"Description":"x","StopId":34,"Name":"Puffton Village Apts","Latitude":42.4},
    {"StopId":35,"Name":"Puffton Village / North Pleasant Street"},
    {"StopId":148,"Name":"Old Belchertown Road"},
    {"StopId":8002,"Name":"Amherst Dog Park"},
    {"StopId":999,"Name":""}
  ],
  "RouteStops":[
    {"Direction":"N","RouteId":30030,"SortOrder":22,"StopId":148},
    {"Direction":"S","RouteId":30030,"SortOrder":1,"StopId":34},
    {"Direction":"S","RouteId":30030,"SortOrder":3,"StopId":148},
    {"Direction":"N","RouteId":30030,"SortOrder":41,"StopId":34},
    {"Direction":"S","RouteId":30030,"SortOrder":2,"StopId":35},
    {"Direction":"N","RouteId":30030,"SortOrder":23,"StopId":8002},
    {"Direction":"N","RouteId":30030,"SortOrder":24,"StopId":999},
    {"Direction":"X","RouteId":30030,"SortOrder":5,"StopId":35}
  ]
})";

static void parseRoute(JsonDocument& doc) {
  JsonDocument filter;
  live_buildStopsFilter(filter);
  DeserializationError err = deserializeJson(doc, ROUTE_JSON, DeserializationOption::Filter(filter));
  TEST_ASSERT_FALSE_MESSAGE(err, err.c_str());
}

void test_stops_in_route_order_without_final_stops() {
  JsonDocument doc;
  parseRoute(doc);
  LiveStop s[16];
  int n = live_extractStops(doc, s, 16);
  // S: 34(1) 35(2) [148(3) last, dropped]   N: 148(22) 8002(23) [34(41) last, dropped]
  // 999 has no name and "X" isn't a direction, so both are skipped.
  TEST_ASSERT_EQUAL_INT(4, n);
  TEST_ASSERT_EQUAL_UINT32(34, s[0].id);   TEST_ASSERT_EQUAL_CHAR('S', s[0].dir);
  TEST_ASSERT_EQUAL_STRING("Puffton Village Apts", s[0].name);
  TEST_ASSERT_EQUAL_UINT32(35, s[1].id);   TEST_ASSERT_EQUAL_CHAR('S', s[1].dir);
  TEST_ASSERT_EQUAL_UINT32(148, s[2].id);  TEST_ASSERT_EQUAL_CHAR('N', s[2].dir);
  TEST_ASSERT_EQUAL_UINT32(8002, s[3].id); TEST_ASSERT_EQUAL_CHAR('N', s[3].dir);
  TEST_ASSERT_EQUAL_STRING("Amherst Dog Park", s[3].name);
}

void test_stops_respects_max() {
  JsonDocument doc;
  parseRoute(doc);
  LiveStop s[2];
  int n = live_extractStops(doc, s, 2);
  TEST_ASSERT_TRUE(n <= 2);
}

void test_stops_long_names_are_truncated() {
  std::string longName(80, 'A');
  std::string json = "{\"Stops\":[{\"StopId\":1,\"Name\":\"" + longName + "\"},{\"StopId\":2,\"Name\":\"End\"}],"
                     "\"RouteStops\":[{\"Direction\":\"S\",\"SortOrder\":1,\"StopId\":1},"
                     "{\"Direction\":\"S\",\"SortOrder\":2,\"StopId\":2}]}";
  JsonDocument doc;
  deserializeJson(doc, json);
  LiveStop s[4];
  TEST_ASSERT_EQUAL_INT(1, live_extractStops(doc, s, 4));
  TEST_ASSERT_EQUAL_size_t(LIVE_NAME_LEN - 1, strlen(s[0].name));
}

void test_toward_names() {
  JsonDocument doc;
  parseRoute(doc);
  TEST_ASSERT_EQUAL_STRING("Old Belchertown Road", live_towardName(doc, 'S'));
  TEST_ASSERT_EQUAL_STRING("Puffton Village Apts", live_towardName(doc, 'N'));
  JsonDocument empty;
  deserializeJson(empty, "{}");
  TEST_ASSERT_EQUAL_STRING("", live_towardName(empty, 'S'));
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
  RUN_TEST(test_stops_in_route_order_without_final_stops);
  RUN_TEST(test_stops_respects_max);
  RUN_TEST(test_stops_long_names_are_truncated);
  RUN_TEST(test_toward_names);
  return UNITY_END();
}
