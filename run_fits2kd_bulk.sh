#!/usr/bin/env bash

# Bulk runner for fits2kd over all Gaia FITS files in the star-catalogs backup directory
CATALOG_DIR="/backup/star-catalogs"
FITS2KD_BIN="/home/murf/kdtree/C/fits2kd"

# Check if the fits2kd binary exists
if [ ! -f "$FITS2KD_BIN" ]; then
    echo "Error: fits2kd binary not found at $FITS2KD_BIN. Attempting to build it..."
    (cd "$(dirname "$FITS2KD_BIN")" && make fits2kd)
    if [ ! -f "$FITS2KD_BIN" ]; then
        echo "Error: Failed to compile fits2kd."
        exit 1
    fi
fi

# Ensure target catalog directory exists
if [ ! -d "$CATALOG_DIR" ]; then
    echo "Error: Catalog directory $CATALOG_DIR does not exist."
    exit 1
fi

echo "=========================================================="
echo " Starting Bulk fits2kd Conversion"
echo " Target Directory: $CATALOG_DIR"
echo " Binary:           $FITS2KD_BIN"
echo "=========================================================="

# Find and sort all filtered FITS files
files=($(find "$CATALOG_DIR" -maxdepth 1 -name "GaiaSource_Filtered_*.fits.gz" | sort))
total_files=${#files[@]}

if [ $total_files -eq 0 ]; then
    echo "No matching GaiaSource_Filtered_*.fits.gz files found in $CATALOG_DIR"
    exit 0
fi

echo "Found $total_files FITS files to process."
echo "----------------------------------------------------------"

count=0
start_time=$(date +%s)

for f in "${files[@]}"; do
    count=$((count + 1))
    
    # Construct output kdtree name by replacing .fits.gz with .kdtree
    out_file="${f%.fits.gz}.kdtree"
    
    echo "[$count / $total_files] Converting: $(basename "$f")"
    echo "  -> Output base: $(basename "$out_file")"
    
    # Run the fits2kd converter
    "$FITS2KD_BIN" "$f" "$out_file"
    status=$?
    
    if [ $status -ne 0 ]; then
        echo "[ERROR] fits2kd conversion failed for $f"
    fi
    echo "----------------------------------------------------------"
done

end_time=$(date +%s)
duration=$((end_time - start_time))
h=$((duration / 3600))
m=$(((duration % 3600) / 60))
s=$((duration % 60))

echo "=========================================================="
echo " Bulk fits2kd Conversion Completed!"
echo " Total files processed: $count"
echo " Total elapsed time:    $(printf "%02d:%02d:%02d" $h $m $s)"
echo "=========================================================="
