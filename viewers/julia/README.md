# kdtree Viewers (Julia Implementation)

Julia ports of the C viewers in `C/earth_viewer.c` and `C/viewer.c`,
using [Raylib.jl](https://github.com/chengchingwen/Raylib.jl) (bindings
auto-generated from raylib's own `raylib_api.xml`/`raymath_api.xml`,
backed by `Raylib_jll`'s precompiled binaries — no system packages or
compilation needed).

Currently pinned via `Project.toml`'s `[sources]` override to
[WyoMurf/Raylib.jl](https://github.com/WyoMurf/Raylib.jl)`@main` — a
raylib 4.0 → 6.0 binding upgrade, pending as
[upstream PR #35](https://github.com/chengchingwen/Raylib.jl/pull/35).
Switch back to the plain registered `Raylib` package (drop the
`[sources]` block) once that merges and a new version is tagged.

- `earthviewer/` — orbits Earth, rendering GeoNames city data
  (`README-cities.md`)
- `starviewer/` — flies through the Gaia DR3 star catalog
  (`README-stars.md`)
- `kdmmap.jl` — shared module that mmaps the `.kdtree`/`.metatree`/`.lod`
  files as plain structs matching the C layout exactly
- `spike.jl` — minimal toolchain smoke test (rotating cube + an rlgl
  quad), not one of the real viewers

## One-time setup

No system packages needed. From this directory, once:

```bash
julia --project=. -e 'using Pkg; Pkg.instantiate()'
```

## Running

Julia has no separate build step — run the scripts directly.

The city viewer needs `cities.metatree`/`cities.manifest`/`cities.names`/
`city_tile_*.kdtree` in the current directory (see `README-cities.md` for
how to build those with `geonames2kd` + `build_city_metatree`):

```bash
cd C/citydata
julia --project=/path/to/viewers/julia /path/to/viewers/julia/earthviewer/main.jl
```

Controls: drag with the left mouse button to orbit, scroll or W/S to zoom.

The star viewer needs `catalog.metatree`/`catalog.metatree.lod`/
`catalog.manifest` plus the per-shard `.kdtree`/`.kdtree.lod` files in the
current directory (see `README-stars.md`):

```bash
cd /path/to/your/star-catalog-dir
julia --project=/path/to/viewers/julia /path/to/viewers/julia/starviewer/main.jl
```

Controls: W/S/A/D/Q/E to fly, click-drag either mouse button to look
around, Up/Down arrows to change speed exponentially, `[`/`]` to adjust
LOD detail.

Both windows close with Esc or the window's close button. First launch
is slower than later ones — Julia is JIT-compiling everything, not
hanging.

## A known limitation: fixed render distance

Still present after the raylib 6.0 upgrade, for a more specific reason
than before. raylib 6.0 itself did add a real runtime API for this —
`rlSetClipPlanes(near, far)`, in `rlgl.h` — but `Raylib.jl`'s binding
generator has never parsed `rlgl.h` at all, only `raylib.h`/`raygui.h`/
`raymath.h` (true on 4.0 and still true on 6.0), so that function isn't
reachable from Julia either way. The far clip distance is therefore
still effectively fixed at raylib's compile-time default
(`RL_CULL_DISTANCE_FAR`, ~1000 world units). Since real km/parsecs can't
be used as world units directly (a default camera altitude of 20,000 km,
or a star catalog spanning 50,000 pc, would be clipped away entirely),
every position and size actually handed to raylib is scaled down by a
fixed factor (`WORLD_UNIT_KM` in `earthviewer/geo.jl`, `WORLD_UNIT_PC` in
`starviewer/render.jl`). Altitude, flight speed, and the HUD display all
stay in real, unscaled km/parsecs — this is invisible in normal use, but
explains why those two constants exist if you're reading the source.
Actually removing this limitation would mean adding `rlgl.h` parsing to
`Raylib.jl` itself — a real upstream gap, not something this version
bump alone fixes.

The star viewer also renders stars as batched dynamic meshes rather than
`viewer.c`'s raw `rlBegin(RL_QUADS)`/`rlVertex3f` immediate-mode calls —
found under raylib 4.0 to be necessary because that build mishandled
overflowing its internal vertex batch capacity (see `starviewer/render.jl`
for details); not re-verified against 6.0, since the batched approach
works regardless and there's been no reason to revisit it.

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
viewers/julia/earthviewer/test_earth_viewer_visual.sh [city-data-dir]
viewers/julia/starviewer/test_viewer_visual.sh <star-catalog-dir>
```

These are smoke tests only (does it run, does it produce a non-empty
image) — review the output PNGs yourself.
