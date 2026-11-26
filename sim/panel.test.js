// Node tests for the simulator's data handling (sim/panel.js), using real
// PVTA responses captured on 2026-11-25. Drawing is covered by tools/parity.
// Run: node --test sim/
const test = require("node:test");
const assert = require("node:assert");
const path = require("path");
const P = require("./panel.js");
const fixture = (f) => require(path.join(__dirname, "../test/fixtures", f));

const DEPS = fixture("stop_departures_34.json");
const ROUTE = fixture("route_details_30030.json");
const CAPTURED = 1795641768;   // LastUpdated in the departures fixture

test("parseDate", () => {
  assert.strictEqual(P.parseDate("/Date(1795643100000-0500)/"), 1795643100);
  assert.strictEqual(P.parseDate("/Date(-2208910500000-0500)/"), 0);
  assert.strictEqual(P.parseDate(null), 0);
  assert.strictEqual(P.parseDate("2026-11-25"), 0);
});

test("departures from the real response", () => {
  const s = P.extractDepartures(DEPS, 30030, "S", CAPTURED, 8);
  assert.strictEqual(s.length, 5);
  assert.deepStrictEqual(s, [...s].sort((a, b) => a - b));
  assert.strictEqual(s[0], 1795643100);                  // 4:45 PM
  assert.deepStrictEqual(P.extractDepartures(DEPS, 30030, "N", CAPTURED, 8), []);
  assert.deepStrictEqual(P.extractDepartures(DEPS, 30031, "S", CAPTURED, 8), []);
  assert.strictEqual(P.extractDepartures(DEPS, 30030, "S", CAPTURED, 2).length, 2);
  assert.strictEqual(P.extractDepartures(DEPS, 30030, "S", s[4] + 1, 8).length, 0);  // all left
});

test("departures skip completed, last-stop, past and duplicates; fall back to SDT", () => {
  const d = (t, extra = {}) => ({ EDT: `/Date(${t * 1000}-0500)/`, SDT: `/Date(${t * 1000}-0500)/`, ...extra });
  const json = [{ RouteDirections: [{ RouteId: 30030, DirectionCode: "S", Departures: [
    d(100, { IsCompleted: true }), d(200, { IsLastStopOnTrip: true }), d(50), d(300), d(300),
    { EDT: null, SDT: "/Date(400000-0500)/" },
  ] }] }];
  assert.deepStrictEqual(P.extractDepartures(json, 30030, "S", 60, 8), [300, 400]);
});

test("stops from the real response", () => {
  const stops = P.extractStops(ROUTE);
  assert.strictEqual(stops.length, 39);   // 41 route stops minus the last of each direction
  assert.deepStrictEqual(stops[0], { id: 34, dir: "S", name: "Puffton Village Apts", toward: "Old Belchertown Road" });
  assert.ok(stops.every((s) => s.name && (s.dir === "N" || s.dir === "S")));
  assert.ok(stops.filter((s) => s.dir === "N").every((s) => s.toward === "Puffton Village Apts"));
  assert.ok(!stops.some((s) => s.dir === "S" && s.name === "Old Belchertown Road"));
});

test("chooseScreen matches logic.h", () => {
  const normal = { apActive: false, forceSetup: false, hasSavedWifi: true, setupClient: false, connected: true, timeValid: true, offlineMs: 0, ms: 0 };
  assert.strictEqual(P.chooseScreen(normal), "main");
  const down = { ...normal, connected: false, offlineMs: 10000 };
  const seen = { main: 0, wifiOffline: 0 };
  for (let ms = 100000; ms < 160000; ms += 500) seen[P.chooseScreen({ ...down, ms })]++;
  assert.deepStrictEqual(seen, { main: 80, wifiOffline: 40 });
  assert.strictEqual(P.chooseScreen({ ...down, offlineMs: 9999, ms: 5000 }), "main");
  assert.strictEqual(P.chooseScreen({ ...down, apActive: true, ms: 1000 }), "main");
  assert.strictEqual(P.chooseScreen({ ...down, apActive: true, setupClient: true }), "setup");
  assert.strictEqual(P.chooseScreen({ ...normal, apActive: true, hasSavedWifi: false }), "setup");
  assert.strictEqual(P.chooseScreen({ ...normal, timeValid: false }), "settingClock");
  assert.strictEqual(P.chooseScreen({ ...down, timeValid: false }), "wifiOffline");
});

