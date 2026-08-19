#!/bin/bash

# Automated visual smoke test for the Rust starviewer, same convention as
# viewers/go/starviewer/test_viewer_visual.sh: build it, run it against a
# real star-catalog directory with the SV_CAM_*/SV_TARGET_*/SV_SCREENSHOT
# env vars (see main.rs) to drive the camera to a known view and capture a
# screenshot, without a human at the keyboard. This doesn't assert anything
# about the *contents* of the screenshot -- it's a smoke test (does it
# build, does it run, does it produce a non-empty image), not a pixel-diff
# regression test. Look at the output PNG yourself after running this.
#
# Unlike earthviewer's citydata (checked into the repo), a real star
# catalog is tens of GB and lives outside the repo (see README-stars.md),
# so there's no in-repo default -- the catalog directory is a required
# argument.
#
# Usage: ./test_viewer_visual.sh <star-catalog-dir>
#   star-catalog-dir must already contain catalog.metatree/
#   catalog.metatree.lod/catalog.manifest/*.kdtree(.lod) -- see
#   README-stars.md for how to produce those via fits2kd + build_metatree +
#   kd2lod.
#
# Cold shard reads on a slow disk can make the first several frames take
# seconds each (see shard.rs's Shard/ensure_shard_loaded comment) -- this
# script budgets a generous timeout accordingly.

set -e

if [ -z "$1" ]; then
    echo "Usage: $0 <star-catalog-dir>"
    exit 1
fi

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CATALOG_DIR="$(cd "$1" 2>/dev/null && pwd || echo "$1")"
OUT_DIR="$CATALOG_DIR/visual_test_output_rust"
BIN="$SCRIPT_DIR/../target/debug/starviewer"

echo "=========================================================="
echo " starviewer (Rust) visual smoke test"
echo "=========================================================="
echo "Star catalog directory: $CATALOG_DIR"
echo "Screenshot output:       $OUT_DIR"
echo "----------------------------------------------------------"

if [ ! -f "$CATALOG_DIR/catalog.metatree" ]; then
    echo "Error: $CATALOG_DIR/catalog.metatree not found."
    echo "  Build the star catalog first -- see README-stars.md."
    exit 1
fi

echo "[BUILD] cargo build -p starviewer"
(cd "$SCRIPT_DIR/.." && cargo build -p starviewer --quiet)

mkdir -p "$OUT_DIR"

png="default.png"
echo "[RUN] view=default (Sun/Earth, looking toward +Z)"
(
    cd "$CATALOG_DIR"
    rm -f "$png"
    SV_SCREENSHOT="$png" SV_SCREENSHOT_FRAME=30 timeout 120 "$BIN" >/dev/null 2>&1 || true

    if [ -s "$png" ]; then
        mv "$png" "$OUT_DIR/$png"
    else
        echo "  -> FAILED: no screenshot produced"
        exit 1
    fi
)

echo "----------------------------------------------------------"
echo "Done. Review the screenshot yourself:"
echo "  $OUT_DIR/$png"
echo "=========================================================="
