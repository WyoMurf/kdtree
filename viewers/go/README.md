# kdtree Viewers (Go Implementation)

Go ports of the C viewers in `C/earth_viewer.c` and `C/viewer.c`, using
[raylib-go](https://github.com/gen2brain/raylib-go) (real cgo bindings to
raylib, built from source at `go build` time — no system-wide raylib
install needed).

- `earthviewer/` — orbits Earth, rendering GeoNames city data
  (`README-cities.md`)
- `starviewer/` — flies through the Gaia DR3 star catalog
  (`README-stars.md`)
- `kdmmap/` — shared package that mmaps the `.kdtree`/`.metatree`/`.lod`
  files as Go structs matching the C layout exactly
- `spike/` — minimal toolchain smoke test (rotating cube + an rlgl quad),
  not one of the real viewers

## One-time system setup

`raylib-go` compiles raylib and GLFW from source the first time you
build. On a fresh AlmaLinux/Fedora/RHEL machine this needs:

```bash
sudo dnf install wayland-devel libxkbcommon-devel mesa-libGL-devel
```

(Equivalent X11/GL/Wayland dev packages on other distros — whatever your
system's C toolchain needs to compile GLFW.)

## Building

```bash
cd viewers/go
go build -o earthviewer/earthviewer ./earthviewer
go build -o starviewer/starviewer   ./starviewer
```

## Running

The city viewer needs `cities.metatree`/`cities.manifest`/`cities.names`/
`city_tile_*.kdtree` in the current directory (see `README-cities.md` for
how to build those with `geonames2kd` + `build_city_metatree`):

```bash
cd C/citydata
/path/to/viewers/go/earthviewer/earthviewer
```

Controls: drag with the left mouse button to orbit, scroll or W/S to zoom.

The star viewer needs `catalog.metatree`/`catalog.metatree.lod`/
`catalog.manifest` plus the per-shard `.kdtree`/`.kdtree.lod` files in the
current directory (see `README-stars.md`):

```bash
cd /path/to/your/star-catalog-dir
/path/to/viewers/go/starviewer/starviewer
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
viewers/go/earthviewer/test_earth_viewer_visual.sh [city-data-dir]
viewers/go/starviewer/test_viewer_visual.sh <star-catalog-dir>
```

These are smoke tests only (does it run, does it produce a non-empty
image) — review the output PNGs yourself.
