// Renders tools/parity/scenarios.json with sim/panel.js and prints every pixel
// in the same format as render.cpp.
const fs = require("fs");
const path = require("path");
const P = require(path.join(__dirname, "../../sim/panel.js"));
const scenarios = JSON.parse(fs.readFileSync(process.argv[2] || path.join(__dirname, "scenarios.json"), "utf8"));
const now = 1795640000;
let out = "";
for (const s of scenarios) {
  const fr = new P.Frame();
  const screen = s.screen || "main";
  if (screen === "main") {
    const fetched = s.fetchedAgo == null ? 0 : now - s.fetchedAgo;
    const v = P.mainView({ hour: s.hour || 0, minute: s.minute || 0, now, dep: s.deps.map((d) => now + d),
      fetched, hasStop: s.hasStop !== false, walk: s.walk ?? 4, maxAge: 180 });
    P.drawMain(fr, v);
  } else if (screen === "setup") {
    P.drawSetupScreen(fr, s.ssid || "", s.pass || "", s.ip || "");
  } else {
    P.drawMessage(fr, s.a || "", s.b == null ? null : s.b);
  }
  out += "# " + s.name + "\n";
  for (let y = 0; y < P.PANEL_H; y++) {
    let row = "";
    for (let x = 0; x < P.PANEL_W; x++) row += fr.get(x, y).toString(16).padStart(4, "0") + " ";
    out += row + "\n";
  }
}
process.stdout.write(out);
