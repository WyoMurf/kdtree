#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"
#include "kdtree.h"

/*
 * Terrestrial analog of viewer.c: a two-layer kd-tree (cities.metatree over
 * city_tile_*.kdtree HEALPix-cell files, produced by geonames2kd +
 * build_city_metatree) rendered as points on Earth's sphere, with an orbit
 * camera and proximity-gated name labels.
 *
 * Deliberately simpler than viewer.c's LOD system: at ~390 tiles and ~170k
 * total points (vs. Gaia's ~33,500 shards and 157M stars), there's no need
 * for kd2lod's subtree-bbox-annotation/angular-collapse machinery -- every
 * meta-tree node is visited and tested every frame (a full scan of ~390
 * entries is trivial), and every point in a visible, loaded tile is drawn
 * directly. If this is ever pointed at a much larger place dataset (e.g.
 * GeoNames' full ~4.8M-place allCountries.zip), that's the point to
 * reconsider porting kd2lod to 2D -- not needed here.
 */

#define EARTH_RADIUS_KM 6371.0f
#define MIN_ALTITUDE_KM 20.0f
#define MAX_ALTITUDE_KM 150000.0f
#define INITIAL_ALTITUDE_KM 20000.0f /* ~3.1 Earth radii out: a "respectful distance" */

/* Optional real Earth texture, looked for in the current directory --
 * see README-cities.md for where to download it. Not required: if it's
 * absent, main() falls back to the original plain colored sphere. */
#define EARTH_TEXTURE_PATH "earth_daymap.jpg"
#define EARTH_SPHERE_RINGS 720
#define EARTH_SPHERE_SLICES 1440

/* raylib's Mesh.indices is `unsigned short *` (see raylib.h) -- a hard
 * 16-bit limit on vertex count PER MESH, baked into the library itself.
 * EARTH_SPHERE_RINGS/SLICES above exceed that in one mesh (721*1441 =
 * 1,038,961 vertices), so BuildEarthMeshes below splits the sphere into
 * several latitude bands, each its own Mesh under the limit, all sharing
 * one Material at draw time -- see its comment for the split math. */

static double DegToRad(double d) { return d * (M_PI / 180.0); }

static Vector3 LonLatToCartesian(double lonDeg, double latDeg, float radiusKm) {
    /* Longitude is negated here: raylib/OpenGL's right-handed, Y-up world
     * means +sin(lon) for Z maps increasing (eastward) longitude to the
     * geometrically wrong side once actually viewed through a standard
     * look-at camera -- the whole globe came out mirrored east-west (north/
     * south, driven by Y alone, was never affected). Negating flips it back
     * without touching the north/south mapping at all. */
    double lonRad = DegToRad(-lonDeg);
    double latRad = DegToRad(latDeg);
    double cl = cos(latRad);
    /* Y is the polar axis (matches raylib's Y-up world convention); north
     * pole at +Y, prime meridian/equator crossing toward +X. */
    return (Vector3){
        (float)(radiusKm * cl * cos(lonRad)),
        (float)(radiusKm * sin(latRad)),
        (float)(radiusKm * cl * sin(lonRad))
    };
}

/* Builds one latitude band (rows rowStart..rowEnd inclusive, out of the
 * overall 0..rings) of a UV-sphere textured with an equirectangular Earth
 * image, using the SAME LonLatToCartesian() that places every city dot --
 * so the texture and the dots are guaranteed to agree on where a given
 * (lonDeg, latDeg) lands, the same way the camera and the dots were made to
 * agree when the east-west mirroring bug was fixed. UV coordinates are
 * derived from the raw (lonDeg, latDeg) fed into that function, not from
 * the Cartesian result, so the vertex-position negation-of-longitude trick
 * inside LonLatToCartesian has no effect on texture alignment. Standard
 * equirectangular layout: u=0 at lon=-180 (west edge), u=1 at lon=+180,
 * increasing eastward; v=0 at the north pole (lat=+90), v=1 at the south
 * pole (lat=-90) -- v is still computed from the band-local row r (not the
 * band's own 0-based offset) so texture coordinates stay correct across
 * band boundaries. */
