# BusClock: PVTA Route 30 countdown sign

ESP32 + 64×32 P5 HUB75 panel. Shows the time, minutes until the next Route 30 bus at your stop, and the bus after that.

Everything comes from PVTA's bus tracker (`bustracker.pvta.com/InfoPoint/rest/`): departures for your stop (`StopDepartures/Get/<stop>`), and the route's stop list (`RouteDetails/Get/30030`), which is reloaded daily. No timetable is stored on the ESP32, so schedule changes show up without re-flashing. The clock makes at most one request every 30 seconds. If the tracker can't be reached, it keeps counting down to the departures it last received and dims the **>>** icon after 3 minutes. The default stop is Puffton Village Apts, southbound (toward Old Belchertown Rd).

```
platformio.ini            build, flash and test settings
src/main.cpp              the firmware: Wi-Fi, web server, fetching, wiring it together
src/display.h             everything drawn on the panel (pure C++, unit-tested)
src/live_parse.h          reads the PVTA bus tracker responses (pure C++, unit-tested)
src/logic.h               stop keys, night window (pure C++, unit-tested)
src/web_page.h            the setup web page
test/                     native unit tests (Unity) and captured PVTA responses
sim/index.html            browser simulator for the panel
sim/panel.js              JavaScript port of display.h used by the simulator
tools/parity/             checks sim/panel.js draws exactly what display.h draws
```

## 1. Install

Open this folder in VS Code with the PlatformIO extension, or from a terminal:

```
pio run -t upload        # build and flash (libraries are fetched automatically)
pio device monitor       # serial log at 115200
```

## 2. Wiring (classic ESP32 dev board)

| Panel (HUB75) | ESP32 GPIO | | Panel (HUB75) | ESP32 GPIO |
|---|---|---|---|---|
| R1 | 25 | | A | 23 |
| G1 | 26 | | B | 19 |
| B1 | 27 | | C | 5 |
| R2 | 14 | | D | 17 |
| G2 | 12 | | LAT | 4 |
| B2 | 13 | | OE | 15 |
| CLK | 16 | | GND | GND |

E is unused on a 1/16-scan 64×32 panel. Power the panel from the 5 V supply, not the ESP32, and connect supply ground to ESP32 GND. To use different pins, change the `PIN_` lines at the top of the sketch.

GPIO 12 is a boot-strapping pin. If the ESP32 won't boot while the panel is connected, move G2 to another free pin (for example 32) and update `PIN_G2`.

## 3. Set up

1. Power on. The panel shows **JOIN WIFI** and the network name **BUSCLOCK**.
2. On your phone, join the open Wi-Fi network **BusClock** (no password). The setup page opens automatically; if it doesn't, go to http://192.168.4.1.
3. Choose your home network and enter its password. Set your walk time and brightness, then tap **Save**.
4. The clock restarts and joins your Wi-Fi. Open **http://busclock.local** from your home network and choose your stop and direction (the stop list comes from the bus tracker, so it only appears once the clock is online). Use the same page to change settings later. If that address doesn't resolve on your device, use the IP address shown on the status page.

Hold the ESP32's **BOOT** button for 3 seconds to reopen the setup network. If the clock can't reach your Wi-Fi for 10 minutes, it reopens the setup network on its own and keeps counting down to the last departures it received.

**Colors:** green means you have time, amber means leave now, and red means you'd have to run. The thresholds are based on your walk time. **HR** means the next bus is more than 99 minutes away. **NO BUS** means the bus tracker lists no upcoming Route 30 buses at your stop (for example, overnight). **NO DATA** means the tracker hasn't been reachable and every bus it last reported has left. **--** means the first response hasn't arrived yet.

To use a different route, change `ROUTE_ID` (InfoPoint's route id, e.g. 30030 for Route 30) and `ROUTE_NAME` at the top of the sketch.

## 4. Tests

```
pio test -e native              # C++ unit tests for display.h, live_parse.h, logic.h
node --test sim/panel.test.js   # simulator's JS parsing, against real PVTA responses
sh tools/parity/check.sh        # simulator and firmware draw identical pixels
```

The parity check renders every scenario in `tools/parity/scenarios.json` with both `src/display.h` and `sim/panel.js` and diffs the pixels. If you change how the panel looks, change both files, then run it. To cover a new case, add a scenario.

## 5. Simulator

Open `sim/index.html` in a browser (double-clicking the file works; no server needed). It draws the panel with the same code and colors as the firmware.

- **Live**: real departures from the PVTA bus tracker for any stop on the route, polled every 30 seconds like the clock. Tick **Simulate offline** to watch the **>>** dim and the countdown keep going on cached data.
- **Manual**: set the next two buses, data age and clock time by hand to check colors and edge cases.
- **Other screens**: Wi-Fi setup, connecting and clock-sync screens.

The walk time and brightness sliders work like the setup page's.
