Demonstration: viewing world towns/cities/villages on a globe

A terrestrial sibling of the star-catalog viewer described in README-stars.md,
using the same two-layer kd-tree idea: tile Earth's surface into HEALPix
cells, build a small 2D kd-tree of towns/cities per cell, and a second
kd-tree over those cells' own bounding boxes, so the viewer only has to load
the cells actually visible from the camera.

It's dramatically smaller in scale than the star viewer -- ~170,000 places
across ~150 non-empty cells, vs. Gaia's ~33,500 shards and 157 million stars
-- so it's also architecturally simpler: there's no need for the star
viewer's `kd2lod` subtree-bounding-box/angular-collapse machinery. Every
meta-tree cell is checked every frame (a scan of ~150 entries is trivial),
and every visible point in a loaded cell is drawn directly.

DATA SOURCE:

Populated-place data comes from GeoNames (https://www.geonames.org/), a free
and open geographic database. `cities1000.txt` is every populated place with
population >= 1000 (~170,000 places worldwide) -- other thresholds are
available (`cities500`, `cities5000`, `cities15000`, or the full
`allCountries` dump with ~4.8 million places, though that scale would need
the kd2lod-porting work this viewer deliberately skipped -- see the code
comments in `C/earth_viewer.c`).

BUILDING:

The C library and tools need to be built first (see the main README.md and
C/README.md for the raylib/general C build prerequisites -- this viewer
needs the same locally-built raylib the star viewer does).

From the repo's `C/` directory:

    make all
    make geonames2kd build_city_metatree earth_viewer

Then, in a working directory for the city data (~10MB download, a few MB of
generated `.kdtree`/`.names` files -- nowhere near the star catalog's
~100GB):

    mkdir citydata && cd citydata
    curl -O https://download.geonames.org/export/dump/cities1000.zip
    unzip cities1000.zip
    /path/to/kdtree/C/geonames2kd cities1000.txt
    /path/to/kdtree/C/build_city_metatree . cities

This produces `city_tile_<healpix-cell-id>.kdtree` (one 2D kd-tree per
non-empty HEALPix level-3 cell), `cities.names` (a geonameid -> name/
population lookup for on-screen labels), `cities.metatree` (the meta-tree of
cell bounding boxes), and `cities.manifest` (index -> tile filename, for the
meta-tree's items).

RUNNING:

From that same directory:

    /path/to/kdtree/C/earth_viewer

Controls: drag with the left mouse button to orbit around the globe;
scroll the mouse wheel or hold W/S (or the up/down arrow keys) to zoom in
and out. The view starts several Earth radii out; as you get closer, city
names appear progressively -- small towns only label once you're quite
close, larger cities label from much farther away (tunable via the
`labelThresholdKm` formula in `DrawTilePoints`, `C/earth_viewer.c`).

KNOWN LIMITATIONS (deliberate v1 scope, not oversights):

- A handful of HEALPix cells straddle the +/-180 degree antimeridian, which
  gives those specific cells a "loose" (larger than necessary, not
  incorrect) bounding box in the meta-tree -- a minor loading-eagerness
  cost, not a rendering bug, since every individual point still gets its own
  precise visibility test regardless of which cell it came from.
- The globe is a plain colored sphere with a wireframe grid, not a textured
  photo-real Earth -- texturing with a real image is a natural follow-up,
  intentionally left out of this first pass.
- No label-collision avoidance: in a dense area (e.g. Madrid's many close
  suburbs), several labels can overlap. A real map application would hide
  overlapping labels by priority; this viewer doesn't yet.