static Mesh GenEarthSphereMeshBand(float radiusKm, int rings, int slices, int rowStart, int rowEnd) {
    int bandRows = rowEnd - rowStart;
    int ringVerts = slices + 1;
    int vertexCount = (bandRows + 1) * ringVerts;
    int triangleCount = bandRows * slices * 2;

    /* Runtime backstop: BuildEarthMeshes below is responsible for keeping
     * every band under raylib's 16-bit Mesh.indices limit, but this
     * function takes rowStart/rowEnd as plain parameters, so nothing stops
     * a future caller from passing a range that violates it. */
    if (vertexCount > 65536) {
        fprintf(stderr, "GenEarthSphereMeshBand: rows %d..%d x %d slices = %d vertices, "
            "exceeds raylib's 16-bit Mesh.indices limit (65536)\n", rowStart, rowEnd, slices, vertexCount);
        exit(1);
    }

    Mesh mesh = { 0 };
    mesh.vertexCount = vertexCount;
    mesh.triangleCount = triangleCount;
    mesh.vertices = RL_MALLOC(sizeof(float) * 3 * (size_t)vertexCount);
    mesh.normals = RL_MALLOC(sizeof(float) * 3 * (size_t)vertexCount);
    mesh.texcoords = RL_MALLOC(sizeof(float) * 2 * (size_t)vertexCount);
    mesh.indices = RL_MALLOC(sizeof(unsigned short) * 3 * (size_t)triangleCount);
    /* Each band now allocates several MB (up to ~63,404 vertices x 32 bytes
     * of position/normal/texcoord data), versus the much smaller buffers
     * before the resolution bump -- worth actually checking these now,
     * rather than segfaulting mid-fill-loop on a NULL return under memory
     * pressure. */
    if (!mesh.vertices || !mesh.normals || !mesh.texcoords || !mesh.indices) {
        fprintf(stderr, "GenEarthSphereMeshBand: out of memory allocating a %d-vertex mesh\n", vertexCount);
        exit(1);
    }

    int v = 0;
    for (int r = rowStart; r <= rowEnd; r++) {
        double latDeg = 90.0 - (180.0 * r / rings);
        for (int s = 0; s <= slices; s++) {
            double lonDeg = -180.0 + (360.0 * s / slices);
            Vector3 p = LonLatToCartesian(lonDeg, latDeg, radiusKm);
            Vector3 n = Vector3Scale(p, 1.0f / radiusKm);
            mesh.vertices[v * 3 + 0] = p.x;
            mesh.vertices[v * 3 + 1] = p.y;
            mesh.vertices[v * 3 + 2] = p.z;
            mesh.normals[v * 3 + 0] = n.x;
            mesh.normals[v * 3 + 1] = n.y;
            mesh.normals[v * 3 + 2] = n.z;
            mesh.texcoords[v * 2 + 0] = (float)s / (float)slices;
            mesh.texcoords[v * 2 + 1] = (float)r / (float)rings;
            v++;
        }
    }

    /* Winding: (a,b,c) and (c,b,d), verified by cross((b-a),(c-a)) pointing
     * the same way as the outward radial direction at every sampled (r,s)
     * (checked numerically, not just by inspection) -- i.e. counter-
     * clockwise as seen from outside the sphere, which is what makes this
     * mesh's outward faces the "front" faces under backface culling.
     *
     * An earlier version of this loop used (a,c,b)/(c,d,b) -- the same
     * three vertices, wound the other way -- which produces INWARD-facing
     * triangles. With backface culling enabled that discarded the near
     * (should be visible) hemisphere and let the far (antipodal)
     * hemisphere win the depth test instead, but only once a texture with
     * actual spatial variation was bound: the default 1x1 placeholder
     * texture rendered as the same flat color regardless of which side was
     * actually showing, so the wrong side being visible had no visible
     * symptom until the real Earth texture made it obvious. It also only
     * showed up at close camera range during testing -- zoomed further out
     * it happened to still read as correct -- but that turned out to be a
     * red herring, not a second, altitude-dependent bug: this fix (getting
     * the winding right) resolved it at every altitude tested, with
     * backface culling back on. */
    int idx = 0;
    for (int r = 0; r < bandRows; r++) {
        for (int s = 0; s < slices; s++) {
            unsigned short a = (unsigned short)(r * ringVerts + s);
            unsigned short b = (unsigned short)(a + ringVerts);
            unsigned short c = (unsigned short)(a + 1);
            unsigned short d = (unsigned short)(b + 1);
            mesh.indices[idx++] = a; mesh.indices[idx++] = b; mesh.indices[idx++] = c;
            mesh.indices[idx++] = c; mesh.indices[idx++] = b; mesh.indices[idx++] = d;
        }
    }

    UploadMesh(&mesh, false);
    return mesh;
}

/* Splits a rings x slices UV-sphere into however many latitude bands are
 * needed to keep every individual Mesh under raylib's 16-bit Mesh.indices
 * limit (see GenEarthSphereMeshBand), and returns them as a plain array --
 * not a raylib Model, since a Model's multi-mesh support still requires one
 * Mesh per struct, and DrawMesh() (used in main()'s render loop) works
 * directly on that array with a single shared Material, which is simpler
 * than building a Model's meshes/meshMaterial arrays by hand for no benefit
 * here (this Earth mesh has no animation, LOD, or per-mesh material need). */
static Mesh *BuildEarthMeshes(float radiusKm, int rings, int slices, int *outCount) {
    int maxBandRows = (65536 / (slices + 1)) - 1;
    int bandCount = (rings + maxBandRows - 1) / maxBandRows; /* ceil(rings/maxBandRows) */
    int bandRows = (rings + bandCount - 1) / bandCount;      /* ceil(rings/bandCount), redistributed evenly */

    Mesh *meshes = malloc((size_t)bandCount * sizeof(Mesh));
    int count = 0;
    for (int rowStart = 0; rowStart < rings; rowStart += bandRows) {
        int rowEnd = rowStart + bandRows;
        if (rowEnd > rings) rowEnd = rings;
        meshes[count++] = GenEarthSphereMeshBand(radiusKm, rings, slices, rowStart, rowEnd);
    }
    *outCount = count;
    return meshes;
}

/* Standard sphere-horizon test: for a camera outside a sphere of radius R
 * centered at the origin, a surface point is on the visible (near) side iff
 * the angle between its direction from center and the camera's direction
 * from center is within the tangent-line angle to the horizon, i.e.
 * cos(angle) >= R / distance(camera, center). This is what actually delivers
 * "any view of Earth only shows half or less of the grid" -- frustum culling
 * alone doesn't know the sphere is opaque. */
/* IsAboveHorizon's original form recomputed Vector3Length(camPos) and
 * Vector3Normalize(camPos) on every single call even though camPos is the
 * same for every point tested in a given frame, and Vector3Normalize(
 * pointOnSphere) even though every pointOnSphere this is ever called with
 * comes from LonLatToCartesian(..., EARTH_RADIUS_KM) and so already has
 * magnitude EARTH_RADIUS_KM exactly -- normalizing it is really just a
 * scale by a compile-time-known constant, not a sqrt. At ~170k points
 * scanned per frame this was 2 wasted sqrtf() calls per point (one for
 * camPos, repeated ~170k times for a value that never changes; one for
 * pointOnSphere, avoidable entirely) -- a real, measured contributor to
 * this viewer's per-point CPU cost (see the DrawCityPoint-batching commit's
 * message for the fuller profiling picture). ComputeHorizonTest hoists the
 * camera-only part out to once per frame; IsAboveHorizonFast drops the
 * pointOnSphere normalize entirely. */
typedef struct {
    Vector3 camDir;     /* normalize(camPos), meaningless if allInside */
    float cosThreshold; /* EARTH_RADIUS_KM / |camPos| */
    int allInside;      /* camera at/under the surface: every point counts as visible */
} HorizonTest;

static HorizonTest ComputeHorizonTest(Vector3 camPos) {
    HorizonTest ht;
    float d = Vector3Length(camPos);
    if (d <= EARTH_RADIUS_KM) {
        ht.camDir = (Vector3){ 0.0f, 0.0f, 0.0f };
        ht.cosThreshold = 0.0f;
        ht.allInside = 1; /* shouldn't happen in practice */
    } else {
        ht.camDir = Vector3Scale(camPos, 1.0f / d);
        ht.cosThreshold = EARTH_RADIUS_KM / d;
        ht.allInside = 0;
    }
    return ht;
}

