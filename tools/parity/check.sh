#!/bin/sh
# Check that the browser simulator (sim/panel.js) draws exactly what the
# firmware (src/display.h) draws, pixel for pixel. Needs a C++ compiler and Node.
# Run from anywhere:  sh tools/parity/check.sh
set -e
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
OUT=$(mktemp -d)
trap 'rm -rf "$OUT"' EXIT

AJ="$ROOT/.pio/libdeps/native/ArduinoJson/src"
if [ ! -d "$AJ" ]; then AJ="$ROOT/.pio/libdeps/esp32dev/ArduinoJson/src"; fi
if [ ! -d "$AJ" ]; then echo "ArduinoJson not found; run 'pio test -e native' once first" >&2; exit 2; fi

c++ -std=c++17 -I"$ROOT/src" -I"$AJ" "$HERE/render.cpp" -o "$OUT/render"
"$OUT/render" "$HERE/scenarios.json" > "$OUT/cpp.txt"
node "$HERE/render.js" "$HERE/scenarios.json" > "$OUT/js.txt"

if cmp -s "$OUT/cpp.txt" "$OUT/js.txt"; then
  echo "parity OK: $(grep -c '^#' "$OUT/cpp.txt") scenarios match"
else
  echo "parity FAILED: sim/panel.js and src/display.h disagree" >&2
  diff "$OUT/cpp.txt" "$OUT/js.txt" | head -20 >&2
  exit 1
fi
