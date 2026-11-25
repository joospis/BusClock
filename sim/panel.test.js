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

test("isNight matches the firmware", () => {
  assert.ok(P.isNight(0, 23, 7));
  assert.ok(!P.isNight(7, 23, 7));
  assert.ok(P.isNight(2, 1, 5));
  assert.ok(!P.isNight(3, 6, 6));
});