static int IsAboveHorizonFast(Vector3 pointOnSphere, const HorizonTest *ht) {
    if (ht->allInside) return 1;
    Vector3 pointDir = Vector3Scale(pointOnSphere, 1.0f / EARTH_RADIUS_KM);
    float cosAngle = Vector3DotProduct(pointDir, ht->camDir);
    return cosAngle >= ht->cosThreshold;
}

typedef struct { float a, b, c, d; } Plane;

/* Verbatim from viewer.c: Gribb-Hartmann frustum plane extraction. */
static void ExtractFrustumPlanes(Matrix m, Plane out[6]) {
    out[0] = (Plane){ m.m3+m.m0, m.m7+m.m4, m.m11+m.m8,  m.m15+m.m12 }; /* left */
    out[1] = (Plane){ m.m3-m.m0, m.m7-m.m4, m.m11-m.m8,  m.m15-m.m12 }; /* right */
    out[2] = (Plane){ m.m3+m.m1, m.m7+m.m5, m.m11+m.m9,  m.m15+m.m13 }; /* bottom */
    out[3] = (Plane){ m.m3-m.m1, m.m7-m.m5, m.m11-m.m9,  m.m15-m.m13 }; /* top */
    out[4] = (Plane){ m.m3+m.m2, m.m7+m.m6, m.m11+m.m10, m.m15+m.m14 }; /* near */
    out[5] = (Plane){ m.m3-m.m2, m.m7-m.m6, m.m11-m.m10, m.m15-m.m14 }; /* far */
}

static int PointInFrustum(const Plane fr[6], Vector3 p) {
    for (int i = 0; i < 6; i++) {
        if (fr[i].a * p.x + fr[i].b * p.y + fr[i].c * p.z + fr[i].d < 0.0f) return 0;
    }
    return 1;
}

/* Verbatim from viewer.c: positive-vertex (p-vertex) box/frustum test. Note
 * this tests whether the *box itself* can overlap the frustum volume, which
 * is not the same as "is any of these N sample points individually inside
 * it" -- a box much larger than a narrow close-up frustum can have every
 * sample point outside the frustum while its true extent still slices
 * through it (this is exactly what broke the first version of
 * CellVisible below: at low camera altitude the frustum footprint is far
 * smaller than a HEALPix level-3 cell, so no single sampled corner ever
 * landed inside it, even directly above a city in that cell). */
static int AABBOutsideFrustum(const Plane fr[6], Vector3 bmin, Vector3 bmax) {
    for (int i = 0; i < 6; i++) {
        Vector3 p;
        p.x = (fr[i].a >= 0.0f) ? bmax.x : bmin.x;
        p.y = (fr[i].b >= 0.0f) ? bmax.y : bmin.y;
        p.z = (fr[i].c >= 0.0f) ? bmax.z : bmin.z;
        if (fr[i].a * p.x + fr[i].b * p.y + fr[i].c * p.z + fr[i].d < 0.0f) return 1;
    }
    return 0;
}

/* Cell-level visibility test: sample a grid across the HEALPix cell's
 * lon/lat bounding box (not just its 4 corners -- see AABBOutsideFrustum's
 * comment for why that was insufficient), build the Cartesian AABB of those
 * samples, and test that box against the frustum properly. This only gates
 * *loading*, not final rendering (every point drawn gets its own precise
 * per-point horizon+frustum test in DrawTilePoints below), so any looseness
 * here just means "might load one extra tile eagerly," never a
 * rendering-correctness issue. Known accepted limitation: a handful of
 * cells straddle the +/-180 antimeridian, giving them a "loose" (not
 * incorrect) box -- see the ingestion plan notes. */
static int CellVisible(double lonMin, double latMin, double lonMax, double latMax, const Plane fr[6], const HorizonTest *ht) {
    Vector3 bmin = { 1e9f, 1e9f, 1e9f }, bmax = { -1e9f, -1e9f, -1e9f };
    int anyAboveHorizon = 0;
    const int STEPS = 6;
    for (int i = 0; i <= STEPS; i++) {
        double lon = lonMin + (lonMax - lonMin) * i / STEPS;
        for (int j = 0; j <= STEPS; j++) {
            double lat = latMin + (latMax - latMin) * j / STEPS;
            Vector3 p = LonLatToCartesian(lon, lat, EARTH_RADIUS_KM);
            if (p.x < bmin.x) bmin.x = p.x;
            if (p.x > bmax.x) bmax.x = p.x;
            if (p.y < bmin.y) bmin.y = p.y;
            if (p.y > bmax.y) bmax.y = p.y;
            if (p.z < bmin.z) bmin.z = p.z;
            if (p.z > bmax.z) bmax.z = p.z;
            if (IsAboveHorizonFast(p, ht)) anyAboveHorizon = 1;
        }
    }
    if (!anyAboveHorizon) return 0;
    return !AABBOutsideFrustum(fr, bmin, bmax);
}

/* --- Name/population lookup: cities.names is small enough (~170k rows, a
 * few MB) to load entirely into memory, unlike the tile/meta-tree files.
 *
 * This used to be a textbook hash table with separately-malloc'd chain
 * nodes (one malloc(sizeof(NameEntry)) per city, 170,603 individually
 * heap-allocated, scattered nodes linked via ->next). That's a real,
 * measured cost: LookupName is called for every point that passes the
 * horizon+frustum test (~88k times/frame at a typical view), and every one
 * of those calls chased a pointer to a effectively-random heap address --
 * profiling with clock_gettime() around WalkMetaTree showed LookupName
 * alone accounted for roughly a third of the ~34ms/frame this viewer was
 * spending on per-point CPU work (confirmed by temporarily short-circuiting
 * it to NULL and watching frame time drop by ~11ms), consistent with
 * near-guaranteed cache misses on every lookup.
 *
 * Fix: store every NameEntry in ONE contiguous array (allocated once,
 * grown like geonames2kd.c's add_point), and hash into an open-addressing
 * index table (linear probing) instead of a chain of pointers -- an empty
 * slot is -1, never a valid index, so lookups for a geonameid that isn't
 * present still terminate correctly. Every probe touches memory within (or
 * very near) the same array and the same small index table, instead of
 * jumping to a different scattered allocation on every step. */
typedef struct {
    uint64_t geonameid;
    char *name;
    long population;
} NameEntry;

#define NAME_HASH_SLOTS 524288 /* power of two; ~0.33 load factor for this dataset's ~170,603 cities */
static int32_t g_name_hash_slots[NAME_HASH_SLOTS];
static NameEntry *g_name_entries = NULL;
static size_t g_name_entry_count = 0;

