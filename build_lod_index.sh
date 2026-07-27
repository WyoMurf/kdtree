#!/bin/bash

# Steps 2 and 3 of the pipeline (after run_pipeline.sh has produced your
# GaiaSource_Filtered_*.kdtree shard files):
#   2) Annotate every shard with a .kdtree.lod sidecar (kd2lod), so the
#      viewer can cull/collapse subtrees of stars instead of drawing
#      everything.
#   3) Build the meta-index over all shards (build_metatree) and annotate
#      the meta-tree itself the same way (kd2lod again), so the viewer can
#      lazily mmap only the shards actually in view instead of loading
#      everything up front.
#
# Run from the data directory itself, after run_pipeline.sh.
#
# Only the parallax-segment shards (*-0.kdtree .. *-9.kdtree) are used, not
# the bare per-chunk "full" trees - the segments already partition the same
# stars, so that's all build_metatree indexes and all the viewer ever opens;
# annotating the full trees too would just be wasted time and disk space.

MAX_THREADS=$(nproc 2>/dev/null || echo 4)
BASE_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)

# Looks in the current directory, this script's directory, and that
# directory's C/ subdir - deliberately no hardcoded absolute-path fallback,
# since that could silently pick up a stale/unrelated build on someone
# else's machine instead of failing with a clear message.
find_tool() {
    local name="$1"
    for candidate in "./$name" "${BASE_DIR}/$name" "${BASE_DIR}/C/$name"; do
        if [ -x "$candidate" ]; then
            echo "$candidate"
            return 0
        fi
    done
    return 1
}

KD2LOD=$(find_tool kd2lod)
if [ -z "$KD2LOD" ]; then
    echo "Error: kd2lod not found (looked in ., ${BASE_DIR}, ${BASE_DIR}/C)."
    echo "Build it first: (cd ${BASE_DIR}/C && make kd2lod)"
    exit 1
fi

BUILD_METATREE=$(find_tool build_metatree)
if [ -z "$BUILD_METATREE" ]; then
    echo "Error: build_metatree not found (looked in ., ${BASE_DIR}, ${BASE_DIR}/C)."
    echo "Build it first: (cd ${BASE_DIR}/C && make build_metatree)"
    exit 1
fi

echo "=========================================================="
echo " Step 2: Annotating shard files with LOD data (kd2lod)"
echo "=========================================================="

shard_count=$(find . -maxdepth 1 -name "*-[0-9].kdtree" | wc -l)
if [ "$shard_count" -eq 0 ]; then
    echo "Error: no GaiaSource_Filtered_*-N.kdtree shard files found in $(pwd)."
    echo "Run the download/build pipeline (run_pipeline.sh) first."
    exit 1
fi

echo "Found $shard_count shard files. Annotating with $MAX_THREADS parallel workers..."
lod_log=$(mktemp)
find . -maxdepth 1 -name "*-[0-9].kdtree" | xargs -P "$MAX_THREADS" -I{} "$KD2LOD" {} > "$lod_log" 2>&1
lod_ok=$(grep -c "^Wrote " "$lod_log")
echo "LOD sidecars written: $lod_ok / $shard_count"
if [ "$lod_ok" -ne "$shard_count" ]; then
    echo "Warning: not all shards produced a .lod file - see $lod_log for details."
else
    rm -f "$lod_log"
fi

echo "=========================================================="
echo " Step 3: Building the meta-index (build_metatree + kd2lod)"
echo "=========================================================="

"$BUILD_METATREE" . catalog
if [ ! -s catalog.metatree ]; then
    echo "Error: build_metatree did not produce catalog.metatree."
    exit 1
fi

"$KD2LOD" catalog.metatree catalog.metatree.lod
if [ ! -s catalog.metatree.lod ]; then
    echo "Error: kd2lod did not produce catalog.metatree.lod."
    exit 1
fi

echo "=========================================================="
echo " Done. catalog.metatree / catalog.manifest / catalog.metatree.lod ready."
echo " Run ./viewer from $(pwd) to fly through it."
echo "=========================================================="
