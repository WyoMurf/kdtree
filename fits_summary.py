#!/usr/bin/env python3
import os
import sys
import glob
import warnings
from astropy.io import fits

warnings.filterwarnings('ignore')

def main():
    # If a path was passed as an argument, use it; otherwise, default to current directory
    target_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    pattern = os.path.join(target_dir, "*.fits.gz")
    files = sorted(glob.glob(pattern))

    if not files:
        print(f"No *.fits.gz files found in: {target_dir}")
        return

    total_stars = 0
    total_bytes = 0

    print(f"{'Filename':<55} | {'Stars (Rows)':<12} | {'Size':<10}")
    print("-" * 83)

    for f in files:
        try:
            with fits.open(f) as hdul:
                num_stars = len(hdul[1].data)
            size_bytes = os.path.getsize(f)
            total_stars += num_stars
            total_bytes += size_bytes
            print(f"{os.path.basename(f):<55} | {num_stars:<12,} | {size_bytes / (1024*1024):.1f} MB")
        except Exception as e:
            print(f"Error reading {f}: {e}")

    print("-" * 83)
    print(f"{'TOTALS':<55} | {total_stars:<12,} | {total_bytes / (1024*1024*1024):.2f} GB")

if __name__ == "__main__":
    main()
