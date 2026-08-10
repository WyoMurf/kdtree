# Universal KD-Tree Library

A highly-optimized, memory-safe, and multi-language K-Dimensional Tree (KD-Tree) implementation for high-performance spatial querying. 

This repository contains completely unified 2D and 3D implementations across **C, Go, Julia, and Rust**. Each implementation natively supports `32-bit`, `64-bit`, and `128-bit` integer coordinate precision, plus `float64` for real-valued coordinates, dynamically adapting to your data's requirements. Except for Go, which does not yet support native 128-bit integer objects (and I'm not interested in using their big-number libraries) — Go does support `float64` like everyone else.

At the core, it stores objects based on their 2-d (x,y) and 3d- (x,y,z) bounding-box coordinates (which can be expressed as 32-bit, 64-bit, or 128-bit integers, or as float64 real numbers). These can searched using another bounding box, new objects can be entered, objects can be deleted, the tree can be rebuilt if it would benefit search times, a "badness" of the tree can be determined, and nearest neighbors can be determined.

Recently added is a serialization of the kdtree data, that can be written to disk, then memory-mapped for quick retrieval of built kdtrees. This is for data that doesn't change often. You can build a kdtree for ~500,000 objects in less than 2 sec. This serialization replaces pointers with integer indices, which are remapped on reading. This serialization is coded for C, go, julia, and rust. For 3d-data, a simple data viewer is provided as a demonstration, Some scripts are provided to download, merge and filter the Gaia Source data, which contains 1.8 billion stars, contained in ~3400 download files, and ~3400 AstrophysicalParameters files. These files are filtered to about 1/10 of their original size, by eliminating stars that don't have a parallax value at all, or don't have a solid parallax value. The C-based viewer has been updated over time to use 2 layers of kdtrees to speed up loading, which is now a "Lazy" loading of the stars that in the viewer's current window. Even with my meager 2060 RTX card, I can view millions of stars (which are either too far or too dim to see) at frame rates from 2 to 3 frames per second (when busy loading shards) to 20+ frames per second.

The viewer is based on raylib, which you will have to clone and compile locally. And, of course, you will need the FITSIO library, and perhaps a Python virtual environment. More on this in a separate README.

I also supply a simple python-based viewer, that basically reads a single kdtree file, and displays that, and supplies keyboard-based inputs to move around thru that data. It requires that you use a virtual environment on some systems (like my local AlmaLinux10.2 machine).



## Implementations

Choose the language environment that best fits your stack. Each implementation resides in its own directory with specific instructions for compilation, usage, and testing.

| Language | Directory | Key Features | Precision Support |
| :--- | :--- | :--- | :--- |
| **C** | [`/C`](C/README.md) | Single shared library (`libkdtree.so`), zero-collision macro suffixes, highly optimized structs. | `int32_t`, `int64_t`, `__int128`, `double` |
| **Rust** | [`/2d/rust`](2d/rust/README.md) <br> [`/3d/rust`](3d/rust/README.md) | Safe generic traits (`Tree<T, C>`), contiguous memory arena allocation, zero-cost abstractions. 2D and 3D now have identical method coverage (`nearest`, `really_delete`, `badness`). | `i32`, `i64`, `i128`, `f64` |
| **Julia** | [`/2d/julia`](2d/julia/README.md) <br> [`/3d/julia`](3d/julia/README.md) | Idiomatic parametric types (`Tree{T, C<:Real}`), multiple dispatch, automated JIT compilation. | `Int32`, `Int64`, `Int128`, `Float64` |
| **Go** | [`/2d/go`](2d/go/README.md) <br> [`/3d/go`](3d/go/README.md) | Go 1.18+ Generics (`[T Coord]`), clean un-suffixed APIs, safe struct bounds. | `int32`, `int64`, `float64` |

## Capabilities

All language implementations adhere to the same underlying high-performance algorithms, originally based on J.L. Bentley's foundational architectures, heavily modernized and rigorously verified.

* **O(N log N) Building:** Highly balanced initial tree construction.
* **O(log N) Nearest Neighbor (NN):** Extremely fast point-based distance searching.
* **Range Searching:** Retrieve all items bound within a defined multi-dimensional box.
* **Soft Deletion:** Fast O(log N) item masking (flagging nodes as dead without structural disruption).
* **Hard Deletion:** True recursive structural removal and tree re-balancing to reclaim memory and maintain search speed.

## Geo & Angle Utilities

Every language also ships a small set of general-purpose utilities that have nothing to do with the kd-tree itself, but are handy alongside it for spatial/astronomical data:

* **`haversine_distance`** — fast, approximate great-circle distance between two lat/lon points (in degrees) on a perfect sphere of a given radius.
* **`vincenty_distance`** — slower, iterative, but exact distance on an oblate spheroid (e.g. Earth's WGS-84 ellipsoid), given its semi-major axis and flattening. Earth's WGS-84 constants (`EARTH_RADIUS_KM`, `EARTH_SEMI_MAJOR_AXIS_M`, `EARTH_FLATTENING`) are provided in every language so you don't have to look them up.
* **`dms_to_degrees` / `degrees_to_dms`** — convert between degrees/minutes/seconds and decimal degrees (`f64`). Degrees and minutes are typed using the same per-language coordinate type as the rest of the library (purely for API-family consistency — the math itself doesn't need it); seconds is always `f64`. Sign is carried separately from the degree/minute magnitudes so angles between −1° and 0° (e.g. a declination of −0° 15′) are representable.
* **`healpix_nested_index`** — converts an equatorial (RA, Dec) or geographic (lon, lat) pair, in degrees, into a HEALPix NESTED-scheme pixel index at a given resolution level (`nside = 2^level`, `12·nside²` cells total — level 3 is 768 cells). The two angle names are interchangeable (same underlying projection either way); the first angle is normalized internally, so longitude can be passed in either the `[0, 360)` or `[-180, 180)` convention without pre-normalizing.
* **`healpix_nested_index_to_coords` / `healpix_ring_index_to_coords`** — the inverse of `healpix_nested_index`: given a pixel index and resolution level, returns the (RA/lon, Dec/lat) of that pixel's center (not necessarily the original point that produced the index). Two variants cover the two standard HEALPix pixel-numbering schemes — NESTED (matching `healpix_nested_index`'s output) and RING (for interoperability with RING-scheme indices from other HEALPix tools). Both return an error for an out-of-range index instead of garbage coordinates.

## Precision & Bit-Widths

Unlike standard algorithms that force you to choose between memory efficiency (32-bit) and overflow safety (64-bit) at the project level, this library parameterizes coordinate widths. All languages but Go can support 128-bit integer coordinates. When Go is upgraded to support 128-bit integers natively, we will upgrade that implementation. All four languages also support `float64`/`double` coordinates natively, for real-valued (non-integer) spatial data.

Whether you compile via Make flags (C), invoke Generic parameters (Rust/Go), or utilize dynamic dispatch (Julia), the KD-Tree will natively process your exact bit-width — integer or floating-point — without requiring separate library distributions or suffering from type-casting overheads.

## License

This software is released under the **LGPL v2.0** License. See the `LICENSE` file for details.

For Go users,

To get the 2D package:
go get github.com/WyoMurf/kdtree/2d/go@v1.0.0

To get the 3D package:
go get github.com/WyoMurf/kdtree/3d/go@v1.0.0


Julia:

Developers can open the Julia REPL and type ] add KDTree and ] add KDTree3D.