static void LoadNames(const char *path) {
    for (size_t i = 0; i < NAME_HASH_SLOTS; i++) g_name_hash_slots[i] = -1; /* correct (empty) even if the file below can't be opened */

    FILE *f = fopen(path, "r");
    if (!f) { printf("Warning: could not open %s, city names will be unavailable.\n", path); return; }

    size_t capacity = 0, count = 0;
    NameEntry *entries = NULL;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        char *tab1 = strchr(line, '\t');
        if (!tab1) continue;
        *tab1 = '\0';
        char *nameStart = tab1 + 1;
        char *tab2 = strchr(nameStart, '\t');
        if (!tab2) continue;
        *tab2 = '\0';
        char *popStart = tab2 + 1;

        if (count == capacity) {
            capacity = capacity ? capacity * 2 : 65536;
            NameEntry *new_entries = realloc(entries, capacity * sizeof(NameEntry));
            if (!new_entries) { fprintf(stderr, "LoadNames: out of memory growing to %zu entries\n", capacity); exit(1); }
            entries = new_entries;
        }
        entries[count].geonameid = strtoull(line, NULL, 10);
        entries[count].name = strdup(nameStart);
        entries[count].population = atol(popStart);
        count++;
    }
    fclose(f);

    for (size_t i = 0; i < count; i++) {
        size_t slot = entries[i].geonameid % NAME_HASH_SLOTS;
        while (g_name_hash_slots[slot] != -1) slot = (slot + 1) % NAME_HASH_SLOTS;
        g_name_hash_slots[slot] = (int32_t)i;
    }

    g_name_entries = entries;
    g_name_entry_count = count;
    printf("Loaded %zu city names.\n", count);
}

static NameEntry *LookupName(uint64_t geonameid) {
    size_t slot = geonameid % NAME_HASH_SLOTS;
    while (g_name_hash_slots[slot] != -1) {
        NameEntry *e = &g_name_entries[g_name_hash_slots[slot]];
        if (e->geonameid == geonameid) return e;
        slot = (slot + 1) % NAME_HASH_SLOTS;
    }
    return NULL;
}

/* --- Per-tile mmap cache, same lazy-load-once-and-keep pattern as
 * viewer.c's Shard/EnsureShardLoaded (no eviction -- at ~390 tiles totaling
 * maybe a few MB, keeping them all mapped for the process lifetime once
 * touched is not a concern the way it was for ~33,500 star shards). --- */
typedef struct {
    kd_2d_f64_mmap_node *nodes;
    size_t nodes_map_size;
    size_t real_count;
    int attempted;
    Vector3 *positions; /* cached LonLatToCartesian(lon, lat, EARTH_RADIUS_KM) per node, computed once -- see EnsureTileLoaded */
} Tile;

static Tile *g_tiles = NULL;
static size_t g_tiles_loaded_count = 0;
static char **g_manifest_paths = NULL;
static size_t g_manifest_count = 0;

static kd_2d_f64_mmap_node *g_meta_nodes = NULL;
static size_t g_meta_node_count = 0;
static size_t g_meta_nodes_map_size = 0;

static char **LoadManifest(const char *path, size_t *out_count) {
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    char **lines = NULL;
    size_t count = 0, cap = 0;
    char buf[1024];
    while (fgets(buf, sizeof(buf), f)) {
        size_t len = strlen(buf);
        if (len > 0 && buf[len - 1] == '\n') buf[len - 1] = '\0';
        if (count == cap) {
            cap = cap ? cap * 2 : 256;
            char **new_lines = realloc(lines, cap * sizeof(char *));
            if (!new_lines) {
                fprintf(stderr, "LoadManifest: out of memory reading %s\n", path);
                for (size_t i = 0; i < count; i++) free(lines[i]);
                free(lines);
                fclose(f);
                return NULL;
            }
            lines = new_lines;
        }
        lines[count++] = strdup(buf);
    }
    fclose(f);
    *out_count = count;
    return lines;
}

static int LoadMetaTree(const char *dir) {
    char metatree_path[600], manifest_path[600];
    snprintf(metatree_path, sizeof(metatree_path), "%s/cities.metatree", dir);
    snprintf(manifest_path, sizeof(manifest_path), "%s/cities.manifest", dir);

    int fd = open(metatree_path, O_RDONLY);
    if (fd == -1) { perror(metatree_path); return 0; }
    struct stat sb;
    if (fstat(fd, &sb) == -1 || sb.st_size == 0) { close(fd); return 0; }
    g_meta_node_count = sb.st_size / sizeof(kd_2d_f64_mmap_node);
    /* A file truncated to fewer bytes than one node (e.g. a build_city_metatree
     * run interrupted mid-write) rounds g_meta_node_count down to 0 via
     * integer division -- guard against that here the same way
     * EnsureTileLoaded already does for tile files below, since
     * g_meta_node_count - 1 on the next line would otherwise wrap (size_t)
     * to SIZE_MAX and read wildly out of bounds. */
    if (g_meta_node_count == 0) {
        printf("Error: %s is too short to contain even one node (%lld bytes) -- likely truncated by an interrupted build_city_metatree run.\n",
               metatree_path, (long long)sb.st_size);
        close(fd);
        return 0;
    }
    g_meta_nodes = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (g_meta_nodes == MAP_FAILED) { g_meta_nodes = NULL; return 0; }
    g_meta_nodes_map_size = sb.st_size;

    size_t real_count = g_meta_node_count;
    if (g_meta_nodes[g_meta_node_count - 1].source_id == UINT64_MAX) {
        real_count = g_meta_node_count - 1;
    }

    g_manifest_paths = LoadManifest(manifest_path, &g_manifest_count);
    if (!g_manifest_paths || g_manifest_count != real_count) {
        printf("Error: %s has %zu entries, expected %zu (doesn't match %s).\n",
               manifest_path, g_manifest_count, real_count, metatree_path);
        return 0;
    }
    return 1;
}

