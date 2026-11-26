// panel.js - JavaScript port of src/display.h, src/logic.h and the parts of
// src/live_parse.h the simulator needs. Keep it in step with the C++:
// tools/parity/check.sh renders the same scenarios with both and diffs the pixels.
// Works as a plain <script> (window.BusPanel) and as a Node module.
(function (root) {
  "use strict";
  const PANEL_W = 64, PANEL_H = 32, BADGE_H = 11, BADGE_PAD = 3, BADGE_GAP = 3, CLOCK_MAX_W = 38, BADGE_DARK = 0x40;
  const idiv = (a, b) => Math.trunc(a / b);   // C integer division

  const RGB565 = (r, g, b) => (((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)) & 0xFFFF;
  const rgb565 = (rgb) => RGB565((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);

  // "FBB033" or "#fbb033" -> 0xFBB033, or null unless exactly 6 hex digits (logic.h parseHexColor)
  function parseHexColor(s) {
    if (typeof s !== "string") return null;
    if (s[0] === "#") s = s.slice(1);
    return /^[0-9a-fA-F]{6}$/.test(s) ? parseInt(s, 16) : null;
  }
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

  // in: {hour, minute, route, routeColor, now, dep: [epoch s...], fetched (0 = never), hasStop, walk, maxAge}
  // route is the badge text ("" for none); routeColor is RGB565.
  function mainView(inp) {
    const v = {};
    const h12 = inp.hour % 12 ? inp.hour % 12 : 12;
    v.clock = h12 + ":" + String(inp.minute).padStart(2, "0");
    v.route = (inp.route || "").slice(0, 4);
    v.routeColor = inp.routeColor || 0;
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

  function badgeWidth(route) {
    return Math.max(BADGE_H, textWidth(route, 1) + 2 * BADGE_PAD);
  }

  function badgeTextColor(bg) {
    const r = ((bg >> 11) & 0x1f) << 3, g = ((bg >> 5) & 0x3f) << 2, b = (bg & 0x1f) << 3;
    return Math.max(r, g, b) < BADGE_DARK ? C.CLOCK : 0;
  }

  function drawBadge(cv, x0, route, bg) {
    const fg = badgeTextColor(bg);
    const w = badgeWidth(route);
    for (let y = 0; y < BADGE_H; y++)
      for (let x = 0; x < w; x++) px(cv, x0 + x, y, bg);
    drawText(cv, x0 + idiv(w - textWidth(route, 1), 2), idiv(BADGE_H - 5, 2), route, fg, 1);
  }

  function drawMain(cv, v) {
    if (v.route) {
      const bw = badgeWidth(v.route);
      const x0 = idiv(PANEL_W - (bw + BADGE_GAP + CLOCK_MAX_W), 2);
      drawBadge(cv, x0, v.route, v.routeColor);
      const cell = x0 + bw + BADGE_GAP;
      drawText(cv, cell + idiv(CLOCK_MAX_W - textWidth(v.clock, 2), 2), 1, v.clock, C.CLOCK, 2);
    } else {
      drawCentered(cv, 1, v.clock, C.CLOCK, 2);
    }
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

  function fitScale(s) {
    return textWidth(s, 2) <= PANEL_W ? 2 : 1;
  }

  function drawFitted(cv, y, s, c) {
    const k = fitScale(s);
    drawCentered(cv, k === 2 ? y : y + 2, s, c, k);
  }

  function drawSetupScreen(cv, ssid, ip) {
    drawCentered(cv, 2, "JOIN WIFI", C.AMBER, 1);
    drawFitted(cv, 10, ssid, C.CLOCK);
    drawCentered(cv, 25, ip, C.NEXT, 1);
  }

  function drawMessage(cv, a, b) {
    if (!b) { drawFitted(cv, 11, a, C.CLOCK); return; }
    drawFitted(cv, 4, a, C.CLOCK);
    drawFitted(cv, 18, b, C.NEXT);
  }

  // ---------------------------------------------------------------- logic.h
  function isNight(hour, start, end) {
    if (start === end) return false;
    if (start < end) return hour >= start && hour < end;
    return hour >= start || hour < end;
  }

  const OFFLINE_FLASH_MAIN_MS = 4000, OFFLINE_FLASH_OFFLINE_MS = 2000, OFFLINE_FLASH_GRACE_MS = 10000;

  // s: {apActive, forceSetup, hasSavedWifi, setupClient, connected, timeValid, offlineMs, ms}
  // Returns "main" | "setup" | "settingClock" | "wifiOffline", like chooseScreen() in logic.h.
  function chooseScreen(s) {
    if (s.apActive && (!s.hasSavedWifi || s.forceSetup || s.setupClient)) return "setup";
    if (!s.timeValid) return s.connected ? "settingClock" : "wifiOffline";
    if (!s.connected && s.offlineMs >= OFFLINE_FLASH_GRACE_MS &&
        s.ms % (OFFLINE_FLASH_MAIN_MS + OFFLINE_FLASH_OFFLINE_MS) >= OFFLINE_FLASH_MAIN_MS)
      return "wifiOffline";
    return "main";
  }

  // ---------------------------------------------------------------- live_parse.h
  function parseDate(s) {
    const m = /Date\((-?\d+)/.exec(s || "");
    if (!m) return 0;
    const ms = Number(m[1]);
    return ms > 0 ? Math.floor(ms / 1000) : 0;
  }

  // logic.h dirMatches: exact, or a one-letter code standing for a longer one ("U" for "UP")
  function dirMatches(code, dir) {
    if (!code || !dir) return false;
    return code === dir || (code.length === 1 && code === dir[0]);
  }

  // StopDepartures response -> sorted upcoming departure times (epoch s)
  function extractDepartures(json, routeId, dir, now, maxOut) {
    const out = [];
    for (const stop of json || [])
      for (const rd of (stop && stop.RouteDirections) || []) {
        if (rd.RouteId !== routeId || !dirMatches(rd.DirectionCode || "", dir)) continue;
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
      .filter((r) => /^[A-Z]{1,4}$/.test(r.Direction || ""))
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
    PANEL_W, PANEL_H, C, Frame, dirMatches, RGB565, rgb565, parseHexColor, badgeWidth, badgeTextColor, textWidth, drawText, drawCentered, urgencyColor, mainView,
    drawMain, drawSetupScreen, drawMessage, fitScale, isNight, chooseScreen, parseDate, extractDepartures, extractStops, toRGB,
  };
  if (typeof module !== "undefined" && module.exports) module.exports = api;
  else root.BusPanel = api;
})(this);
