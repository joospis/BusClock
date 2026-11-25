// panel.js - JavaScript port of src/display.h, src/logic.h and the parts of
// src/live_parse.h the simulator needs. Keep it in step with the C++:
// tools/parity/check.sh renders the same scenarios with both and diffs the pixels.
// Works as a plain <script> (window.BusPanel) and as a Node module.
(function (root) {
  "use strict";
  const PANEL_W = 64, PANEL_H = 32;
  const idiv = (a, b) => Math.trunc(a / b);   // C integer division

  const RGB565 = (r, g, b) => (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)) & 0xFFFF;
  const C = {
    CLOCK: RGB565(0xDD, 0xDD, 0xDD),
    LABEL: RGB565(0xDD, 0xDD, 0xDD),
    LINE: RGB565(0x33, 0x33, 0x33),
    CHEVRON: RGB565(0x77, 0x77, 0x77),
    CHEV_OLD: RGB565(0x2A, 0x2A, 0x2A),
    NEXT: RGB565(0x99, 0x99, 0x99),
    DIM: RGB565(0x77, 0x77, 0x77),
    GREEN: RGB565(0x33, 0xEE, 0x55),
    AMBER: RGB565(0xFF, 0xAA, 0x00),
    RED: RGB565(0xFF, 0x33, 0x33),
  };

  // ---------------------------------------------------------------- font (3x5)
  const FONT_CHARS = " !-.:0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  const FONT = [
    [0,0,0,0,0],[2,2,2,0,2],[0,0,7,0,0],[0,0,0,0,2],[0,2,0,2,0],
    [7,5,5,5,7],[2,6,2,2,7],[7,1,7,4,7],[7,1,7,1,7],[5,5,7,1,1],
    [7,4,7,1,7],[7,4,7,5,7],[7,1,2,2,2],[7,5,7,5,7],[7,5,7,1,7],
    [2,5,7,5,5],[6,5,6,5,6],[3,4,4,4,3],[6,5,5,5,6],[7,4,6,4,7],[7,4,6,4,4],
    [3,4,5,5,3],[5,5,7,5,5],[7,2,2,2,7],[1,1,1,5,2],[5,5,6,5,5],[4,4,4,4,7],
    [5,7,7,5,5],[6,5,5,5,5],[2,5,5,5,2],[6,5,6,4,4],[2,5,5,6,3],[6,5,6,5,5],
    [3,4,2,1,6],[7,2,2,2,2],[5,5,5,5,7],[5,5,5,5,2],[5,5,7,7,5],[5,5,2,5,5],
    [5,5,2,2,2],[7,1,2,4,7],
  ];
  const CHEVRON = [0x48, 0x24, 0x12, 0x24, 0x48];

  // A 64x32 RGB565 framebuffer, 0 = off.
  class Frame {
    constructor() { this.px = new Uint16Array(PANEL_W * PANEL_H); }
    set(x, y, c) { this.px[y * PANEL_W + x] = c; }
    get(x, y) { return this.px[y * PANEL_W + x]; }
    clear() { this.px.fill(0); }
  }

  function px(cv, x, y, c) {
    if (x >= 0 && x < PANEL_W && y >= 0 && y < PANEL_H) cv.set(x, y, c);
  }

  function textWidth(s, k) {
    const n = s.length;
    return n ? n * 4 * k - k : 0;
  }

  function drawText(cv, x, y, s, c, k) {
    for (const raw of s) {
      const ch = raw >= "a" && raw <= "z" ? raw.toUpperCase() : raw;
      let g = FONT_CHARS.indexOf(ch);
      if (g < 0) g = 0;
      for (let r = 0; r < 5; r++)
        for (let col = 0; col < 3; col++)
          if (FONT[g][r] & (4 >> col))
            for (let dy = 0; dy < k; dy++)
              for (let dx = 0; dx < k; dx++) px(cv, x + col * k + dx, y + r * k + dy, c);
      x += 4 * k;
    }
  }

  function drawCentered(cv, y, s, c, k) {
    drawText(cv, idiv(PANEL_W - textWidth(s, k), 2), y, s, c, k);
  }

  // ---------------------------------------------------------------- main screen
  function urgencyColor(mins, walk) {
    if (mins < walk) return C.RED;
    if (mins < walk + 3) return C.AMBER;
    return C.GREEN;
  }

  // in: {hour, minute, now, dep: [epoch s...], fetched (0 = never), hasStop, walk, maxAge}
  function mainView(inp) {
    const v = {};
    const h12 = inp.hour % 12 ? inp.hour % 12 : 12;
    v.clock = h12 + ":" + String(inp.minute).padStart(2, "0");
    v.fresh = !!inp.fetched && inp.now - inp.fetched <= inp.maxAge;
    const n = inp.dep.length;

    if (!inp.hasStop) { v.big = "NO"; v.unit = "STOP"; v.bigColor = C.DIM; }
    else if (!inp.fetched) { v.big = "--"; v.unit = ""; v.bigColor = C.DIM; }
    else if (n === 0 && !v.fresh) { v.big = "NO"; v.unit = "DATA"; v.bigColor = C.DIM; }
    else if (n === 0) { v.big = "NO"; v.unit = "BUS"; v.bigColor = C.DIM; }
    else {
      const m = idiv(inp.dep[0] - inp.now, 60);
      if (m < 100) { v.big = String(m); v.unit = "MIN"; v.bigColor = urgencyColor(m, inp.walk); }
      else { v.big = String(Math.min(idiv(m + 30, 60), 99)); v.unit = "HR"; v.bigColor = C.GREEN; }
    }

    v.nextColor = C.NEXT;
    v.chevColor = v.fresh ? C.CHEVRON : C.CHEV_OLD;
    if (inp.hasStop && n >= 2) {
      const m2 = idiv(inp.dep[1] - inp.now, 60);
      v.next = m2 < 100 ? m2 + "M" : Math.min(idiv(m2 + 30, 60), 99) + "H";
    } else {
      v.next = "--";
    }
    return v;
  }

  function drawMain(cv, v) {
    drawCentered(cv, 1, v.clock, C.CLOCK, 2);
    for (let x = 0; x < PANEL_W; x++) px(cv, x, 12, C.LINE);
    for (let y = 13; y < PANEL_H; y++) px(cv, 44, y, C.LINE);

    const gw = textWidth(v.big, 3) + 4 + textWidth(v.unit, 1);
    const gx = idiv(44 - gw, 2);
    drawText(cv, gx, 15, v.big, v.bigColor, 3);
    drawText(cv, gx + textWidth(v.big, 3) + 4, 25, v.unit, C.LABEL, 1);

    for (let r = 0; r < 5; r++)
      for (let c = 0; c < 7; c++)
        if (CHEVRON[r] & (0x40 >> c)) px(cv, 51 + c, 16 + r, v.chevColor);
    drawText(cv, 45 + idiv(19 - textWidth(v.next, 1), 2), 24, v.next, v.nextColor, 1);
  }

  function drawSetupScreen(cv, ssid, pass, ip) {
    drawCentered(cv, 2, "WIFI SETUP", C.AMBER, 1);
    drawCentered(cv, 10, ssid, C.CLOCK, 1);
    drawCentered(cv, 18, "PW " + pass, C.CLOCK, 1);
    drawCentered(cv, 26, ip, C.NEXT, 1);
  }

  function drawMessage(cv, a, b) {
    drawCentered(cv, b ? 9 : 13, a, C.CLOCK, 1);
    if (b) drawCentered(cv, 18, b, C.NEXT, 1);
  }

  // ---------------------------------------------------------------- logic.h
  function isNight(hour, start, end) {
    if (start === end) return false;
    if (start < end) return hour >= start && hour < end;
    return hour >= start || hour < end;
  }

  // ---------------------------------------------------------------- live_parse.h
  function parseDate(s) {
    const m = /Date\((-?\d+)/.exec(s || "");
    if (!m) return 0;
    const ms = Number(m[1]);
    return ms > 0 ? Math.floor(ms / 1000) : 0;
  }

  // StopDepartures response -> sorted upcoming departure times (epoch s)
  function extractDepartures(json, routeId, dir, now, maxOut) {
    const out = [];
    for (const stop of json || [])
      for (const rd of (stop && stop.RouteDirections) || []) {
        if (rd.RouteId !== routeId || !(rd.DirectionCode || "").startsWith(dir)) continue;
        for (const d of rd.Departures || []) {
          if (d.IsCompleted || d.IsLastStopOnTrip) continue;
          const t = parseDate(d.EDT) || parseDate(d.SDT);
          if (!t || t < now || out.includes(t)) continue;
          out.push(t);
        }
      }
    out.sort((a, b) => a - b);
    return out.slice(0, maxOut);
  }

  // RouteDetails response -> [{id, dir, name, toward}] in route order, minus each direction's last stop
  function extractStops(json) {
    const names = new Map((json.Stops || []).map((s) => [s.StopId, s.Name || ""]));
    const rs = (json.RouteStops || [])
      .filter((r) => r.Direction === "N" || r.Direction === "S")
      .sort((a, b) => a.SortOrder - b.SortOrder);
    const toward = {};
    for (const r of rs) toward[r.Direction] = names.get(r.StopId) || "";
    const lastIdx = {};
    rs.forEach((r, i) => (lastIdx[r.Direction] = i));
    return rs
      .filter((r, i) => i !== lastIdx[r.Direction] && names.get(r.StopId))
      .map((r) => ({ id: r.StopId, dir: r.Direction, name: names.get(r.StopId).slice(0, 40), toward: toward[r.Direction] }));
  }

  // RGB565 -> [r, g, b] 0-255
  function toRGB(c) {
    const r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    return [(r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)];
  }

  const api = {
    PANEL_W, PANEL_H, C, Frame, textWidth, drawText, drawCentered, urgencyColor, mainView,
    drawMain, drawSetupScreen, drawMessage, isNight, parseDate, extractDepartures, extractStops, toRGB,
  };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else root.BusPanel = api;
})(this);