static Tile *EnsureTileLoaded(uint64_t manifest_idx) {
    if (manifest_idx >= g_manifest_count) return NULL;
    Tile *tile = &g_tiles[manifest_idx];
    if (tile->attempted) return tile->nodes ? tile : NULL;
    tile->attempted = 1;

    const char *path = g_manifest_paths[manifest_idx];
    int fd = open(path, O_RDONLY);
    if (fd == -1) return NULL;
    struct stat sb;
    if (fstat(fd, &sb) == -1 || sb.st_size == 0) { close(fd); return NULL; }
    size_t node_count_total = sb.st_size / sizeof(kd_2d_f64_mmap_node);
    if (node_count_total == 0) { close(fd); return NULL; }
    kd_2d_f64_mmap_node *nodes = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (nodes == MAP_FAILED) return NULL;

    size_t real_count = node_count_total;
    if (nodes[node_count_total - 1].source_id == UINT64_MAX) real_count = node_count_total - 1;
    if (real_count == 0) { munmap(nodes, sb.st_size); return NULL; }

    tile->nodes = nodes;
    tile->nodes_map_size = sb.st_size;
    tile->real_count = real_count;

    /* Every point's (lon, lat) is fixed city data -- it never changes from
     * one frame to the next, so there's no reason to re-run
     * LonLatToCartesian's trig on it every single frame the way
     * DrawTilePoints used to. Computing it once here, the one time this
     * tile is ever loaded, turned out to be a meaningful chunk of this
     * viewer's per-point CPU cost profiled via clock_gettime() around
     * WalkMetaTree: LonLatToCartesian runs on every point in every visible
     * tile (not just the ones that end up drawn), ~170k times/frame at a
     * typical view, entirely redundantly for a value that was already
     * known at tile-load time. */
    tile->positions = malloc(sizeof(Vector3) * real_count);
    if (!tile->positions) {
        fprintf(stderr, "EnsureTileLoaded: out of memory caching positions for %s\n", path);
        exit(1);
    }
    for (size_t i = 0; i < real_count; i++) {
        double lon = (nodes[i].size[0] + nodes[i].size[2]) / 2.0;
        double lat = (nodes[i].size[1] + nodes[i].size[3]) / 2.0;
        tile->positions[i] = LonLatToCartesian(lon, lat, EARTH_RADIUS_KM);
    }

    g_tiles_loaded_count++;
    return tile;
}

/* --- Rendering --- */

static Vector3 g_billboardRight = { 1.0f, 0.0f, 0.0f };
static Vector3 g_billboardUp = { 0.0f, 1.0f, 0.0f };
static size_t g_points_drawn = 0;

/* City dots used to be drawn one at a time via rlBegin(RL_QUADS)/rlVertex3f
 * -- correct, but a real, measured performance problem: ~88k individual
 * immediate-mode vertex calls per frame (rlVertex3f/rlColor4ub/rlTexCoord2f,
 * each a real function call with rlgl's per-vertex batch-buffer bookkeeping)
 * capped this viewer at ~23 FPS at typical viewing distance regardless of
 * anything else in the scene, confirmed by disabling dot rendering alone and
 * watching FPS jump straight to a smooth 60 (verified: the Earth mesh's own
 * resolution/texture size, separately investigated, were NOT the
 * bottleneck).
 *
 * Fix: batch the same billboard-quad math into plain CPU arrays instead of
 * calling into rlgl per vertex, then upload each batch in one bulk
 * UpdateMeshBuffer call and draw it with one DrawMesh call. raylib's
 * Mesh.indices is still the hard 16-bit-per-mesh limit seen with the Earth
 * mesh, so quads are split across several fixed-capacity batch meshes the
 * same way the Earth sphere is split into latitude bands -- the difference
 * here is the *positions* change every frame (billboards always face the
 * camera, and which cities are visible changes as you move), while the
 * *index* buffer (which vertices form which triangles) never does, so it's
 * uploaded once at startup and only ever the position buffer is refreshed
 * per frame. Color and UV are dropped entirely -- every dot was always the
 * same flat amber color (rlColor4ub's argument never varied), so a shared
 * Material's diffuse tint reproduces that exactly without a per-vertex
 * color buffer, and an untextured mesh (material's texture defaults to
 * raylib's white 1x1) needs no UVs either. */
#define DOT_BATCH_MAX_QUADS 16384 /* 4 verts/quad * 16384 = 65536 = raylib's Mesh.indices (unsigned short) limit, exactly */
#define MAX_VISIBLE_DOTS_PER_FRAME 300000 /* headroom above cities500's ~235,206 total places -- see README-cities.md */
#define DOT_BATCH_COUNT ((MAX_VISIBLE_DOTS_PER_FRAME + DOT_BATCH_MAX_QUADS - 1) / DOT_BATCH_MAX_QUADS)

typedef struct {
    Mesh mesh;      /* capacity DOT_BATCH_MAX_QUADS quads; mesh.vertices is written into directly every frame */
    int quadCount;  /* how many of those quads are actually filled this frame */
} DotBatch;

static DotBatch g_dotBatches[DOT_BATCH_COUNT];
static int g_dotBatchesUsed = 0; /* highest batch touched this frame, +1 */
static Material g_dotMaterial = { 0 };

static void InitDotBatches(void) {
    g_dotMaterial = LoadMaterialDefault();
    g_dotMaterial.maps[MATERIAL_MAP_DIFFUSE].color = (Color){ 255, 210, 90, 255 }; /* warm amber marker dots */

    for (int b = 0; b < DOT_BATCH_COUNT; b++) {
        int maxVerts = DOT_BATCH_MAX_QUADS * 4;
        int maxTris = DOT_BATCH_MAX_QUADS * 2;

        Mesh mesh = { 0 };
        mesh.vertexCount = maxVerts;
        mesh.triangleCount = maxTris; /* capacity; per-frame draws lower this to quadCount*2 */
        mesh.vertices = RL_MALLOC(sizeof(float) * 3 * (size_t)maxVerts);
        mesh.indices = RL_MALLOC(sizeof(unsigned short) * 3 * (size_t)maxTris);
        if (!mesh.vertices || !mesh.indices) {
            fprintf(stderr, "InitDotBatches: out of memory allocating dot batch %d\n", b);
            exit(1);
        }
        memset(mesh.vertices, 0, sizeof(float) * 3 * (size_t)maxVerts);

        for (int q = 0; q < DOT_BATCH_MAX_QUADS; q++) {
            unsigned short v0 = (unsigned short)(q * 4);
            mesh.indices[q * 6 + 0] = v0;     mesh.indices[q * 6 + 1] = v0 + 1; mesh.indices[q * 6 + 2] = v0 + 2;
            mesh.indices[q * 6 + 3] = v0;     mesh.indices[q * 6 + 4] = v0 + 2; mesh.indices[q * 6 + 5] = v0 + 3;
        }

        UploadMesh(&mesh, true); /* dynamic: positions are re-uploaded every frame */
        g_dotBatches[b].mesh = mesh;
        g_dotBatches[b].quadCount = 0;
    }
}

