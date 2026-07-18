#!/bin/bash

# Worker script to process a single Gaia chunk
# Usage: ./process_chunk_shell.sh <chunk_range>

CHUNK="$1"
if [ -z "$CHUNK" ]; then
    echo "Error: No chunk range provided."
    exit 1
fi

# Dynamically resolve directory of this script
BASE_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)

GAIA_URL="https://cdn.gea.esac.esa.int/Gaia/gdr3/gaia_source/"
ASTRO_URL="https://cdn.gea.esac.esa.int/Gaia/gdr3/Astrophysical_parameters/astrophysical_parameters/"

# Auto-detect local fits2kd or fallback to global path
CONVERTER="/home/murf/kdtree/C/fits2kd"
if [ -f "./fits2kd" ]; then
    CONVERTER="./fits2kd"
elif [ -f "${BASE_DIR}/fits2kd" ]; then
    CONVERTER="${BASE_DIR}/fits2kd"
elif [ -f "${BASE_DIR}/C/fits2kd" ]; then
    CONVERTER="${BASE_DIR}/C/fits2kd"
fi

# Auto-detect local csv2fits or fallback to global path
CSV2FITS="/home/murf/kdtree/C/csv2fits"
if [ -f "./csv2fits" ]; then
    CSV2FITS="./csv2fits"
elif [ -f "${BASE_DIR}/csv2fits" ]; then
    CSV2FITS="${BASE_DIR}/csv2fits"
elif [ -f "${BASE_DIR}/C/csv2fits" ]; then
    CSV2FITS="${BASE_DIR}/C/csv2fits"
fi

# Auto-detect local python environment or fallback to system python
PYTHON="/home/murf/gaia_env/bin/python3"
if [ ! -x "$PYTHON" ]; then
    PYTHON="python3"
fi

GAIA_FILE="GaiaSource_${CHUNK}.csv.gz"
ASTRO_FILE="AstrophysicalParameters_${CHUNK}.csv.gz"
OUT_FILTERED="GaiaSource_Filtered_${CHUNK}.fits.gz"
OUT_KDTREE="GaiaSource_Filtered_${CHUNK}.kdtree"

# Check if both final files already exist and are non-empty
if [ -s "${BASE_DIR}/${OUT_FILTERED}" ] && [ -s "${BASE_DIR}/${OUT_KDTREE}" ]; then
    # Skip
    exit 0
fi

echo "[START] Processing chunk $CHUNK..."
start_time=$(date +%s)

# 1. Download Gaia file if missing or corrupted
if [ ! -f "${BASE_DIR}/${GAIA_FILE}" ] || ! gzip -t "${BASE_DIR}/${GAIA_FILE}" 2>/dev/null; then
    rm -f "${BASE_DIR}/${GAIA_FILE}"
    wget -q -c "${GAIA_URL}${GAIA_FILE}" -O "${BASE_DIR}/${GAIA_FILE}"
fi

# 2. Download Astro file if missing or corrupted
if [ ! -f "${BASE_DIR}/${ASTRO_FILE}" ] || ! gzip -t "${BASE_DIR}/${ASTRO_FILE}" 2>/dev/null; then
    rm -f "${BASE_DIR}/${ASTRO_FILE}"
    wget -q -c "${ASTRO_URL}${ASTRO_FILE}" -O "${BASE_DIR}/${ASTRO_FILE}"
fi

# 3. Merge, filter using C program, then apply ZP correction in Python
"$CSV2FITS" "${BASE_DIR}/${GAIA_FILE}" "${BASE_DIR}/${ASTRO_FILE}" "${BASE_DIR}/${OUT_FILTERED}"
status=$?

if [ $status -ne 0 ]; then
    echo "[ERROR] C csv2fits failed for chunk $CHUNK"
    rm -f "${BASE_DIR}/${OUT_FILTERED}" "${BASE_DIR}/${GAIA_FILE}" "${BASE_DIR}/${ASTRO_FILE}"
    exit 1
fi

"$PYTHON" "${BASE_DIR}/apply_zeropoint.py" "${BASE_DIR}/${OUT_FILTERED}"
status=$?

if [ $status -ne 0 ]; then
    echo "[ERROR] Python zero-point correction failed for chunk $CHUNK"
    rm -f "${BASE_DIR}/${OUT_FILTERED}" "${BASE_DIR}/${GAIA_FILE}" "${BASE_DIR}/${ASTRO_FILE}"
    exit 1
fi

# Deleting downloaded raw CSV files instantly to save massive disk space!
rm -f "${BASE_DIR}/${GAIA_FILE}" "${BASE_DIR}/${ASTRO_FILE}"

# 4. Generate .kdtree file
if [ -s "${BASE_DIR}/${OUT_FILTERED}" ]; then
    "$CONVERTER" "${BASE_DIR}/${OUT_FILTERED}" "${BASE_DIR}/${OUT_KDTREE}"
    status=$?
    if [ $status -ne 0 ]; then
        echo "[ERROR] fits2kd failed for chunk $CHUNK"
        rm -f "${BASE_DIR}/${OUT_KDTREE}"
        exit 1
    fi
fi

# Calculate and record run duration for active chunk
end_time=$(date +%s)
elapsed=$((end_time - start_time))
echo "$elapsed" >> "${BASE_DIR}/progress.log"

# Calculate and display professional real-time progress HUD
completed_count=$(find "${BASE_DIR}" -maxdepth 1 -name "*.kdtree" | wc -l)
total_chunks=$(wc -l < "${BASE_DIR}/chunks.txt")
remaining_chunks=$((total_chunks - completed_count))
[ $remaining_chunks -lt 0 ] && remaining_chunks=0

C_active=$(wc -l < "${BASE_DIR}/progress.log")
sum_elapsed=$(awk '{sum+=$1} END {print sum}' "${BASE_DIR}/progress.log")

if [ $C_active -gt 0 ]; then
    avg_active=$(echo "scale=2; $sum_elapsed / $C_active" | bc)
    parallel_avg=$(echo "scale=2; $avg_active / 10" | bc)
    remaining_seconds=$(echo "$remaining_chunks * $parallel_avg" | bc | cut -d'.' -f1)
    
    # Format remaining seconds to HH:MM:SS
    h=$((remaining_seconds / 3600))
    m=$(((remaining_seconds % 3600) / 60))
    s=$((remaining_seconds % 60))
    time_str=$(printf "%02d:%02d:%02d" $h $m $s)
else
    avg_active="Calculating..."
    parallel_avg="Calculating..."
    time_str="Calculating..."
fi

echo "=========================================================="
echo "[PROGRESS] Chunks completed: $completed_count / $total_chunks"
echo "[PROGRESS] Running Avg Chunk: ${avg_active}s (Effective: ${parallel_avg}s)"
echo "[PROGRESS] Estimated time remaining: $time_str"
echo "=========================================================="
