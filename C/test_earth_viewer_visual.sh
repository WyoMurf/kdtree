#!/bin/bash

# Automated visual smoke test for earth_viewer.
#
# earth_viewer is a GUI app with no interactive test harness -- this is the
# automatable substitute: build it, run it a few times against a real
# city-data directory with the EV_LON/EV_LAT/EV_ALT/EV_SCREENSHOT env vars
# (see main()'s comment in earth_viewer.c) to drive the camera to a few
# known views and capture a screenshot of each, without a human at the
# keyboard. This doesn't assert anything about the *contents* of the
# screenshots -- it's a smoke test (does it build, does it run, does it
# produce non-empty images), not a pixel-diff regression test. Look at the
# output PNGs yourself after running this.
#
# Usage: ./test_earth_viewer_visual.sh [city-data-dir]
#   city-data-dir defaults to ./citydata (relative to this script's
#   directory) and must already contain cities.metatree/cities.manifest/
#   cities.names/city_tile_*.kdtree -- see README-cities.md for how to
#   produce those via geonames2kd + build_city_metatree.

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CITY_DATA_DIR="${1:-$SCRIPT_DIR/citydata}"
# Resolve to an absolute path up front -- the view loop below cd's into this
# directory in a subshell, so a relative path here would resolve against
# the wrong (post-cd) working directory by the time it's used again.
CITY_DATA_DIR="$(cd "$CITY_DATA_DIR" 2>/dev/null && pwd || echo "$CITY_DATA_DIR")"
OUT_DIR="$CITY_DATA_DIR/visual_test_output"

echo "=========================================================="
echo " earth_viewer visual smoke test"
echo "=========================================================="
echo "City data directory: $CITY_DATA_DIR"
echo "Screenshot output:    $OUT_DIR"
echo "----------------------------------------------------------"

if [ ! -f "$CITY_DATA_DIR/cities.metatree" ]; then
    echo "Error: $CITY_DATA_DIR/cities.metatree not found."
    echo "  Build the city data first -- see README-cities.md:"
    echo "    mkdir -p '$CITY_DATA_DIR' && cd '$CITY_DATA_DIR'"
    echo "    curl -O https://download.geonames.org/export/dump/cities1000.zip"
    echo "    unzip cities1000.zip"
    echo "    $SCRIPT_DIR/geonames2kd cities1000.txt"
    echo "    $SCRIPT_DIR/build_city_metatree . cities"
    exit 1
fi

echo "[BUILD] make earth_viewer"
make -C "$SCRIPT_DIR" earth_viewer

mkdir -p "$OUT_DIR"

# name, lon, lat, altitude(km), frame-to-capture-at
# - default: the viewer's own real startup view (no overrides), a
#   "respectful distance" over North America -- confirms the whole globe,
#   texture, and city-dot density render sanely.
# - regional: zoomed into a real coastline/river-valley (NYC area) to check
#   that dot clustering follows real geography, not just "some dots
#   somewhere".
# - labels: close enough that the population-gated label system should
#   kick in -- confirms name lookup and on-screen text placement work.
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
            timeout 15 "$SCRIPT_DIR/earth_viewer" >/dev/null 2>&1 || true

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
