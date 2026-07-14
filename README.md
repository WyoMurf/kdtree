# Universal KD-Tree Library

A highly-optimized, memory-safe, and multi-language K-Dimensional Tree (KD-Tree) implementation for high-performance spatial querying. 

This repository contains completely unified 2D and 3D implementations across **C, Go, Julia, and Rust**. Each implementation natively supports `32-bit`, `64-bit`, and `128-bit` coordinate precision, dynamically adapting to your data's requirements.

At the core, it stores objects based on their 2-d (x,y) and 3d- (x,y,z) bounding-box coordinates (which can be expressed as 32-bit, 64-bit, or 128-bit integer numbers). These can searched using another bounding box, new objects can be entered, objects can be deleted, the tree can be rebuilt if it would benefit search times, a "badness" of the tree can be determined, and nearest neighbors can be determined.

Recently added is a serialization of the kdtree data, that can be written to disk, then memory-mapped for quick retrieval of built kdtrees. This is for data that doesn't change often. You can build a kdtree for ~500,000 objects in less than 2 sec. This serialization replaces pointers with integer indices, which are remapped on reading. This serialization is coded for C, go, julia, and rust. For 3d-data, a simple data viewer is provided as a demonstration, Some scripts are provided to download, merge and filter the Gaia Source data, which contains 1.8 billion stars, contained in ~3400 download files, and ~3400 AstrophysicalParameters files. These files are filtered to about 1/10 that number. The viewer is based on raylib, which you will have to clone and compile locally. And, of course, you will need the FITSIO library, and perhaps a Python virtual environment. More on this in a separate README.


## Implementations

Choose the language environment that best fits your stack. Each implementation resides in its own directory with specific instructions for compilation, usage, and testing.

| Language | Directory | Key Features | Precision Support |
| :--- | :--- | :--- | :--- |
| **C** | [`/C`](C/README.md) | Single shared library (`libkdtree.so`), zero-collision macro suffixes, highly optimized structs. | `int32_t`, `int64_t`, `__int128` |
| **Rust** | [`/2d/rust`](2d/rust/README.md) <br> [`/3d/rust`](3d/rust/README.md) | Safe generic traits (`Tree<T, C>`), contiguous memory arena allocation, zero-cost abstractions. | `i32`, `i64`, `i128` |
| **Julia** | [`/2d/julia`](2d/julia/README.md) <br> [`/3d/julia`](3d/julia/README.md) | Idiomatic parametric types (`Tree{T, C<:Integer}`), multiple dispatch, automated JIT compilation. | `Int32`, `Int64`, `Int128` |
| **Go** | [`/2d/go`](2d/go/README.md) <br> [`/3d/go`](3d/go/README.md) | Go 1.18+ Generics (`[T Coord]`), clean un-suffixed APIs, safe struct bounds. | `int32`, `int64` |

## Capabilities

All language implementations adhere to the same underlying high-performance algorithms, originally based on J.L. Bentley's foundational architectures, heavily modernized and rigorously verified.

* **O(N log N) Building:** Highly balanced initial tree construction.
* **O(log N) Nearest Neighbor (NN):** Extremely fast point-based distance searching.
* **Range Searching:** Retrieve all items bound within a defined multi-dimensional box.
* **Soft Deletion:** Fast O(log N) item masking (flagging nodes as dead without structural disruption).
* **Hard Deletion:** True recursive structural removal and tree re-balancing to reclaim memory and maintain search speed.

## Precision & Bit-Widths

Unlike standard algorithms that force you to choose between memory efficiency (32-bit) and overflow safety (64-bit) at the project level, this library parameterizes coordinate widths. All languages but Go can support 128-bit integer coordinates. When Go is upgraded to support 128-bit integers natively, we will upgrade that implementation. 

Whether you compile via Make flags (C), invoke Generic parameters (Rust/Go), or utilize dynamic dispatch (Julia), the KD-Tree will natively process your exact bit-width without requiring separate library distributions or suffering from type-casting overheads.

## License

This software is released under the **LGPL v2.0** License. See the `LICENSE` file for details.

For Go users,

To get the 2D package:
go get github.com/WyoMurf/kdtree/2d/go@v1.0.0

To get the 3D package:
go get github.com/WyoMurf/kdtree/3d/go@v1.0.0


Julia:

Developers can open the Julia REPL and type ] add KDTree and ] add KDTree3D.


