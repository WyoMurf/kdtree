#!/bin/bash

# Idempotent KD-tree builder for Gaia Filtered FITS files
# Author: Gemini CLI / Murf
# Date: Saturday, July 11, 2026

BASE_DIR="/backup/star-catalogs"
CONVERTER="/home/murf/kdtree/C/fits2kd"

echo "=========================================================="
echo " Starting Idempotent Gaia KD-Tree Batch Generation"
echo "=========================================================="
echo "Source Directory: $BASE_DIR"
echo "Converter Binary: $CONVERTER"
echo "----------------------------------------------------------"

# Ensure the converter binary exists and is executable
if [ ! -x "$CONVERTER" ]; then
    echo "Error: Converter binary not found or not executable at $CONVERTER"
    exit 1
fi

# Find all GaiaSource_Filtered_*.fits.gz files
files=("$BASE_DIR"/GaiaSource_Filtered_*.fits.gz)

# If no files found matching the pattern, exit
if [ ! -e "${files[0]}" ]; then
    echo "No GaiaSource_Filtered_*.fits.gz files found in $BASE_DIR"
    exit 0
fi

total_files=${#files[@]}
processed=0
skipped=0
failed=0

echo "Found $total_files FITS files to process."
echo ""

for fits_file in "${files[@]}"; do
    # Extract filename without directory
    filename=$(basename "$fits_file")
    
    # Determine output .kdtree filename
    # Remove .fits.gz and append .kdtree
    kdtree_file="${fits_file%.fits.gz}.kdtree"
    kdtree_filename=$(basename "$kdtree_file")
    
    # Check if .kdtree file already exists and is non-empty
    if [ -s "$kdtree_file" ]; then
        echo "[SKIP] $kdtree_filename already exists."
        ((skipped++))
    else
        echo "[BUILD] Converting $filename -> $kdtree_filename..."
        start_time=$(date +%s.%N)
        
        "$CONVERTER" "$fits_file" "$kdtree_file"
        status=$?
        
        if [ $status -eq 0 ]; then
            end_time=$(date +%s.%N)
            elapsed=$(echo "$end_time - $start_time" | bc 2>/dev/null || echo "unknown")
            if [ "$elapsed" != "unknown" ]; then
                echo "  -> Completed in $(printf "%.3f" "$elapsed")s"
            else
                echo "  -> Completed successfully."
            fi
            ((processed++))
        else
            echo "  -> Error: fits2kd failed with exit code $status"
            # Remove any partial file created
            rm -f "$kdtree_file"
            ((failed++))
        fi
    fi
done

echo "----------------------------------------------------------"
echo "Batch processing finished!"
echo "Total found: $total_files"
echo "Converted:   $processed"
echo "Skipped:     $skipped"
echo "Failed:      $failed"
echo "=========================================================="
