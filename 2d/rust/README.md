# 2D KD-Tree (Rust Implementation)

This directory contains the unified Rust implementation of a 2D KD-Tree, parameterized using Rust Generics and Traits to support `i32`, `i64`, `i128`, and `f64` coordinate widths dynamically. 2D now has the same method coverage as 3D, including `nearest`, `really_delete`, and `badness`.

## Compilation & Usage

You do not need any build-time feature flags to choose between coordinate widths. The `Tree` struct uses a generic type parameter bounded by a custom `Coord` trait. Function names remain clean and idiomatic, without any bit-size suffixes.

```rust
use kdtree2d::{Tree, KdBox};

fn main() {
    // Instantiate a 32-bit KDTree
    let mut tree32: Tree<&str, i32> = Tree::new();
    let box32: KdBox<i32> = [0, 0, 10, 10];
    tree32.insert("item32", box32);

    // Instantiate a 64-bit KDTree
    let mut tree64: Tree<&str, i64> = Tree::new();
    let box64: KdBox<i64> = [0, 0, 10, 10];
    tree64.insert("item64", box64);

    // Instantiate a 128-bit KDTree
    let mut tree128: Tree<&str, i128> = Tree::new();
    let box128: KdBox<i128> = [0, 0, 10, 10];
    tree128.insert("item128", box128);

    // Instantiate a float64 KDTree, for real-valued coordinates
    let mut treef64: Tree<&str, f64> = Tree::new();
    let boxf64: KdBox<f64> = [0.0, 0.0, 10.5, 10.5];
    treef64.insert("itemf64", boxf64);
}
```

## Geo & Angle Utilities

Also exported, unrelated to the tree itself: great-circle distance calculators and DMS↔degrees conversion.

```rust
use kdtree2d::{haversine_distance, vincenty_distance, dms_to_degrees, degrees_to_dms,
               EARTH_RADIUS_KM, EARTH_SEMI_MAJOR_AXIS_M, EARTH_FLATTENING};

// Fast, approximate (perfect sphere)
let km = haversine_distance(40.7128, -74.0060, 51.5074, -0.1278, EARTH_RADIUS_KM);

// Slow, exact (oblate spheroid) -- Earth's WGS-84 constants are provided for you
let m = vincenty_distance(40.7128, -74.0060, 51.5074, -0.1278, EARTH_SEMI_MAJOR_AXIS_M, EARTH_FLATTENING);

// DMS <-> decimal degrees (sign carried separately so -0deg 15min is representable)
let degrees = dms_to_degrees::<i32>(-1, 73, 58, 34.0);
let (sign, deg, min, sec) = degrees_to_dms::<i32>(degrees);
```

## Testing

Run the test suite using standard cargo commands:
```bash
cargo test
```
