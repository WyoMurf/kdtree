# 2D KD-Tree (Julia Implementation)

This directory contains the unified Julia implementation of a 2D KD-Tree, relying on Julia's parametric types and multiple dispatch to support 32-bit, 64-bit, 128-bit, and Float64 coordinate widths seamlessly.

## Usage

You do not need to compile or build anything explicitly. Julia handles compilation at runtime (JIT) based on the exact types you provide. Function names remain clean and idiomatic, without any bit-size suffixes.

```julia
using KDTree

# Instantiate a 32-bit KDTree
tree32 = Tree{String, Int32}()
box32 = (Int32(0), Int32(0), Int32(10), Int32(10))
insert!(tree32, "item32", box32)


# Instantiate a 64-bit KDTree
tree64 = Tree{String, Int64}()
box64 = (Int64(0), Int64(0), Int64(10), Int64(10))
insert!(tree64, "item64", box64)

# Instantiate a 128-bit KDTree
tree128 = Tree{String, Int128}()
box128 = (Int128(0), Int128(0), Int128(10), Int128(10))
insert!(tree128, "item128", box128)

# Instantiate a Float64 KDTree, for real-valued coordinates
treef64 = Tree{String, Float64}()
boxf64 = (0.0, 0.0, 10.5, 10.5)
insert!(treef64, "itemf64", boxf64)
```


## Geo & Angle Utilities

Also exported, unrelated to the tree itself: great-circle distance calculators and DMS↔degrees conversion.

```julia
# Fast, approximate (perfect sphere)
km = haversine_distance(40.7128, -74.0060, 51.5074, -0.1278, EARTH_RADIUS_KM)

# Slow, exact (oblate spheroid) -- Earth's WGS-84 constants are provided for you
m = vincenty_distance(40.7128, -74.0060, 51.5074, -0.1278, EARTH_SEMI_MAJOR_AXIS_M, EARTH_FLATTENING)

# DMS <-> decimal degrees (sign carried separately so -0deg 15min is representable)
degrees = dms_to_degrees(-1, Int32(73), Int32(58), 34.0)
sign, deg, min, sec = degrees_to_dms(Int32, degrees)
```

## Testing

Run the test suite using standard Julia commands:
```bash
make
```