/* A fixed world-space size looks fine from far away (shrinks correctly with
 * perspective) but is far too large up close: real neighboring towns in a
 * densely-settled region can be closer together than a generously-sized
 * fixed marker, so their billboards overlap and merge into a solid blob
 * instead of showing as distinct dots. Sizing by a roughly-constant angular
 * radius (world size = angle * distance) keeps markers a sensible, mostly
 * distance-independent size on screen instead, clamped so it neither
 * vanishes at planetary range nor balloons absurdly at very low altitude.
 *
 * Takes log10(population) pre-computed rather than a raw population count:
 * DrawTilePoints below also needs log10(population) for the label-threshold
 * curve, and log10f is a real transcendental-function cost at ~88k calls/
 * frame -- computing it once and reusing it here avoids doing it twice per
 * point for the identical value. */
static float CityMarkerWorldSize(float distKm, float log10Pop) {
    float popBoost = 1.0f + 0.15f * log10Pop;
    float angularRadius = 0.0009f * popBoost;
    float sz = angularRadius * distKm;
    if (sz < 0.3f) sz = 0.3f;
    if (sz > 150.0f) sz = 150.0f;
    return sz;
}

static void DrawCityPoint(Vector3 pos, float sizeKm) {
    size_t globalQuadIdx = g_points_drawn;
    size_t batchIdx = globalQuadIdx / DOT_BATCH_MAX_QUADS;
    if (batchIdx >= DOT_BATCH_COUNT) return; /* hit MAX_VISIBLE_DOTS_PER_FRAME; should never happen at this dataset's scale */
    size_t localQuadIdx = globalQuadIdx % DOT_BATCH_MAX_QUADS;

    Vector3 right = Vector3Scale(g_billboardRight, sizeKm);
    Vector3 up = Vector3Scale(g_billboardUp, sizeKm);
    Vector3 p0 = Vector3Subtract(Vector3Subtract(pos, right), up);
    Vector3 p1 = Vector3Subtract(Vector3Add(pos, right), up);
    Vector3 p2 = Vector3Add(Vector3Add(pos, right), up);
    Vector3 p3 = Vector3Add(Vector3Subtract(pos, right), up);

    DotBatch *batch = &g_dotBatches[batchIdx];
    float *v = &batch->mesh.vertices[localQuadIdx * 4 * 3];
    v[0] = p0.x; v[1] = p0.y; v[2] = p0.z;
    v[3] = p1.x; v[4] = p1.y; v[5] = p1.z;
    v[6] = p2.x; v[7] = p2.y; v[8] = p2.z;
    v[9] = p3.x; v[10] = p3.y; v[11] = p3.z;
    batch->quadCount = (int)localQuadIdx + 1;
    if ((int)batchIdx + 1 > g_dotBatchesUsed) g_dotBatchesUsed = (int)batchIdx + 1;

    g_points_drawn++;
}

#define MAX_LABELS_PER_FRAME 4000
typedef struct { Vector3 worldPos; char name[256]; } PendingLabel;
static PendingLabel g_labels[MAX_LABELS_PER_FRAME];
static int g_label_count = 0;

/* Every visible point drawn gets its own exact horizon+frustum test here --
 * the tile-level CellVisible check above only gates whether we bothered to
 * load/scan this tile at all, so any looseness there never leaks into what
 * actually gets rendered. */
static void DrawTilePoints(const Tile *tile, const Plane fr[6], const HorizonTest *ht, Vector3 camPos, float altitudeKm) {
    for (size_t i = 0; i < tile->real_count; i++) {
        const kd_2d_f64_mmap_node *node = &tile->nodes[i];
        if (node->source_id == 0) continue;
        Vector3 pos = tile->positions[i]; /* precomputed once in EnsureTileLoaded, see its comment */
        if (!IsAboveHorizonFast(pos, ht)) continue;
        if (!PointInFrustum(fr, pos)) continue;

        NameEntry *ne = LookupName(node->source_id);
        long population = ne ? ne->population : 0;
        float popf = (population > 10) ? (float)population : 10.0f;
        float log10Pop = log10f(popf);
        float distKm = Vector3Distance(camPos, pos);
        DrawCityPoint(pos, CityMarkerWorldSize(distKm, log10Pop));

        if (ne && g_label_count < MAX_LABELS_PER_FRAME) {
            /* Steep population curve so labels appear progressively, the way
             * a map app would: tiny villages (~1000 people) only label once
             * you're within ~50km; a capital-sized city (~3-10M) labels from
             * several hundred km out. A flatter/higher-based curve (tried
             * first) made every hamlet within view label at once, an
             * unreadable wall of text -- see the ingestion plan notes on
             * this being tuned to taste, easy to adjust further. */
            float labelThresholdKm = 60.0f * log10Pop - 130.0f;
            if (altitudeKm < labelThresholdKm) {
                PendingLabel *pl = &g_labels[g_label_count++];
                pl->worldPos = pos;
                snprintf(pl->name, sizeof(pl->name), "%s", ne->name);
            }
        }
    }
}

/* Every meta-tree node holds exactly one tile's own box (there's no
 * subtree-aggregated box without a kd2lod-style annotation, which this
 * viewer deliberately doesn't build -- see the file header comment), so
 * this always visits every node rather than pruning subtrees; at ~390
 * entries that's trivial. */
static void WalkMetaTree(int64_t idx, const Plane fr[6], const HorizonTest *ht, Vector3 camPos, float altitudeKm) {
    if (idx < 0) return;
    const kd_2d_f64_mmap_node *node = &g_meta_nodes[idx];

    if (CellVisible(node->size[0], node->size[1], node->size[2], node->size[3], fr, ht)) {
        uint64_t manifestIdx = node->source_id - 1;
        Tile *tile = EnsureTileLoaded(manifestIdx);
        if (tile) DrawTilePoints(tile, fr, ht, camPos, altitudeKm);
    }

    WalkMetaTree(node->left_child, fr, ht, camPos, altitudeKm);
    WalkMetaTree(node->right_child, fr, ht, camPos, altitudeKm);
}

