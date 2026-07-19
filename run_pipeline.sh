#!/bin/bash

# Master pipeline runner
MAX_THREADS=3
# Spawns up to $MAX_THREADS worker threads in parallel
$MAX_THREADS
# Dynamically resolve directory of this script
BASE_DIR=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" &> /dev/null && pwd)
cd "$BASE_DIR" || exit 1

# Make worker script executable
chmod +x "${BASE_DIR}/process_chunk_shell.sh"

# Clear old progress logs
rm -f "${BASE_DIR}/progress.log" "${BASE_DIR}/pipeline_start.txt"

# Find Gaia MD5 SUM file dynamically (local or fallback)
MD5_FILE="/backup/star-catalogs/Gaia_source_MD5SUM.txt"
if [ ! -f "$MD5_FILE" ] && [ -f "./Gaia_source_MD5SUM.txt" ]; then
    MD5_FILE="./Gaia_source_MD5SUM.txt"
elif [ ! -f "$MD5_FILE" ] && [ -f "${BASE_DIR}/Gaia_source_MD5SUM.txt" ]; then
    MD5_FILE="${BASE_DIR}/Gaia_source_MD5SUM.txt"
fi

if [ ! -f "$MD5_FILE" ]; then
    wget https://cdn.gea.esac.esa.int/Gaia/gdr3/gaia_source/Gaia_source_MD5SUM.txt 
fi

# Generate chunks list -- the '$d' deletes the last line, which is the MD5 file
chunks_file="${BASE_DIR}/chunks.txt"
awk '{print $2}' "$MD5_FILE" | sed 's/GaiaSource_//' | sed 's/\.csv\.gz//' | sed '$d' > "$chunks_file"

total_chunks=$(wc -l < "$chunks_file")
echo "=========================================================="
echo " Starting Parallel Gaia Pipeline ($MAX_THREADS Workers)"
echo "=========================================================="
echo "Total Chunks to process: $total_chunks"
echo "Target Directory:        $BASE_DIR"
echo "MD5 Index File:          $MD5_FILE"
echo "----------------------------------------------------------"

# Record the pipeline start time globally
pipeline_start_time=$(date +%s)
echo "$pipeline_start_time" > "${BASE_DIR}/pipeline_start.txt"

# Run in parallel using GNU xargs!
cat "$chunks_file" | xargs -I {} -P $MAX_THREADS "${BASE_DIR}/process_chunk_shell.sh" "{}"

# Calculate and record total elapsed time
pipeline_end_time=$(date +%s)
total_elapsed=$((pipeline_end_time - pipeline_start_time))
eh=$((total_elapsed / 3600))
em=$(((total_elapsed % 3600) / 60))
es=$((total_elapsed % 60))
elapsed_str=$(printf "%02d:%02d:%02d" $eh $em $es)

# Clean up global start time file
rm -f "${BASE_DIR}/pipeline_start.txt"

echo "=========================================================="
echo " All Parallel Pipeline tasks completed!"
echo " Total pipeline execution time: $elapsed_str"
echo "=========================================================="
