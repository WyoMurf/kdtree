#!/bin/bash

# Automated visual smoke test for the Julia earthviewer, mirroring
# C/test_earth_viewer_visual.sh and viewers/go/earthviewer's own copy:
# run it a few times against a real city-data directory with the
# EV_LON/EV_LAT/EV_ALT/EV_SCREENSHOT env vars (see main.jl) to drive the
# camera to a few known views and capture a screenshot of each, without a
# human at the keyboard. This doesn't assert anything about the *contents*
# of the screenshots -- it's a smoke test (does it run, does it produce
# non-empty images), not a pixel-diff regression test. Look at the output
# PNGs yourself after running this.
#
# Usage: ./test_earth_viewer_visual.sh [city-data-dir]
#   city-data-dir defaults to C/citydata (relative to the repo root) and
#   must already contain cities.metatree/cities.manifest/cities.names/
#   city_tile_*.kdtree -- see README-cities.md for how to produce those via
#   geonames2kd + build_city_metatree.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
JULIA_PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
CITY_DATA_DIR="${1:-$REPO_ROOT/C/citydata}"
CITY_DATA_DIR="$(cd "$CITY_DATA_DIR" 2>/dev/null && pwd || echo "$CITY_DATA_DIR")"
OUT_DIR="$CITY_DATA_DIR/visual_test_output_julia"

echo "=========================================================="
echo " earthviewer (Julia) visual smoke test"
echo "=========================================================="
echo "City data directory: $CITY_DATA_DIR"
echo "Screenshot output:    $OUT_DIR"
echo "----------------------------------------------------------"

if [ ! -f "$CITY_DATA_DIR/cities.metatree" ]; then
    echo "Error: $CITY_DATA_DIR/cities.metatree not found."
    echo "  Build the city data first -- see README-cities.md."
    exit 1
fi

mkdir -p "$OUT_DIR"

# name, lon, lat, altitude(km), frame-to-capture-at -- same three views as
# the C smoke test and the Go/Rust ports', for a like-for-like comparison.
VIEWS=(
    "default::::30"
    "regional:-74.006:40.7128:300:45"
    "labels:-74.006:40.7128:40:45"
)

for view in "${VIEWS[@]}"; do
    IFS=':' read -r name lon lat alt frame <<< "$view"
    png="$name.png"
    echo "[RUN] view=$name lon=${lon:-<default>} lat=${lat:-<default>} altitude=${alt:-<default>}km"

    (
        cd "$CITY_DATA_DIR"
        rm -f "$png"
        env \
            ${lon:+EV_LON="$lon"} \
            ${lat:+EV_LAT="$lat"} \
            ${alt:+EV_ALT="$alt"} \
            EV_SCREENSHOT="$png" \
            EV_SCREENSHOT_FRAME="$frame" \
            timeout 30 julia --project="$JULIA_PROJECT_DIR" "$SCRIPT_DIR/main.jl" >/dev/null 2>&1 || true

        if [ -s "$png" ]; then
            mv "$png" "$OUT_DIR/$png"
        else
            echo "  -> FAILED: no screenshot produced for view '$name'"
            exit 1
        fi
    )
done

echo "----------------------------------------------------------"
echo "Done. Review the screenshots yourself:"
for view in "${VIEWS[@]}"; do
    IFS=':' read -r name _ <<< "$view"
    echo "  $OUT_DIR/$name.png"
done
echo "=========================================================="
