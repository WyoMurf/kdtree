import sys
import os
import warnings
import numpy as np
from astropy.table import Table, join

# Ignore Astropy warnings about unrecognized metadata fields in the ECSV/CSV header
warnings.filterwarnings('ignore')

try:
    from zero_point import zpt
    zpt.load_tables()
except ImportError:
    print("Error: The 'gaiadr3-zeropoint' package is not installed.")
    sys.exit(1)

if len(sys.argv) < 4:
    print("Usage: python3 process_chunk.py <gaia_csv> <astro_csv> <out_filtered_fits>")
    sys.exit(1)

gaia_file = sys.argv[1]
astro_file = sys.argv[2]
out_file = sys.argv[3]

try:
    print(f"  -> Reading Gaia source: {gaia_file}")
    gaia_tb = Table.read(gaia_file, format='ascii.ecsv', fill_values=[('null', '0')], fast_reader=False)
    
    # Filter out stars without a valid parallax early to save memory and join time
    if 'parallax' in gaia_tb.colnames:
        initial_len = len(gaia_tb)
        if hasattr(gaia_tb['parallax'], 'filled'):
            par_data = gaia_tb['parallax'].filled(np.nan)
        else:
            par_data = gaia_tb['parallax']
        valid_mask = ~np.isnan(par_data) & (par_data > 0.0) # Pre-filter for positive parallax
        gaia_tb = gaia_tb[valid_mask]
        print(f"  -> Early filter: kept {len(gaia_tb)}/{initial_len} stars with positive parallax.")
        
    if len(gaia_tb) == 0:
        print("  -> No stars with valid parallax. Skipping.")
        sys.exit(0)

    print(f"  -> Reading Astrophysical parameters: {astro_file}")
    astro_tb = Table.read(astro_file, format='ascii.ecsv', fill_values=[('null', '0')], fast_reader=False)
    
    # Keep only the columns we actually care about to save massive memory and time
    target_cols = [
        'source_id', 'parallax_over_error', 'ruwe', 'spectraltype_esphs', 
        'teff_gspphot', 'logg_gspphot', 'mh_gspphot', 'classprob_dsc_star', 
        'classprob_dsc_whitedwarf', 'classprob_dsc_binarystar', 
        'classprob_dsc_quasar', 'classprob_dsc_galaxy'
    ]
    keep_cols = [c for c in target_cols if c in astro_tb.colnames]
    astro_tb = astro_tb[keep_cols]
    
    print("  -> Joining tables...")
    merged = join(gaia_tb, astro_tb, keys='source_id', join_type='left')
    
    initial_len = len(merged)
    print(f"  -> Merged count: {initial_len}")

    # 1. Filter: parallax_over_error >= 5
    if 'parallax_over_error' in merged.colnames:
        mask_snr = merged['parallax_over_error'] >= 5.0
        mask_snr = np.nan_to_num(mask_snr, nan=False).astype(bool)
    else:
        mask_snr = np.ones(initial_len, dtype=bool)

    # 2. Filter: ruwe <= 1.4
    if 'ruwe' in merged.colnames:
        mask_ruwe = merged['ruwe'] <= 1.4
        mask_ruwe = np.nan_to_num(mask_ruwe, nan=False).astype(bool)
    else:
        mask_ruwe = np.ones(initial_len, dtype=bool)

    valid_mask = mask_snr & mask_ruwe
    merged = merged[valid_mask]
    print(f"  -> Dropped {initial_len - len(merged)} stars due to SNR < 5 or RUWE > 1.4")
    
    if len(merged) == 0:
        print("  -> No stars left after SNR/RUWE filtering. Skipping.")
        sys.exit(0)

    # 3. Calculate and apply Zero-Point
    try:
        gmag = np.array(merged['phot_g_mean_mag'].filled(np.nan)) if hasattr(merged['phot_g_mean_mag'], 'filled') else np.array(merged['phot_g_mean_mag'])
        nueff = np.array(merged['nu_eff_used_in_astrometry'].filled(np.nan)) if hasattr(merged['nu_eff_used_in_astrometry'], 'filled') else np.array(merged['nu_eff_used_in_astrometry'])
        pseudocolour = np.array(merged['pseudocolour'].filled(np.nan)) if hasattr(merged['pseudocolour'], 'filled') else np.array(merged['pseudocolour'])
        ecl_lat = np.array(merged['ecl_lat'].filled(np.nan)) if hasattr(merged['ecl_lat'], 'filled') else np.array(merged['ecl_lat'])
        soltype = np.array(merged['astrometric_params_solved'].filled(31)) if hasattr(merged['astrometric_params_solved'], 'filled') else np.array(merged['astrometric_params_solved'])
        
        soltype = np.where(~np.isin(soltype, [31, 95]), 31, soltype).astype(int)
        
        zp = zpt.get_zpt(gmag, nueff, pseudocolour, ecl_lat, soltype)
        
        original_parallax = np.array(merged['parallax'].filled(np.nan)) if hasattr(merged['parallax'], 'filled') else np.array(merged['parallax'])
        merged['parallax'] = original_parallax - zp
        print("  -> Zero-point correction mathematically applied.")
        
        # 3b. Filter out non-positive parallax after ZP correction
        mask_positive = merged['parallax'] > 0.0
        mask_positive = np.nan_to_num(mask_positive, nan=False).astype(bool)
        initial_count = len(merged)
        merged = merged[mask_positive]
        print(f"  -> Dropped {initial_count - len(merged)} stars with non-positive parallax after ZP correction.")
        
    except Exception as e:
        print(f"  -> Warning: Could not apply zero-point correction: {e}")

    if len(merged) == 0:
        print("  -> No stars left after final positive parallax filter. Skipping.")
        sys.exit(0)

    # 4. Save the highly refined output directly (no giant Merged files ever written!)
    print(f"  -> Writing {out_file}...")
    merged.write(out_file, format='fits', overwrite=True)
    print("  -> Done.")
except Exception as e:
    print(f"Error processing chunk: {e}")
    sys.exit(1)
