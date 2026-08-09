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


## Testing

Run the test suite using standard Julia commands:
```bash
make
```
