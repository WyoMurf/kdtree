# 3D KD-Tree (Julia Implementation)

This directory contains the unified Julia implementation of a 3D KD-Tree, relying on Julia's parametric types and multiple dispatch to support 32-bit, 64-bit, and 128-bit coordinate widths seamlessly.

## Usage

You do not need to compile or build anything explicitly. Julia handles compilation at runtime (JIT) based on the exact types you provide. Function names remain clean and idiomatic, without any bit-size suffixes.

```julia
using KDTree3D

# Instantiate a 32-bit KDTree
tree32 = Tree{String, Int32}()
box32 = (Int32(0), Int32(0), Int32(0), Int32(10), Int32(10), Int32(10))
insert!(tree32, "item32", box32)

# Instantiate a 128-bit KDTree

tree128 = Tree{String, Int128}()

box128 = (Int128(0), Int128(0), Int128(0), Int128(10), Int128(10), Int128(10))

insert!(tree128, "item128", box128)


# Instantiate a 64-bit KDTree
tree64 = Tree{String, Int64}()
box64 = (Int64(0), Int64(0), Int64(0), Int64(10), Int64(10), Int64(10))
insert!(tree64, "item64", box64)
```

## Testing

Run the test suite using standard Julia commands:
```bash
julia --project=. -e 'using Pkg; Pkg.test()'
```
