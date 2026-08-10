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
 * Deliberately simpler than viewer.c's LOD system: at ~150 tiles and ~170k
 * total points (vs. Gaia's ~33,500 shards and 157M stars), there's no need
 * for kd2lod's subtree-bbox-annotation/angular-collapse machinery -- every
 * meta-tree node is visited and tested every frame (a full scan of ~150
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
#define EARTH_SPHERE_RINGS 90
#define EARTH_SPHERE_SLICES 180

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

/* Builds a UV-sphere textured with an equirectangular Earth image, using the
 * SAME LonLatToCartesian() that places every city dot -- so the texture and
 * the dots are guaranteed to agree on where a given (lonDeg, latDeg) lands,
 * the same way the camera and the dots were made to agree when the
 * east-west mirroring bug was fixed. UV coordinates are derived from the
 * raw (lonDeg, latDeg) fed into that function, not from the Cartesian
 * result, so the vertex-position negation-of-longitude trick inside
 * LonLatToCartesian has no effect on texture alignment. Standard
 * equirectangular layout: u=0 at lon=-180 (west edge), u=1 at lon=+180,
 * increasing eastward; v=0 at the north pole (lat=+90), v=1 at the south
 * pole (lat=-90). */
static Mesh GenEarthSphereMesh(float radiusKm, int rings, int slices) {
    int ringVerts = slices + 1;
    int vertexCount = (rings + 1) * ringVerts;
    int triangleCount = rings * slices * 2;

    Mesh mesh = { 0 };
    mesh.vertexCount = vertexCount;
    mesh.triangleCount = triangleCount;
    mesh.vertices = RL_MALLOC(sizeof(float) * 3 * (size_t)vertexCount);
    mesh.normals = RL_MALLOC(sizeof(float) * 3 * (size_t)vertexCount);
    mesh.texcoords = RL_MALLOC(sizeof(float) * 2 * (size_t)vertexCount);
    mesh.indices = RL_MALLOC(sizeof(unsigned short) * 3 * (size_t)triangleCount);

    int v = 0;
    for (int r = 0; r <= rings; r++) {
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

    int idx = 0;
    for (int r = 0; r < rings; r++) {
        for (int s = 0; s < slices; s++) {
            unsigned short a = (unsigned short)(r * ringVerts + s);
            unsigned short b = (unsigned short)(a + ringVerts);
            unsigned short c = (unsigned short)(a + 1);
            unsigned short d = (unsigned short)(b + 1);
            mesh.indices[idx++] = a; mesh.indices[idx++] = c; mesh.indices[idx++] = b;
            mesh.indices[idx++] = c; mesh.indices[idx++] = d; mesh.indices[idx++] = b;
        }
    }

    UploadMesh(&mesh, false);
    return mesh;
}

/* Standard sphere-horizon test: for a camera outside a sphere of radius R
 * centered at the origin, a surface point is on the visible (near) side iff
 * the angle between its direction from center and the camera's direction
 * from center is within the tangent-line angle to the horizon, i.e.
 * cos(angle) >= R / distance(camera, center). This is what actually delivers
 * "any view of Earth only shows half or less of the grid" -- frustum culling
 * alone doesn't know the sphere is opaque. */
static int IsAboveHorizon(Vector3 pointOnSphere, Vector3 camPos) {
    float d = Vector3Length(camPos);
    if (d <= EARTH_RADIUS_KM) return 1; /* camera at/under the surface: don't occlude (shouldn't happen in practice) */
    float cosThreshold = EARTH_RADIUS_KM / d;
    float cosAngle = Vector3DotProduct(Vector3Normalize(pointOnSphere), Vector3Normalize(camPos));
    return cosAngle >= cosThreshold;
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
static int CellVisible(double lonMin, double latMin, double lonMax, double latMax, const Plane fr[6], Vector3 camPos) {
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
            if (IsAboveHorizon(p, camPos)) anyAboveHorizon = 1;
        }
    }
    if (!anyAboveHorizon) return 0;
    return !AABBOutsideFrustum(fr, bmin, bmax);
}

/* --- Name/population lookup: cities.names is small enough (~170k rows, a
 * few MB) to load entirely into memory, unlike the tile/meta-tree files. --- */
typedef struct NameEntry {
    uint64_t geonameid;
    char *name;
    long population;
    struct NameEntry *next;
} NameEntry;

#define NAME_HASH_BUCKETS 262144
static NameEntry *g_name_buckets[NAME_HASH_BUCKETS];

static void LoadNames(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { printf("Warning: could not open %s, city names will be unavailable.\n", path); return; }
    char line[512];
    long loaded = 0;
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

        NameEntry *e = malloc(sizeof(NameEntry));
        e->geonameid = strtoull(line, NULL, 10);
        e->name = strdup(nameStart);
        e->population = atol(popStart);
        size_t bucket = e->geonameid % NAME_HASH_BUCKETS;
        e->next = g_name_buckets[bucket];
        g_name_buckets[bucket] = e;
        loaded++;
    }
    fclose(f);
    printf("Loaded %ld city names.\n", loaded);
}

static NameEntry *LookupName(uint64_t geonameid) {
    NameEntry *e = g_name_buckets[geonameid % NAME_HASH_BUCKETS];
    while (e) {
        if (e->geonameid == geonameid) return e;
        e = e->next;
    }
    return NULL;
}

/* --- Per-tile mmap cache, same lazy-load-once-and-keep pattern as
 * viewer.c's Shard/EnsureShardLoaded (no eviction -- at ~150 tiles totaling
 * maybe a few MB, keeping them all mapped for the process lifetime once
 * touched is not a concern the way it was for ~33,500 star shards). --- */
typedef struct {
    kd_2d_f64_mmap_node *nodes;
    size_t nodes_map_size;
    size_t real_count;
    int attempted;
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
            lines = realloc(lines, cap * sizeof(char *));
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
    g_tiles_loaded_count++;
    return tile;
}

/* --- Rendering --- */

static Vector3 g_billboardRight = { 1.0f, 0.0f, 0.0f };
static Vector3 g_billboardUp = { 0.0f, 1.0f, 0.0f };
static size_t g_points_drawn = 0;

/* A fixed world-space size looks fine from far away (shrinks correctly with
 * perspective) but is far too large up close: real neighboring towns in a
 * densely-settled region can be closer together than a generously-sized
 * fixed marker, so their billboards overlap and merge into a solid blob
 * instead of showing as distinct dots. Sizing by a roughly-constant angular
 * radius (world size = angle * distance) keeps markers a sensible, mostly
 * distance-independent size on screen instead, clamped so it neither
 * vanishes at planetary range nor balloons absurdly at very low altitude. */
static float CityMarkerWorldSize(float distKm, long population) {
    float popBoost = 1.0f + 0.15f * log10f((population > 10) ? (float)population : 10.0f);
    float angularRadius = 0.0009f * popBoost;
    float sz = angularRadius * distKm;
    if (sz < 0.3f) sz = 0.3f;
    if (sz > 150.0f) sz = 150.0f;
    return sz;
}

static void DrawCityPoint(Vector3 pos, float sizeKm) {
    Vector3 right = Vector3Scale(g_billboardRight, sizeKm);
    Vector3 up = Vector3Scale(g_billboardUp, sizeKm);
    Vector3 p0 = Vector3Subtract(Vector3Subtract(pos, right), up);
    Vector3 p1 = Vector3Subtract(Vector3Add(pos, right), up);
    Vector3 p2 = Vector3Add(Vector3Add(pos, right), up);
    Vector3 p3 = Vector3Add(Vector3Subtract(pos, right), up);

    rlColor4ub(255, 210, 90, 255); /* warm amber marker dots */
    rlTexCoord2f(0.0f, 1.0f); rlVertex3f(p0.x, p0.y, p0.z);
    rlTexCoord2f(1.0f, 1.0f); rlVertex3f(p1.x, p1.y, p1.z);
    rlTexCoord2f(1.0f, 0.0f); rlVertex3f(p2.x, p2.y, p2.z);
    rlTexCoord2f(0.0f, 0.0f); rlVertex3f(p3.x, p3.y, p3.z);
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
static void DrawTilePoints(const Tile *tile, const Plane fr[6], Vector3 camPos, float altitudeKm) {
    for (size_t i = 0; i < tile->real_count; i++) {
        const kd_2d_f64_mmap_node *node = &tile->nodes[i];
        if (node->source_id == 0) continue;
        double lon = (node->size[0] + node->size[2]) / 2.0;
        double lat = (node->size[1] + node->size[3]) / 2.0;
        Vector3 pos = LonLatToCartesian(lon, lat, EARTH_RADIUS_KM);
        if (!IsAboveHorizon(pos, camPos)) continue;
        if (!PointInFrustum(fr, pos)) continue;

        NameEntry *ne = LookupName(node->source_id);
        long population = ne ? ne->population : 0;
        float distKm = Vector3Distance(camPos, pos);
        DrawCityPoint(pos, CityMarkerWorldSize(distKm, population));

        if (ne && g_label_count < MAX_LABELS_PER_FRAME) {
            /* Steep population curve so labels appear progressively, the way
             * a map app would: tiny villages (~1000 people) only label once
             * you're within ~50km; a capital-sized city (~3-10M) labels from
             * several hundred km out. A flatter/higher-based curve (tried
             * first) made every hamlet within view label at once, an
             * unreadable wall of text -- see the ingestion plan notes on
             * this being tuned to taste, easy to adjust further. */
            float popf = (population > 10) ? (float)population : 10.0f;
            float labelThresholdKm = 60.0f * log10f(popf) - 130.0f;
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
 * this always visits every node rather than pruning subtrees; at ~150
 * entries that's trivial. */
static void WalkMetaTree(int64_t idx, const Plane fr[6], Vector3 camPos, float altitudeKm) {
    if (idx < 0) return;
    const kd_2d_f64_mmap_node *node = &g_meta_nodes[idx];

    if (CellVisible(node->size[0], node->size[1], node->size[2], node->size[3], fr, camPos)) {
        uint64_t manifestIdx = node->source_id - 1;
        Tile *tile = EnsureTileLoaded(manifestIdx);
        if (tile) DrawTilePoints(tile, fr, camPos, altitudeKm);
    }

    WalkMetaTree(node->left_child, fr, camPos, altitudeKm);
    WalkMetaTree(node->right_child, fr, camPos, altitudeKm);
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

    /* LoadTexture needs a GL context, so this has to happen after
     * InitWindow. Textured Earth is optional -- fall back to the plain
     * sphere below if the file isn't there or fails to load. */
    bool haveEarthTexture = false;
    Texture2D earthTexture = { 0 };
    Model earthModel = { 0 };
    if (access(EARTH_TEXTURE_PATH, R_OK) == 0) {
        earthTexture = LoadTexture(EARTH_TEXTURE_PATH);
        if (earthTexture.id != 0) {
            Mesh earthMesh = GenEarthSphereMesh(EARTH_RADIUS_KM, EARTH_SPHERE_RINGS, EARTH_SPHERE_SLICES);
            earthModel = LoadModelFromMesh(earthMesh);
            earthModel.materials[0].maps[MATERIAL_MAP_ALBEDO].texture = earthTexture;
            haveEarthTexture = true;
            printf("Loaded Earth texture: %s\n", EARTH_TEXTURE_PATH);
        } else {
            printf("Warning: found %s but couldn't load it as a texture; using a plain sphere.\n", EARTH_TEXTURE_PATH);
        }
    } else {
        printf("No %s found; using a plain sphere (see README-cities.md to add a real texture).\n", EARTH_TEXTURE_PATH);
    }

    /* Start over Cody, Wyoming (44.52634 N, 109.05653 W, per cities1000.txt's
     * own entry for it) rather than an arbitrary point. */
    OrbitCamera oc = { .lon = -109.05653, .lat = 44.52634, .altitude = INITIAL_ALTITUDE_KM };
    Camera3D camera = { 0 };
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    SetTargetFPS(60);
    const double nearClip = 1.0, farClip = 300000.0;
    rlSetClipPlanes(nearClip, farClip);

    while (!WindowShouldClose()) {
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

        Vector3 camForward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
        g_billboardRight = Vector3Normalize(Vector3CrossProduct(camForward, camera.up));
        g_billboardUp = Vector3Normalize(Vector3CrossProduct(g_billboardRight, camForward));

        g_points_drawn = 0;
        g_label_count = 0;

        BeginDrawing();
        ClearBackground((Color){ 5, 5, 15, 255 });

        BeginMode3D(camera);
            /* Backface culling stays off for the Earth mesh draw too: at
             * close range, culling was (for reasons not fully pinned down --
             * possibly a Zink/NVK-specific winding/rasterization quirk,
             * confirmed unrelated to LonLatToCartesian or the mesh's own
             * vertex data via a minimal standalone repro) discarding the
             * near-side triangles and letting the antipodal far side win the
             * depth test instead. Disabling it costs nothing for one modest,
             * single, opaque mesh and empirically fixes it at every altitude
             * tested. */
            rlDisableBackfaceCulling();
            if (haveEarthTexture) {
                DrawModel(earthModel, (Vector3){ 0, 0, 0 }, 1.0f, WHITE);
            } else {
                DrawSphere((Vector3){ 0, 0, 0 }, EARTH_RADIUS_KM, (Color){ 25, 60, 95, 255 });
                DrawSphereWires((Vector3){ 0, 0, 0 }, EARTH_RADIUS_KM * 1.001f, 18, 36, (Color){ 255, 255, 255, 40 });
            }

            rlBegin(RL_QUADS);
            WalkMetaTree(0, frustum, camera.position, oc.altitude);
            rlEnd();
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
    }

    if (haveEarthTexture) {
        UnloadModel(earthModel);
        UnloadTexture(earthTexture);
    }
    CloseWindow();

    for (size_t i = 0; i < g_manifest_count; i++) {
        if (g_tiles[i].nodes) munmap(g_tiles[i].nodes, g_tiles[i].nodes_map_size);
        free(g_manifest_paths[i]);
    }
    free(g_tiles);
    free(g_manifest_paths);
    munmap(g_meta_nodes, g_meta_nodes_map_size);

    for (int b = 0; b < NAME_HASH_BUCKETS; b++) {
        NameEntry *e = g_name_buckets[b];
        while (e) {
            NameEntry *next = e->next;
            free(e->name);
            free(e);
            e = next;
        }
    }

    printf("Viewer closed successfully.\n");
    return 0;
}
