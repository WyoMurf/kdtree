#!/usr/bin/env python3
import os
import sys
import glob

def calculate_box_volume(coords):
    if len(coords) < 6:
        return 0.0
    min_x, min_y, min_z, max_x, max_y, max_z = coords
    dx = float(max_x - min_x)
    dy = float(max_y - min_y)
    dz = float(max_z - min_z)
    if dx < 0 or dy < 0 or dz < 0:
        return 0.0
    return dx * dy * dz

def parse_bb_file(filepath):
    try:
        with open(filepath, "r") as f:
            line = f.readline().strip()
            if not line:
                return None
            parts = line.split()
            if len(parts) >= 6:
                return [int(x) for x in parts[:6]]
    except Exception as e:
        print(f"Error reading {filepath}: {e}", file=sys.stderr)
    return None

def format_volume(v):
    # Since coordinates are scaled by 1e9, the physical volume is in cubic parsecs.
    # Volume in scaled coordinates = V_physical * (1e9)^3 = V_physical * 1e27.
    # We can divide by 1e27 to get the volume in cubic parsecs (pc^3)!
    vol_pc3 = v / 1e27
    if vol_pc3 >= 1e9:
        return f"{vol_pc3/1e9:.3e} billion pc³"
    elif vol_pc3 >= 1e6:
        return f"{vol_pc3/1e6:.3f} million pc³"
    else:
        return f"{vol_pc3:.3f} pc³"

def main():
    target_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    
    # We find all original .bb files (e.g. ones that do not end in -<digit>.bb)
    pattern = os.path.join(target_dir, "*.bb")
    all_bb_files = glob.glob(pattern)
    
    original_bb_files = []
    for f in all_bb_files:
        base = os.path.basename(f)
        name, _ = os.path.splitext(base)
        # If the name ends in -0, -1, ..., -9, it is a subtree segment, not the original!
        parts = name.split("-")
        if len(parts) > 1 and parts[-1].isdigit() and 0 <= int(parts[-1]) <= 9:
            continue
        original_bb_files.append(f)
        
    original_bb_files = sorted(original_bb_files)
    
    if not original_bb_files:
        print(f"No original *.bb files found in: {target_dir}")
        print("Please run fits2kd first to generate them.")
        return

    print("=" * 95)
    print(f"{'Base Catalog Name':<45} | {'Orig Volume':<18} | {'Subtrees Vol':<18} | {'Reduction'}")
    print("=" * 95)

    global_orig_vol = 0.0
    global_sub_vol = 0.0

    for orig_f in original_bb_files:
        orig_coords = parse_bb_file(orig_f)
        if not orig_coords:
            continue
            
        orig_vol = calculate_box_volume(orig_coords)
        if orig_vol == 0.0:
            continue
            
        # Find all corresponding subtrees for this base catalog
        base_path, _ = os.path.splitext(orig_f)
        
        subtree_vol_sum = 0.0
        subtrees_found = 0
        
        for s in range(10):
            sub_f = f"{base_path}-{s}.bb"
            if os.path.exists(sub_f):
                sub_coords = parse_bb_file(sub_f)
                if sub_coords:
                    sub_vol = calculate_box_volume(sub_coords)
                    subtree_vol_sum += sub_vol
                    subtrees_found += 1
                    
        reduction_pct = (1.0 - (subtree_vol_sum / orig_vol)) * 100.0 if orig_vol > 0 else 0.0
        reduction_factor = orig_vol / subtree_vol_sum if subtree_vol_sum > 0 else 1.0
        
        base_name = os.path.basename(base_path)
        print(f"{base_name:<45} | {format_volume(orig_vol):<18} | {format_volume(subtree_vol_sum):<18} | {reduction_pct:.2f}% ({reduction_factor:.1f}x)")
        
        global_orig_vol += orig_vol
        global_sub_vol += subtree_vol_sum

    if global_orig_vol > 0.0:
        global_reduction_pct = (1.0 - (global_sub_vol / global_orig_vol)) * 100.0
        global_reduction_factor = global_orig_vol / global_sub_vol if global_sub_vol > 0 else 1.0
        print("=" * 95)
        print(f"{'OVERALL TOTALS':<45} | {format_volume(global_orig_vol):<18} | {format_volume(global_sub_vol):<18} | {global_reduction_pct:.2f}% ({global_reduction_factor:.1f}x)")
        print("=" * 95)

if __name__ == "__main__":
    main()