test("hex colors and RGB565 match logic.h / display.h", () => {
  assert.strictEqual(P.parseHexColor("FBB033"), 0xfbb033);
  assert.strictEqual(P.parseHexColor("#00467e"), 0x00467e);
  for (const bad of [null, "", "#", "FBB03", "FBB0333", "GBB033", "0xFBB0"]) assert.strictEqual(P.parseHexColor(bad), null);
  assert.strictEqual(P.rgb565(0xfbb033), P.RGB565(0xfb, 0xb0, 0x33));
  assert.strictEqual(P.rgb565(0xffffff), 0xffff);
  assert.strictEqual(P.rgb565(0xff0000), 0xf800);
  assert.strictEqual(P.badgeWidth("30"), 13);
  assert.strictEqual(P.badgeWidth("G101"), 21);
});

test("every real route name fits the badge", () => {
  const routes = fixture("visible_routes.json");
  assert.ok(routes.length > 40);
  for (const r of routes) {
    assert.ok(r.ShortName.length >= 1 && r.ShortName.length <= 4, r.ShortName);
    assert.notStrictEqual(P.parseHexColor(r.Color), null, r.ShortName + " color " + r.Color);
    const bg = P.rgb565(P.parseHexColor(r.Color));
    const black = /^0+$/.test(r.Color);
    assert.strictEqual(P.badgeTextColor(bg), black ? P.C.CLOCK : 0, r.ShortName + " " + r.Color);   // cut out unless black
  }
  const r30 = routes.find((r) => r.RouteId === 30030);
  assert.deepStrictEqual([r30.ShortName, r30.Color], ["30", "FBB033"]);
});

test("other direction codes (E/W routes, campus shuttles)", () => {
  const b43 = fixture("route_details_10043.json");
  const stops = P.extractStops(b43);
  assert.ok(stops.length > 40);
  assert.deepStrictEqual([...new Set(stops.map((s) => s.dir))].sort(), ["E", "W"]);
  const blank = { Stops: [{ StopId: 1, Name: "A" }, { StopId: 2, Name: "B" }, { StopId: 3, Name: "C" }],
    RouteStops: [{ Direction: "UP", StopId: 1, SortOrder: 1 }, { Direction: "UP", StopId: 2, SortOrder: 2 },
      { Direction: "", StopId: 3, SortOrder: 3 }] };
  assert.deepStrictEqual(P.extractStops(blank).map((s) => s.id + ":" + s.dir), ["1:UP"]);   // blank skipped
  assert.ok(P.dirMatches("U", "UP") && P.dirMatches("E", "E"));
  assert.ok(!P.dirMatches("UP", "U") && !P.dirMatches("CW", "CCW") && !P.dirMatches("", "S"));
  const json = [{ RouteDirections: [{ RouteId: 30034, DirectionCode: "UP", Departures: [{ EDT: "/Date(900000-0500)/" }] }] }];
  assert.deepStrictEqual(P.extractDepartures(json, 30034, "UP", 0, 8), [900]);
  assert.deepStrictEqual(P.extractDepartures(json, 30034, "DWN", 0, 8), []);
});

test("isNight matches the firmware", () => {
  assert.ok(P.isNight(0, 23, 7));
  assert.ok(!P.isNight(7, 23, 7));
  assert.ok(P.isNight(2, 1, 5));
  assert.ok(!P.isNight(3, 6, 6));
});
