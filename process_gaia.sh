#!/bin/bash

# Define working directory
BASE_DIR="/backup/star-catalogs"
cd "$BASE_DIR" || exit 1

# Define base URLs
GAIA_URL="https://cdn.gea.esac.esa.int/Gaia/gdr3/gaia_source/"
ASTRO_URL="https://cdn.gea.esac.esa.int/Gaia/gdr3/Astrophysical_parameters/astrophysical_parameters/"

# Generate the Python merge script
cat << 'EOF' > convert_and_merge.py
import sys
import warnings
import numpy as np
from astropy.table import Table, join

# Ignore Astropy warnings about unrecognized metadata fields in the ECSV/CSV header
warnings.filterwarnings('ignore')

gaia_file = sys.argv[1]
astro_file = sys.argv[2]
out_file = sys.argv[3]

print(f"Processing chunk: {gaia_file} + {astro_file}")

try:
    print("  -> Reading Gaia source...")
    # Astropy natively handles .csv.gz and ECSV headers
    gaia_tb = Table.read(gaia_file, format='ascii.ecsv', fill_values=[('null', '0')], fast_reader=False) 
    
    # Filter out stars without a valid parallax
    if 'parallax' in gaia_tb.colnames:
        initial_len = len(gaia_tb)
        # Handle both MaskedColumn and standard Column arrays
        if hasattr(gaia_tb['parallax'], 'filled'):
            par_data = gaia_tb['parallax'].filled(np.nan)
        else:
            par_data = gaia_tb['parallax']
            
        valid_mask = ~np.isnan(par_data)
        gaia_tb = gaia_tb[valid_mask]
        print(f"  -> Filtered out {initial_len - len(gaia_tb)} stars lacking parallax.")
        print(f"  -> Keeping {len(gaia_tb)} stars with resolvable 3D coordinates.")
    
    print("  -> Reading Astrophysical parameters...")
    astro_tb = Table.read(astro_file, format='ascii.ecsv', fill_values=[('null', '0')], fast_reader=False)
    
    # Columns to extract (note: 'ruwe' is the correct field name for Renormalised Unit Weight Error)
    target_cols = [
        'source_id', 'parallax_over_error', 'ruwe', 'spectraltype_esphs', 
        'teff_gspphot', 'logg_gspphot', 'mh_gspphot', 'classprob_dsc_star', 
        'classprob_dsc_whitedwarf', 'classprob_dsc_binarystar', 
        'classprob_dsc_quasar', 'classprob_dsc_galaxy'
    ]
    
    # Keep only the columns that actually exist to prevent KeyErrors
    keep_cols = [c for c in target_cols if c in astro_tb.colnames]
    astro_tb = astro_tb[keep_cols]
    
    print("  -> Joining tables...")
    # Left join ensures all original stars are kept; missing astro params become NaN/null
    merged = join(gaia_tb, astro_tb, keys='source_id', join_type='left')
    
    print(f"  -> Writing {out_file}...")
    # Writing as .fits.gz natively invokes cfitsio's transparent GZIP compression
    merged.write(out_file, format='fits', overwrite=True)
    print("  -> Done.")
except Exception as e:
    print(f"Error processing {gaia_file}: {e}")
EOF

echo "Starting Gaia processing pipeline..."

# Read through the MD5 list to get every Gaia file name
# The second column contains the file name (e.g., GaiaSource_000000-003111.csv.gz)
awk '{print $2}' Gaia_source_MD5SUM.txt | while read -r gaia_file; do
    
    # Ignore empty lines
    [ -z "$gaia_file" ] && continue

    # Extract the chunk range identifier to find the matching astrophysical file
    chunk=$(echo "$gaia_file" | sed 's/GaiaSource_//' | sed 's/\.csv\.gz//')
    astro_file="AstrophysicalParameters_${chunk}.csv.gz"
    out_file="GaiaSource_Merged_${chunk}.fits.gz"

    # Skip if we already successfully created the FITS file
    if [ -f "$out_file" ]; then
        echo "Skipping $out_file (already exists)."
        continue
    fi

    # Verify file integrity and delete if corrupted
    if [ -f "$gaia_file" ] && ! gzip -t "$gaia_file" 2>/dev/null; then
        echo "Corrupted $gaia_file detected. Deleting to re-download..."
        rm -f "$gaia_file"
    fi
    if [ -f "$astro_file" ] && ! gzip -t "$astro_file" 2>/dev/null; then
        echo "Corrupted $astro_file detected. Deleting to re-download..."
        rm -f "$astro_file"
    fi

    # 1. Download GaiaSource if missing
    if [ ! -f "$gaia_file" ]; then
        echo "Downloading $gaia_file..."
        wget -q -c "${GAIA_URL}${gaia_file}" -O "$gaia_file"
    fi

    # 2. Download AstrophysicalParameters if missing
    if [ ! -f "$astro_file" ]; then
        echo "Downloading $astro_file..."
        wget -q -c "${ASTRO_URL}${astro_file}" -O "$astro_file"
    fi

    # 3. Merge and Convert using Python
    /usr/bin/python3 convert_and_merge.py "$gaia_file" "$astro_file" "$out_file"

    # Optional: If you are critically low on disk space and want to delete the raw CSVs 
    # after they are successfully converted to FITS, you could uncomment the lines below:
    if [ -f "$out_file" ]; then
       rm -f "$gaia_file" "$astro_file"
    fi
done

echo "All tasks completed!"
