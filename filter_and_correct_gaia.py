#!/usr/bin/env python3
import sys
import os
import glob
import warnings
import numpy as np
from astropy.table import Table

# Ignore minor FITS warnings
warnings.filterwarnings('ignore')

try:
    from zero_point import zpt
    # Pre-load the calibration tables required for the calculation
    zpt.load_tables()
except ImportError:
    print("Error: The 'gaiadr3-zeropoint' package is not installed.")
    print("Please install it by running: pip install gaiadr3-zeropoint")
    sys.exit(1)

def process_file(in_file):
    out_file = in_file.replace('Merged', 'Filtered')
    if os.path.exists(out_file):
        print(f"Skipping {out_file} (already exists).")
        return
        
    print(f"Processing {in_file} ...")
    try:
        tb = Table.read(in_file)
    except Exception as e:
        print(f"  -> Error reading file: {e}")
        return
        
    initial_len = len(tb)
    if initial_len == 0:
        print("  -> File is empty. Skipping.")
        return

    # 1. Filter: parallax_over_error >= 5
    if 'parallax_over_error' in tb.colnames:
        mask_snr = tb['parallax_over_error'] >= 5.0
        # Convert any NaNs to False so they get dropped
        mask_snr = np.nan_to_num(mask_snr, nan=False).astype(bool)
    else:
        mask_snr = np.ones(initial_len, dtype=bool)

    # 2. Filter: ruwe <= 1.4
    if 'ruwe' in tb.colnames:
        mask_ruwe = tb['ruwe'] <= 1.4
        mask_ruwe = np.nan_to_num(mask_ruwe, nan=False).astype(bool)
    else:
        mask_ruwe = np.ones(initial_len, dtype=bool)

    # Apply both filters simultaneously
    valid_mask = mask_snr & mask_ruwe
    tb = tb[valid_mask]
    
    print(f"  -> Dropped {initial_len - len(tb)} rows due to SNR < 5 or RUWE > 1.4")
    print(f"  -> Keeping {len(tb)} high-quality stars.")
    
    if len(tb) == 0:
        print("  -> No stars left after filtering. Skipping FITS write.")
        return

    # 3. Calculate and subtract Zero-Point
    try:
        # Extract the necessary arrays, ensuring we don't pass Astropy MaskedArrays to the zpt function
        gmag = np.array(tb['phot_g_mean_mag'].filled(np.nan)) if hasattr(tb['phot_g_mean_mag'], 'filled') else np.array(tb['phot_g_mean_mag'])
        nueff = np.array(tb['nu_eff_used_in_astrometry'].filled(np.nan)) if hasattr(tb['nu_eff_used_in_astrometry'], 'filled') else np.array(tb['nu_eff_used_in_astrometry'])
        pseudocolour = np.array(tb['pseudocolour'].filled(np.nan)) if hasattr(tb['pseudocolour'], 'filled') else np.array(tb['pseudocolour'])
        ecl_lat = np.array(tb['ecl_lat'].filled(np.nan)) if hasattr(tb['ecl_lat'], 'filled') else np.array(tb['ecl_lat'])
        soltype = np.array(tb['astrometric_params_solved'].filled(31)) if hasattr(tb['astrometric_params_solved'], 'filled') else np.array(tb['astrometric_params_solved'])
        
        # Ensure soltype is strictly 31 or 95 to prevent zero-point calculation crashes
        soltype = np.where(~np.isin(soltype, [31, 95]), 31, soltype).astype(int)
        
        # Calculate the exact zero-point offset for each star
        zp = zpt.get_zpt(gmag, nueff, pseudocolour, ecl_lat, soltype)
        
        # Apply the correction (the official documentation states: true_parallax = catalog_parallax - zero_point)
        original_parallax = np.array(tb['parallax'].filled(np.nan)) if hasattr(tb['parallax'], 'filled') else np.array(tb['parallax'])
        tb['parallax'] = original_parallax - zp
        
        print("  -> Zero-point correction mathematically applied.")
    except Exception as e:
        print(f"  -> Warning: Could not apply zero-point correction: {e}")
        
    # 4. Save the highly refined output
    print(f"  -> Writing {out_file}...")
    tb.write(out_file, format='fits', overwrite=True)
    print("  -> Done.\n")

if __name__ == "__main__":
    # If the user passed specific files as arguments, process those. Otherwise, process everything in the directory.
    if len(sys.argv) > 1:
        files = sys.argv[1:]
    else:
        files = sorted(glob.glob("/backup/star-catalogs/GaiaSource_Merged_*.fits.gz"))
        
    if not files:
        print("No 'GaiaSource_Merged_*.fits.gz' files found in /backup/star-catalogs/")
        sys.exit(0)
        
    print(f"Found {len(files)} files to filter and correct.")
    for f in files:
        process_file(f)
        
    print("All filtering and zero-point corrections complete!")