# kdtree Viewers (Rust Implementation)

Rust ports of the C viewers in `C/earth_viewer.c` and `C/viewer.c`, using
[raylib-rs](https://github.com/deltaphc/raylib-rs) (real FFI bindings to
raylib; `raylib-sys` compiles raylib from source at build time via CMake
+ bindgen — no system-wide raylib install needed).

- `earthviewer/` — orbits Earth, rendering GeoNames city data
  (`README-cities.md`)
- `starviewer/` — flies through the Gaia DR3 star catalog
  (`README-stars.md`)
- `kdmmap/` — shared crate that mmaps the `.kdtree`/`.metatree`/`.lod`
  files as `#[repr(C)]` structs matching the C layout exactly
- `spike/` — minimal toolchain smoke test (rotating cube + an rlgl quad),
  not one of the real viewers

## One-time system setup

Building `raylib-sys` from source needs `cmake` (compiles raylib) and
`libclang` (bindgen's Rust binding generation). On a fresh
AlmaLinux/Fedora/RHEL machine:

```bash
sudo dnf install cmake clang-devel
```

## Building

```bash
cd viewers/rust
cargo build --release -p earthviewer -p starviewer
```

Binaries land at `target/release/earthviewer` and `target/release/starviewer`.

## Running

The city viewer needs `cities.metatree`/`cities.manifest`/`cities.names`/
`city_tile_*.kdtree` in the current directory (see `README-cities.md` for
how to build those with `geonames2kd` + `build_city_metatree`):

```bash
cd C/citydata
/path/to/viewers/rust/target/release/earthviewer
```

Controls: drag with the left mouse button to orbit, scroll or W/S to zoom.

The star viewer needs `catalog.metatree`/`catalog.metatree.lod`/
`catalog.manifest` plus the per-shard `.kdtree`/`.kdtree.lod` files in the
current directory (see `README-stars.md`):

```bash
cd /path/to/your/star-catalog-dir
/path/to/viewers/rust/target/release/starviewer
```

Controls: W/S/A/D/Q/E to fly, click-drag either mouse button to look
around, Up/Down arrows to change speed exponentially, `[`/`]` to adjust
LOD detail.

Both windows close with Esc or the window's close button.

## Automated visual smoke tests

Since there's no interactive test harness for a GUI app, both viewers
support env-var-driven headless screenshots, exercised by
`earthviewer/test_earth_viewer_visual.sh` and
`starviewer/test_viewer_visual.sh`:

- `earthviewer`: `EV_LON`, `EV_LAT`, `EV_ALT` (override the starting
  camera position), `EV_SCREENSHOT` (output path), `EV_SCREENSHOT_FRAME`
  (which frame to capture, default 30)
- `starviewer`: `SV_CAM_X/Y/Z`, `SV_TARGET_X/Y/Z` (override the starting
  camera position/target, in parsecs), `SV_SCREENSHOT`,
  `SV_SCREENSHOT_FRAME`

```bash
viewers/rust/earthviewer/test_earth_viewer_visual.sh [city-data-dir]
viewers/rust/starviewer/test_viewer_visual.sh <star-catalog-dir>
```

These are smoke tests only (does it build, does it run, does it produce
a non-empty image) — review the output PNGs yourself.