/* --- Orbit camera: (lon, lat, altitude) instead of viewer.c's free-fly
 * scheme, since "start at a respectful distance, move closer" only makes
 * sense with a genuine distance-from-center concept, which the star
 * viewer's camera has no notion of at all. --- */
typedef struct {
    double lon, lat;
    float altitude;
} OrbitCamera;

static void UpdateOrbitCamera(OrbitCamera *oc, Camera3D *camera, float deltaTime) {
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT)) {
        Vector2 d = GetMouseDelta();
        /* Drag sensitivity scales with altitude so panning feels proportional
         * at both a full-globe view and a close-up one. */
        double degPerPixel = 0.02 * (oc->altitude / EARTH_RADIUS_KM + 0.05);
        oc->lon -= d.x * degPerPixel;
        oc->lat += d.y * degPerPixel;
        if (oc->lat > 89.0) oc->lat = 89.0;
        if (oc->lat < -89.0) oc->lat = -89.0;
        if (oc->lon > 180.0) oc->lon -= 360.0;
        if (oc->lon < -180.0) oc->lon += 360.0;
    }

    float zoom = 1.0f - GetMouseWheelMove() * 0.1f;
    if (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP))   zoom *= (1.0f - 0.8f * deltaTime);
    if (IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) zoom *= (1.0f + 0.8f * deltaTime);
    oc->altitude *= zoom;
    if (oc->altitude < MIN_ALTITUDE_KM) oc->altitude = MIN_ALTITUDE_KM;
    if (oc->altitude > MAX_ALTITUDE_KM) oc->altitude = MAX_ALTITUDE_KM;

    Vector3 surfacePoint = LonLatToCartesian(oc->lon, oc->lat, EARTH_RADIUS_KM);
    Vector3 outward = Vector3Normalize(surfacePoint);
    camera->position = Vector3Add(surfacePoint, Vector3Scale(outward, oc->altitude));
    camera->target = (Vector3){ 0.0f, 0.0f, 0.0f };

    /* "Up" is the local north tangent direction, not a blindly-fixed world
     * axis -- projecting world-Y onto the tangent plane at the camera's
     * sub-point keeps the view from flipping/degenerating as you orbit
     * around, which a fixed (0,1,0) up would do near the poles. Only
     * undefined exactly at the poles, which the +/-89 degree clamp above
     * avoids. */
    Vector3 worldUp = (Vector3){ 0.0f, 1.0f, 0.0f };
    Vector3 north = Vector3Subtract(worldUp, Vector3Scale(outward, Vector3DotProduct(worldUp, outward)));
    camera->up = Vector3Normalize(north);
}

