import sys
import os
import warnings
import numpy as np
from astropy.table import Table

# Ignore minor FITS/Astropy warnings
warnings.filterwarnings('ignore')

try:
    from zero_point import zpt
    zpt.load_tables()
except ImportError:
    print("Error: The 'gaiadr3-zeropoint' package is not installed.")
    sys.exit(1)

if len(sys.argv) < 2:
    print("Usage: python3 apply_zeropoint.py <fits_file>")
    sys.exit(1)

fits_file = sys.argv[1]

try:
    tb = Table.read(fits_file)
    if len(tb) == 0:
        print("FITS table is empty.")
        sys.exit(0)
        
    gmag = np.array(tb['phot_g_mean_mag'].filled(np.nan)) if hasattr(tb['phot_g_mean_mag'], 'filled') else np.array(tb['phot_g_mean_mag'])
    nueff = np.array(tb['nu_eff_used_in_astrometry'].filled(np.nan)) if hasattr(tb['nu_eff_used_in_astrometry'], 'filled') else np.array(tb['nu_eff_used_in_astrometry'])
    pseudocolour = np.array(tb['pseudocolour'].filled(np.nan)) if hasattr(tb['pseudocolour'], 'filled') else np.array(tb['pseudocolour'])
    ecl_lat = np.array(tb['ecl_lat'].filled(np.nan)) if hasattr(tb['ecl_lat'], 'filled') else np.array(tb['ecl_lat'])
    soltype = np.array(tb['astrometric_params_solved'].filled(31)) if hasattr(tb['astrometric_params_solved'], 'filled') else np.array(tb['astrometric_params_solved'])
    
    soltype = np.where(~np.isin(soltype, [31, 95]), 31, soltype).astype(int)
    
    # Calculate exact zero-point offset
    zp = zpt.get_zpt(gmag, nueff, pseudocolour, ecl_lat, soltype)
    
    # Apply zero-point subtraction (true = catalog - zero_point)
    original_parallax = np.array(tb['parallax'].filled(np.nan)) if hasattr(tb['parallax'], 'filled') else np.array(tb['parallax'])
    tb['parallax'] = original_parallax - zp
    
    # Filter: Must keep positive parallax after correction
    mask_positive = tb['parallax'] > 0.0
    mask_positive = np.nan_to_num(mask_positive, nan=False).astype(bool)
    initial_count = len(tb)
    tb = tb[mask_positive]
    
    print(f"Applied Zero-Point correction. Kept {len(tb)}/{initial_count} stars with positive parallax.")
    
    if len(tb) == 0:
        print("No stars left after positive parallax filter. Removing empty FITS file.")
        if os.path.exists(fits_file):
            os.remove(fits_file)
    else:
        tb.write(fits_file, format='fits', overwrite=True)
        
except Exception as e:
    print(f"Error applying zero-point: {e}")
    sys.exit(1)
