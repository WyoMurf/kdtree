# 3D KD-Tree (Rust Implementation)

This directory contains the unified Rust implementation of a 3D KD-Tree, parameterized using Rust Generics and Traits to support `i32`, `i64`, `i128`, and `f64` coordinate widths dynamically.

## Compilation & Usage

You do not need any build-time feature flags to choose between coordinate widths. The `Tree` struct uses a generic type parameter bounded by a custom `Coord` trait. Function names remain clean and idiomatic, without any bit-size suffixes.

```rust
use kdtree3d::{Tree, KdBox};

fn main() {
    // Instantiate a 32-bit KDTree
    let mut tree32: Tree<&str, i32> = Tree::new();
    let box32: KdBox<i32> = [0, 0, 0, 10, 10, 10];
    tree32.insert("item32", box32);

    // Instantiate a 64-bit KDTree
    let mut tree64: Tree<&str, i64> = Tree::new();
    let box64: KdBox<i64> = [0, 0, 0, 10, 10, 10];
    tree64.insert("item64", box64);

    // Instantiate a 128-bit KDTree
    let mut tree128: Tree<&str, i128> = Tree::new();
    let box128: KdBox<i128> = [0, 0, 0, 10, 10, 10];
    tree128.insert("item128", box128);

    // Instantiate a float64 KDTree, for real-valued coordinates
    let mut treef64: Tree<&str, f64> = Tree::new();
    let boxf64: KdBox<f64> = [0.0, 0.0, 0.0, 10.5, 10.5, 10.5];
    treef64.insert("itemf64", boxf64);
}
```

## Testing

Run the test suite using standard cargo commands:
```bash
cargo test
```