int main(void) {
    if (access("cities.metatree", R_OK) != 0) {
        printf("Error: cities.metatree not found in the current directory.\n");
        printf("Build it first (see README-cities.md):\n");
        printf("  ./geonames2kd cities1000.txt\n");
        printf("  ./build_city_metatree . cities\n");
        return 1;
    }

    printf("Loading city meta-index...\n");
    if (!LoadMetaTree(".")) return 1;
    printf("Meta-index ready: %zu tiles indexed.\n", g_manifest_count);
    g_tiles = calloc(g_manifest_count, sizeof(Tile));

    LoadNames("cities.names");

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(1280, 720, "Earth Cities Viewer");
    InitDotBatches(); /* needs a GL context, so after InitWindow */

    /* LoadTexture needs a GL context, so this has to happen after
     * InitWindow. Textured Earth is optional -- fall back to the plain
     * sphere below if the file isn't there or fails to load. */
    bool haveEarthTexture = false;
    Mesh *earthMeshes = NULL;
    int earthMeshCount = 0;
    Material earthMaterial = { 0 };
    if (access(EARTH_TEXTURE_PATH, R_OK) == 0) {
        Texture2D earthTexture = LoadTexture(EARTH_TEXTURE_PATH);
        if (earthTexture.id != 0) {
            /* LoadTexture defaults to GL_NEAREST with no mipmaps -- fine for
             * the old flat-color sphere, but it made the real texture look
             * blocky up close and would alias/shimmer at mid-range zoom.
             * Mipmaps + trilinear filtering fix both. */
            GenTextureMipmaps(&earthTexture);
            SetTextureFilter(earthTexture, TEXTURE_FILTER_TRILINEAR);

            earthMeshes = BuildEarthMeshes(EARTH_RADIUS_KM, EARTH_SPHERE_RINGS, EARTH_SPHERE_SLICES, &earthMeshCount);
            earthMaterial = LoadMaterialDefault();
            earthMaterial.maps[MATERIAL_MAP_ALBEDO].texture = earthTexture;
            haveEarthTexture = true;
            printf("Loaded Earth texture: %s (%d mesh band%s)\n", EARTH_TEXTURE_PATH, earthMeshCount, earthMeshCount == 1 ? "" : "s");
        } else {
            printf("Warning: found %s but couldn't load it as a texture; using a plain sphere.\n", EARTH_TEXTURE_PATH);
            printf("  (if it's a .jpg: raylib's JPEG loader may be disabled -- SUPPORT_FILEFORMAT_JPG\n");
            printf("  is off in a default raylib build. Enable it in raylib's config.h and rebuild,\n");
            printf("  or use a PNG texture instead. See README-cities.md.)\n");
        }
    } else {
        printf("No %s found; using a plain sphere (see README-cities.md to add a real texture).\n", EARTH_TEXTURE_PATH);
    }

    /* Start over Cody, Wyoming (44.52634 N, 109.05653 W, per cities1000.txt's
     * own entry for it) rather than an arbitrary point. EV_LON/EV_LAT/EV_ALT
     * override the starting camera position -- see test_earth_viewer_visual.sh,
     * which uses this (plus EV_SCREENSHOT below) to smoke-test rendering
     * without a human at the keyboard: there's no interactive test harness
     * for a GUI app, so this is the automatable substitute. All four are
     * no-ops when unset, so normal interactive runs are unaffected. */
    OrbitCamera oc = { .lon = -109.05653, .lat = 44.52634, .altitude = INITIAL_ALTITUDE_KM };
    if (getenv("EV_LON")) oc.lon = atof(getenv("EV_LON"));
    if (getenv("EV_LAT")) oc.lat = atof(getenv("EV_LAT"));
    if (getenv("EV_ALT")) oc.altitude = atof(getenv("EV_ALT"));
    const char *screenshotPath = getenv("EV_SCREENSHOT");
    int screenshotFrame = getenv("EV_SCREENSHOT_FRAME") ? atoi(getenv("EV_SCREENSHOT_FRAME")) : 30;
    int frameCount = 0;

    Camera3D camera = { 0 };
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    SetTargetFPS(60);
    const double nearClip = 1.0, farClip = 300000.0;
    rlSetClipPlanes(nearClip, farClip);

    while (!WindowShouldClose()) {
        frameCount++;
        float deltaTime = GetFrameTime();
        UpdateOrbitCamera(&oc, &camera, deltaTime);

        int currentWidth = GetScreenWidth();
        int currentHeight = GetScreenHeight();
        float aspect = (float)currentWidth / (float)currentHeight;

        Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);
        Matrix matProj = MatrixPerspective(camera.fovy * DEG2RAD, aspect, nearClip, farClip);
        Matrix matViewProj = MatrixMultiply(matView, matProj);
        Plane frustum[6];
        ExtractFrustumPlanes(matViewProj, frustum);
        HorizonTest horizonTest = ComputeHorizonTest(camera.position);

        Vector3 camForward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
        g_billboardRight = Vector3Normalize(Vector3CrossProduct(camForward, camera.up));
        g_billboardUp = Vector3Normalize(Vector3CrossProduct(g_billboardRight, camForward));

        g_points_drawn = 0;
        g_label_count = 0;
        for (int b = 0; b < g_dotBatchesUsed; b++) g_dotBatches[b].quadCount = 0;
        g_dotBatchesUsed = 0;

        BeginDrawing();
        ClearBackground((Color){ 5, 5, 15, 255 });

        BeginMode3D(camera);
            /* Backface culling stays on for the Earth mesh itself -- see
             * GenEarthSphereMeshBand's comment on triangle winding for the
             * bug this used to paper over by disabling culling entirely. */
            rlEnableBackfaceCulling();
            if (haveEarthTexture) {
                for (int i = 0; i < earthMeshCount; i++) {
                    DrawMesh(earthMeshes[i], earthMaterial, MatrixIdentity());
                }
            } else {
                DrawSphere((Vector3){ 0, 0, 0 }, EARTH_RADIUS_KM, (Color){ 25, 60, 95, 255 });
                DrawSphereWires((Vector3){ 0, 0, 0 }, EARTH_RADIUS_KM * 1.001f, 18, 36, (Color){ 255, 255, 255, 40 });
            }

            /* City-dot billboards are camera-facing quads, not a wound mesh
             * -- backface culling has to stay off for them regardless of
             * the Earth mesh's winding. WalkMetaTree/DrawCityPoint fill the
             * batch meshes below (see their comment); the actual upload and
             * draw happens after traversal completes. */
            rlDisableBackfaceCulling();
            WalkMetaTree(0, frustum, &horizonTest, camera.position, oc.altitude);
            for (int b = 0; b < g_dotBatchesUsed; b++) {
                DotBatch *batch = &g_dotBatches[b];
                if (batch->quadCount == 0) continue;
                UpdateMeshBuffer(batch->mesh, RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION,
                    batch->mesh.vertices, (int)(sizeof(float) * 3 * (size_t)batch->quadCount * 4), 0);
                batch->mesh.triangleCount = batch->quadCount * 2;
                DrawMesh(batch->mesh, g_dotMaterial, MatrixIdentity());
            }
            rlEnableBackfaceCulling();
        EndMode3D();

        /* Labels are plain 2D screen text, drawn after EndMode3D (not mixed
         * into the 3D immediate-mode batch above) via GetWorldToScreen --
         * every position here already passed the same horizon+frustum test
         * as its dot, so it's reliably in front of the camera. */
        for (int i = 0; i < g_label_count; i++) {
            Vector2 sp = GetWorldToScreen(g_labels[i].worldPos, camera);
            int tw = MeasureText(g_labels[i].name, 14);
            DrawRectangle((int)sp.x - 2, (int)sp.y - 2, tw + 4, 16, (Color){ 0, 0, 0, 140 });
            DrawText(g_labels[i].name, (int)sp.x, (int)sp.y, 14, RAYWHITE);
        }

        DrawFPS(10, 10);
        DrawText(TextFormat("Points drawn this frame: %zu", g_points_drawn), 10, 35, 18, GREEN);
        DrawText(TextFormat("Camera: lon=%.2f lat=%.2f altitude=%.0f km", oc.lon, oc.lat, oc.altitude), 10, 58, 16, RAYWHITE);
        DrawText(TextFormat("Tiles mmap'd so far: %zu / %zu", g_tiles_loaded_count, g_manifest_count), 10, 80, 16, RAYWHITE);
        DrawText("Controls: drag with left mouse to orbit, scroll/W-S to zoom", 10, currentHeight - 30, 14, SKYBLUE);

        EndDrawing();

        /* Screenshot is taken (and the exact filename) relative to the
         * process's working directory regardless of what path is given --
         * TakeScreenshot()/ExportImage() always prepend raylib's own
         * GetWorkingDirectory(), so EV_SCREENSHOT should just be a bare
         * filename; test_earth_viewer_visual.sh runs from citydata/ for
         * exactly this reason. */
        if (screenshotPath && frameCount == screenshotFrame) {
            TakeScreenshot(screenshotPath);
            break;
        }
    }

    if (haveEarthTexture) {
        for (int i = 0; i < earthMeshCount; i++) UnloadMesh(earthMeshes[i]);
        free(earthMeshes);
        UnloadMaterial(earthMaterial); /* also unloads earthMaterial's texture */
    }
    for (int b = 0; b < DOT_BATCH_COUNT; b++) UnloadMesh(g_dotBatches[b].mesh);
    UnloadMaterial(g_dotMaterial); /* untextured -- only unloads the shared default shader/texture references, both no-ops */
    CloseWindow();

    for (size_t i = 0; i < g_manifest_count; i++) {
        if (g_tiles[i].nodes) munmap(g_tiles[i].nodes, g_tiles[i].nodes_map_size);
        free(g_tiles[i].positions);
        free(g_manifest_paths[i]);
    }
    free(g_tiles);
    free(g_manifest_paths);
    munmap(g_meta_nodes, g_meta_nodes_map_size);

    for (size_t i = 0; i < g_name_entry_count; i++) free(g_name_entries[i].name);
    free(g_name_entries);

    printf("Viewer closed successfully.\n");
    return 0;
}
