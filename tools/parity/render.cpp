// Renders tools/parity/scenarios.json with src/display.h and prints every pixel.
// Built and run by check.sh; render.js does the same with sim/panel.js.
#include <ArduinoJson.h>
#include <fstream>
#include <iostream>
#include <sstream>
#include <vector>
#include "display.h"
#include "logic.h"

struct Frame : Canvas {
  uint16_t px[PANEL_H][PANEL_W] = {};
  void set(int x, int y, uint16_t c) override { px[y][x] = c; }
};

int main(int argc, char** argv) {
  std::ifstream f(argc > 1 ? argv[1] : "scenarios.json");
  std::stringstream ss;
  ss << f.rdbuf();
  JsonDocument doc;
  if (deserializeJson(doc, ss.str())) { std::cerr << "bad scenarios.json\n"; return 1; }
  const time_t now = 1795640000;
  for (JsonObject s : doc.as<JsonArray>()) {
    Frame fr;
    std::string screen = s["screen"] | "main";
    if (screen == "main") {
      std::vector<time_t> deps;
      for (JsonVariant d : s["deps"].as<JsonArray>()) deps.push_back(now + d.as<long>());
      time_t fetched = s["fetchedAgo"].isNull() ? 0 : now - s["fetchedAgo"].as<long>();
      uint32_t bg = 0;
      parseHexColor(s["routeColor"] | "000000", &bg);
      MainInput in = {s["hour"] | 0, s["minute"] | 0, s["route"] | "", rgb565(bg),
                      now, deps.data(), (int)deps.size(), fetched, s["hasStop"] | true, s["walk"] | 4, 180};
      drawMain(fr, mainView(in));
    } else if (screen == "setup") {
      drawSetupScreen(fr, s["ssid"] | "", s["ip"] | "");
    } else {
      drawMessage(fr, s["a"] | "", s["b"].isNull() ? nullptr : (const char*)(s["b"] | ""));
    }
    std::cout << "# " << (s["name"] | "") << "\n";
    char hex[8];
    for (int y = 0; y < PANEL_H; y++) {
      for (int x = 0; x < PANEL_W; x++) { snprintf(hex, sizeof hex, "%04x ", fr.px[y][x]); std::cout << hex; }
      std::cout << "\n";
    }
  }
  return 0;
}
