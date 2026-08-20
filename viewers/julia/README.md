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

## Resolved: the old fixed render distance

Both viewers used to scale every position/size down by a fixed factor
(`WORLD_UNIT_KM`/`WORLD_UNIT_PC`) to stay under raylib's compile-time
far-clip default (~1000 world units) — raylib 4.0 had no runtime API to
change it at all. `Raylib.jl` now binds `rlgl.h` (previously unparsed on
any raylib version), exposing `rlSetClipPlanes(near, far)`; both viewers
call it once at startup with real near/far km/pc distances, matching the
same distances their own CPU-side culling frustum already used. Every
position and size handed to raylib is real, unscaled km/parsecs directly
now — `WORLD_UNIT_KM`/`WORLD_UNIT_PC` are gone.

Verified via the visual smoke tests below against real data: output is
visually identical to before the change (down to near-identical drawn-
point counts — the tiny remaining difference is boundary-condition
floating-point noise from computing in real units instead of scaled
ones, not a behavior change), with no depth-precision/z-fighting
artifacts from the much wider near:far ratio now in play.

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
