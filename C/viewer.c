#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <glob.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <math.h>
#include <pthread.h>

#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"
#include "kdtree.h"
#include "kd2lod.h"

// Must match fits2kd.c's fixed-point scale (parsecs -> integer units).
#define SCALE_FACTOR 1000000000.0f

// One loaded shard: a mmap'd .kdtree node array, plus (if present and
// valid) the mmap'd .kdtree.lod sidecar of per-node subtree bounds/counts
// produced by kd2lod. Both files use the same pre-order indexing, so
// shard->lod[i] describes the subtree rooted at shard->nodes[i].
typedef struct {
    kd_3d_64_mmap_node *nodes;
    size_t nodes_map_size;
    size_t real_count;     // tree nodes, excluding the trailing bounds sentinel

    kd2lod_record *lod;    // NULL if no valid sidecar was found
    void *lod_map_base;    // the actual mmap() base (lod points past the header)
    size_t lod_map_size;
} Shard;

static Shard *g_shards = NULL;
static size_t g_shard_count = 0;

// A soft circular glow sprite used to billboard every star (see
// CreateStarTexture). g_billboardRight/g_billboardUp are the camera's actual
// right/up vectors, recomputed once per frame (not per star) so each star's
// quad always faces the camera regardless of view direction - fixes stars
// degenerating into visible line segments up close, since a world-space
// line segment (the original approach) only looks round from angles where
// it foreshortens away.
static Texture2D g_starTexture;
static Vector3 g_billboardRight = { 1.0f, 0.0f, 0.0f };
static Vector3 g_billboardUp = { 0.0f, 1.0f, 0.0f };

// Per-frame stats, reset at the top of each render pass.
static size_t g_points_drawn = 0;
static size_t g_nodes_expanded = 0;
static size_t g_nodes_collapsed = 0;

// LOD aggressiveness: a subtree collapses to one representative point once
// its angular size (as seen from the camera) drops below this many screen
// pixels. Smaller = more detail + slower, larger = coarser + faster. Tuned
// live with '[' / ']' and nudged automatically toward a comfortable frame time.
static float g_lod_pixel_target = 2.0f;
#define LOD_PIXEL_TARGET_MIN 0.25f
#define LOD_PIXEL_TARGET_MAX 64.0f

// Hard safety valve: some subtrees (e.g. a parallax segment spanning
// thousands of parsecs) have a bounding box large enough that angular-size
// culling alone doesn't collapse them from certain camera positions, which
// would otherwise make a single frame's traversal unboundedly expensive and
// freeze the window. Once this many points are drawn in a frame, every
// further CullAndCollect call returns immediately - the frame always
// finishes quickly, and the auto-adapt below reacts on the next frame.
#define FRAME_POINT_BUDGET 1500000
static int g_budget_hit = 0;

typedef struct { float a, b, c, d; } Plane;

// Gribb-Hartmann frustum plane extraction from a combined view*projection
// matrix. raylib's Matrix stores rows as (m0,m4,m8,m12), (m1,m5,m9,m13),
// (m2,m6,m10,m14), (m3,m7,m11,m15); planes are (row3 +/- rowN).
static void ExtractFrustumPlanes(Matrix m, Plane out[6]) {
    out[0] = (Plane){ m.m3+m.m0, m.m7+m.m4, m.m11+m.m8,  m.m15+m.m12 }; // left
    out[1] = (Plane){ m.m3-m.m0, m.m7-m.m4, m.m11-m.m8,  m.m15-m.m12 }; // right
    out[2] = (Plane){ m.m3+m.m1, m.m7+m.m5, m.m11+m.m9,  m.m15+m.m13 }; // bottom
    out[3] = (Plane){ m.m3-m.m1, m.m7-m.m5, m.m11-m.m9,  m.m15-m.m13 }; // top
    out[4] = (Plane){ m.m3+m.m2, m.m7+m.m6, m.m11+m.m10, m.m15+m.m14 }; // near
    out[5] = (Plane){ m.m3-m.m2, m.m7-m.m6, m.m11-m.m10, m.m15-m.m14 }; // far
}

// Positive-vertex (p-vertex) box/frustum test: for each plane, pick the AABB
// corner most aligned with the plane's normal. If even that corner is on the
// negative side, the whole box is outside this plane (and thus the frustum).
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

static inline Vector3 NodeStarPos(const kd_3d_64_mmap_node *n) {
    // Exact mathematical center from the 3D bounding box [min, max].
    return (Vector3){
        (float)(n->size[0] + n->size[3]) / (2.0f * SCALE_FACTOR),
        (float)(n->size[1] + n->size[4]) / (2.0f * SCALE_FACTOR),
        (float)(n->size[2] + n->size[5]) / (2.0f * SCALE_FACTOR)
    };
}

// Physically-motivated apparent-brightness mapping: closer = brighter/larger.
// When a single point stands in for a collapsed subtree of hidden_count
// unresolved stars, brighten/enlarge it a bit so dense regions still read
// as dense even from far away, instead of looking like one dim star.
static void StarBrightness(float dist, uint32_t hidden_count, unsigned char *alpha, float *size) {
    unsigned char a;
    float sz;

    if (dist < 50.0f) {
        a = 255;
        sz = 0.20f;
    } else if (dist > 3000.0f) {
        a = 45;
        sz = 0.01f;
    } else {
        float t = (dist - 50.0f) / 2950.0f;
        a = (unsigned char)(255.0f - t * 210.0f);
        sz = 0.20f - t * 0.19f;
    }

    if (hidden_count > 1) {
        float boost = 20.0f * log2f((float)hidden_count);
        int boosted = (int)a + (int)boost;
        a = (boosted > 255) ? 255 : (unsigned char)boosted;
        sz += 0.02f * log2f((float)hidden_count);
    }

    *alpha = a;
    *size = sz;
}

// Draws a camera-facing quad (billboarded via g_billboardRight/g_billboardUp,
// recomputed once per frame) textured with g_starTexture's soft radial glow.
// Looks round from every viewing angle and distance, unlike a fixed-axis
// world-space line segment.
static inline void DrawStarPoint(Vector3 pos, float dist, uint32_t hidden_count) {
    unsigned char alpha;
    float size;
    StarBrightness(dist, hidden_count, &alpha, &size);

    Vector3 right = Vector3Scale(g_billboardRight, size);
    Vector3 up = Vector3Scale(g_billboardUp, size);
    Vector3 p0 = Vector3Subtract(Vector3Subtract(pos, right), up); // bottom-left
    Vector3 p1 = Vector3Subtract(Vector3Add(pos, right), up);      // bottom-right
    Vector3 p2 = Vector3Add(Vector3Add(pos, right), up);           // top-right
    Vector3 p3 = Vector3Add(Vector3Subtract(pos, right), up);      // top-left

    rlColor4ub(255, 245, 218, alpha); // Warm yellow-white glowing stars
    rlTexCoord2f(0.0f, 1.0f); rlVertex3f(p0.x, p0.y, p0.z);
    rlTexCoord2f(1.0f, 1.0f); rlVertex3f(p1.x, p1.y, p1.z);
    rlTexCoord2f(1.0f, 0.0f); rlVertex3f(p2.x, p2.y, p2.z);
    rlTexCoord2f(0.0f, 0.0f); rlVertex3f(p3.x, p3.y, p3.z);
    g_points_drawn++;
}

// The core LOD walk. Every node holds a real star, so "collapsing" just
// means: draw this node's own star (boosted to represent its whole subtree)
// and stop, instead of recursing into children to draw them individually.
static void CullAndCollect(const Shard *shard, int64_t idx, const Plane fr[6], Vector3 camPos, float angleThreshold) {
    if (idx < 0) return;
    if (g_points_drawn >= FRAME_POINT_BUDGET) { g_budget_hit = 1; return; }

    const kd2lod_record *rec = &shard->lod[idx];
    Vector3 bmin = {
        (float)rec->min[0] / SCALE_FACTOR,
        (float)rec->min[1] / SCALE_FACTOR,
        (float)rec->min[2] / SCALE_FACTOR
    };
    Vector3 bmax = {
        (float)rec->max[0] / SCALE_FACTOR,
        (float)rec->max[1] / SCALE_FACTOR,
        (float)rec->max[2] / SCALE_FACTOR
    };

    if (AABBOutsideFrustum(fr, bmin, bmax)) return;

    Vector3 ext = { bmax.x - bmin.x, bmax.y - bmin.y, bmax.z - bmin.z };
    float diag = sqrtf(ext.x*ext.x + ext.y*ext.y + ext.z*ext.z);
    Vector3 center = { (bmin.x+bmax.x)*0.5f, (bmin.y+bmax.y)*0.5f, (bmin.z+bmax.z)*0.5f };
    float centerDist = Vector3Distance(camPos, center);
    if (centerDist < 0.001f) centerDist = 0.001f;
    float angularSize = diag / centerDist;

    const kd_3d_64_mmap_node *node = &shard->nodes[idx];
    Vector3 starPos = NodeStarPos(node);
    float starDist = Vector3Distance(camPos, starPos);

    if (angularSize < angleThreshold) {
        DrawStarPoint(starPos, starDist, rec->count);
        g_nodes_collapsed++;
        return;
    }

    DrawStarPoint(starPos, starDist, 1);
    g_nodes_expanded++;

    CullAndCollect(shard, node->left_child,  fr, camPos, angleThreshold);
    CullAndCollect(shard, node->right_child, fr, camPos, angleThreshold);
}

// Fallback for shards with no (or a stale/mismatched) .lod sidecar: draw
// every star exactly as the original viewer did. Correct but slow with no
// frustum culling - run kd2lod on the shard's .kdtree file to speed it up.
static void DrawShardBruteForce(const Shard *shard, Vector3 camPos) {
    for (size_t i = 0; i < shard->real_count; i++) {
        if (g_points_drawn >= FRAME_POINT_BUDGET) { g_budget_hit = 1; return; }
        const kd_3d_64_mmap_node *node = &shard->nodes[i];
        if (node->source_id == 0) continue;
        Vector3 starPos = NodeStarPos(node);
        float dist = Vector3Distance(camPos, starPos);
        DrawStarPoint(starPos, dist, 1);
    }
}

// Custom camera flying controls
void UpdateFreeCamera(Camera3D *camera, float *speed, float deltaTime) {
    // Keyboard speed adjustments (exponential)
    if (IsKeyDown(KEY_UP)) *speed *= 1.05f;
    if (IsKeyDown(KEY_DOWN)) *speed /= 1.05f;

    // Clamp speed to reasonable ranges (0.1 to 1000 parsecs per second)
    if (*speed < 0.1f) *speed = 0.1f;
    if (*speed > 1000.0f) *speed = 1000.0f;

    // Calculate direction vectors
    Vector3 forward = Vector3Subtract(camera->target, camera->position);
    float distance = Vector3Length(forward);
    forward = Vector3Scale(forward, 1.0f / distance); // Normalize

    Vector3 right = Vector3CrossProduct(forward, camera->up);
    right = Vector3Normalize(right);

    // Movement direction input mapping
    Vector3 move = { 0 };
    if (IsKeyDown(KEY_W)) move = Vector3Add(move, forward);
    if (IsKeyDown(KEY_S)) move = Vector3Subtract(move, forward);
    if (IsKeyDown(KEY_D)) move = Vector3Add(move, right);
    if (IsKeyDown(KEY_A)) move = Vector3Subtract(move, right);
    if (IsKeyDown(KEY_E)) move = Vector3Add(move, camera->up);
    if (IsKeyDown(KEY_Q)) move = Vector3Subtract(move, camera->up);

    if (Vector3Length(move) > 0.0f) {
        move = Vector3Normalize(move);
        Vector3 displacement = Vector3Scale(move, (*speed) * deltaTime);
        camera->position = Vector3Add(camera->position, displacement);
        camera->target = Vector3Add(camera->target, displacement);
    }

    // Camera rotation using mouse clicks & drags
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 mouseDelta = GetMouseDelta();
        if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) {
            float sensitivity = 0.003f;
            float angleX = -mouseDelta.x * sensitivity;
            float angleY = -mouseDelta.y * sensitivity;

            // Rotate around Up axis (yaw)
            Vector3 targetOffset = Vector3Subtract(camera->target, camera->position);
            targetOffset = Vector3RotateByAxisAngle(targetOffset, camera->up, angleX);

            // Rotate around Right axis (pitch)
            targetOffset = Vector3RotateByAxisAngle(targetOffset, right, angleY);

            camera->target = Vector3Add(camera->position, targetOffset);
        }
    }
}

// Opening, fstat-ing and mmap-ing tens of thousands of shard files is
// disk-I/O-bound, not CPU-bound (each call blocks on the filesystem, not on
// computation) - the same reason kd2lod's batch annotation run was ~3.6x
// faster at 32-way parallelism than sequential. Loading shards one at a time
// on a single thread at startup showed the same per-file cost and, at the
// full catalog's ~33,500 files, projects to 20+ minutes before the window
// even appears - long enough for the window manager to call it "not
// responding". LoadOneShard() does the per-file work; LoadWorker() runs it
// across a slice of the file list on its own thread.
typedef struct {
    double weight;
    Vector3 center;
    size_t star_count;
    int has_lod;
} ShardLoadResult;

static int LoadOneShard(const char *path, Shard *out, ShardLoadResult *result) {
    out->nodes = NULL;
    out->lod = NULL;
    out->lod_map_base = NULL;
    out->lod_map_size = 0;
    result->weight = 0;
    result->has_lod = 0;
    result->star_count = 0;
    result->center = (Vector3){ 0.0f, 0.0f, 0.0f };

    int fd = open(path, O_RDONLY);
    if (fd == -1) return 0;

    struct stat sb;
    if (fstat(fd, &sb) == -1 || sb.st_size == 0) {
        close(fd);
        return 0;
    }

    size_t node_count_total = sb.st_size / sizeof(kd_3d_64_mmap_node);
    if (node_count_total == 0) {
        close(fd);
        return 0;
    }

    kd_3d_64_mmap_node *nodes = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd); // mapping stays valid after the fd is closed
    if (nodes == MAP_FAILED) return 0;

    size_t real_count = node_count_total;
    if (nodes[node_count_total - 1].source_id == UINT64_MAX) {
        real_count = node_count_total - 1; // exclude the O(1) bounds sentinel
    }
    if (real_count == 0) {
        munmap(nodes, sb.st_size);
        return 0;
    }

    out->nodes = nodes;
    out->nodes_map_size = sb.st_size;
    out->real_count = real_count;

    // Look for a sidecar .kdtree.lod file annotated by kd2lod. It must match
    // this exact source file (same size, same node count) or we ignore it
    // and fall back to brute-force rendering for this shard.
    char lod_path[600];
    snprintf(lod_path, sizeof(lod_path), "%s.lod", path);
    int lod_fd = open(lod_path, O_RDONLY);
    if (lod_fd != -1) {
        struct stat lod_sb;
        if (fstat(lod_fd, &lod_sb) == 0 && (size_t)lod_sb.st_size >= sizeof(kd2lod_header)) {
            void *lod_map = mmap(NULL, lod_sb.st_size, PROT_READ, MAP_PRIVATE, lod_fd, 0);
            if (lod_map != MAP_FAILED) {
                kd2lod_header *hdr = (kd2lod_header *)lod_map;
                size_t expected_size = sizeof(kd2lod_header) + (size_t)hdr->node_count * sizeof(kd2lod_record);
                if (hdr->magic == KD2LOD_MAGIC && hdr->version == KD2LOD_VERSION &&
                    hdr->source_size == (uint64_t)sb.st_size &&
                    hdr->node_count == (uint64_t)real_count &&
                    (size_t)lod_sb.st_size == expected_size) {
                    out->lod = (kd2lod_record *)((uint8_t *)lod_map + sizeof(kd2lod_header));
                    out->lod_map_base = lod_map;
                    out->lod_map_size = lod_sb.st_size;
                } else {
                    munmap(lod_map, lod_sb.st_size);
                }
            }
        }
        close(lod_fd);
    }

    // Camera-target accumulation: use the exact root-subtree centroid when
    // LOD data is available, else fall back to this shard's own root star
    // position weighted by node count.
    if (out->lod) {
        kd2lod_record *root = &out->lod[0];
        result->center = (Vector3){
            (float)((root->min[0] + root->max[0]) / 2) / SCALE_FACTOR,
            (float)((root->min[1] + root->max[1]) / 2) / SCALE_FACTOR,
            (float)((root->min[2] + root->max[2]) / 2) / SCALE_FACTOR
        };
        result->weight = (double)root->count;
        result->has_lod = 1;
        result->star_count = root->count;
    } else {
        result->center = NodeStarPos(&out->nodes[0]);
        result->weight = (double)out->real_count;
        result->has_lod = 0;
        result->star_count = out->real_count;
    }
    return 1;
}

static size_t g_load_progress = 0; // shared across worker threads, updated atomically

typedef struct {
    glob_t *glob_result;
    size_t start, end;       // half-open slice of glob_result->gl_pathv this thread owns
    size_t files_to_load;    // total across all threads, for progress messages
    double sumX, sumY, sumZ, sumWeight;
    size_t total_known_stars;
    size_t shards_without_lod;
} LoadWorkerArgs;

static void *LoadWorker(void *arg) {
    LoadWorkerArgs *w = (LoadWorkerArgs *)arg;

    for (size_t i = w->start; i < w->end; i++) {
        ShardLoadResult result;
        if (LoadOneShard(w->glob_result->gl_pathv[i], &g_shards[i], &result)) {
            w->sumX += result.center.x * result.weight;
            w->sumY += result.center.y * result.weight;
            w->sumZ += result.center.z * result.weight;
            w->sumWeight += result.weight;
            w->total_known_stars += result.star_count;
            if (!result.has_lod) w->shards_without_lod++;
        }

        size_t done = __sync_add_and_fetch(&g_load_progress, 1);
        if (done % 2000 == 0 || done == w->files_to_load) {
            printf("Loaded %zu / %zu shard files...\n", done, w->files_to_load);
        }
    }
    return NULL;
}

// A small soft-edged circular glow sprite: opaque white core fading to fully
// transparent at the edge. Tinted per-star via rlColor4ub in DrawStarPoint,
// so this only supplies the round shape/falloff, not the color itself.
static Texture2D CreateStarTexture(void) {
    Image img = GenImageGradientRadial(64, 64, 0.15f, WHITE, (Color){ 255, 255, 255, 0 });
    Texture2D tex = LoadTextureFromImage(img);
    UnloadImage(img);
    GenTextureMipmaps(&tex);
    SetTextureFilter(tex, TEXTURE_FILTER_TRILINEAR); // avoids shimmering as distant stars shrink sub-pixel
    return tex;
}

int main(int argc, char **argv) {
    long max_files = -1; // -1 means no limit (all available files)
    if (argc > 1) {
        if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
            printf("Usage: %s [max_files_to_load]\n", argv[0]);
            printf("  max_files_to_load: a positive integer limiting the number of .kdtree files loaded\n");
            return 0;
        }
        char *endptr;
        long val = strtol(argv[1], &endptr, 10);
        if (*endptr != '\0' || val <= 0) {
            printf("Error: invalid limit '%s'. It must be a positive integer.\n", argv[1]);
            printf("Usage: %s [max_files_to_load]\n", argv[0]);
            return 1;
        }
        max_files = val;
    }

    // fits2kd partitions each chunk's stars into 10 parallax-segment trees.
    // Per fits2kd.c's PARALLAX_RANGES, segment -9 covers parallax 20-1,000,000
    // mas, i.e. distance ~0-50pc - the closest band, the solar neighborhood.
    // (Segment -0 is the opposite end: parallax 0-0.5 mas, ~2000pc out to the
    // catalog's detection limit - the farthest, sparsest, faintest stars.)
    // Loading only -9 both limits data volume and gives the densest, brightest
    // part of the sky to fly through. The other 9 segments (and the bare
    // per-chunk "full" tree, which just re-partitions the same stars across
    // all distances) are skipped entirely.
    glob_t glob_result;
    printf("Scanning current directory for GaiaSource_Filtered_*-9.kdtree files (closest/solar-neighborhood segment)...\n");
    int glob_ret = glob("GaiaSource_Filtered_*-9.kdtree", GLOB_ERR, NULL, &glob_result);

    if (glob_ret != 0 || glob_result.gl_pathc == 0) {
        printf("No local .kdtree files found. Scanning /backup/stars2/...\n");
        if (glob_ret == 0) globfree(&glob_result);
        glob_ret = glob("/backup/stars2/GaiaSource_Filtered_*-9.kdtree", GLOB_ERR, NULL, &glob_result);
    }

    if (glob_ret != 0 || glob_result.gl_pathc == 0) {
        printf("No files found in /backup/stars2/. Scanning /backup/star-catalogs/...\n");
        if (glob_ret == 0) globfree(&glob_result);
        glob_ret = glob("/backup/star-catalogs/GaiaSource_Filtered_*-9.kdtree", GLOB_ERR, NULL, &glob_result);
    }

    if (glob_ret != 0 || glob_result.gl_pathc == 0) {
        printf("Error: No GaiaSource_Filtered_*-9.kdtree files found in current directory, /backup/stars2/, or /backup/star-catalogs/.\n");
        printf("Please run the pipeline or build_kdtrees.sh first.\n");
        return 1;
    }

    printf("Found %zu KD-Tree files on disk.\n", glob_result.gl_pathc);
    size_t files_to_load = glob_result.gl_pathc;
    if (max_files > 0) {
        if ((size_t)max_files < files_to_load) {
            files_to_load = (size_t)max_files;
        }
        printf("Limiting load to the first %zu files as specified.\n", files_to_load);
    } else {
        printf("Loading all %zu files.\n", files_to_load);
    }

    // g_shards is indexed 1:1 with glob_result.gl_pathv; a slot whose
    // .nodes is NULL means that file failed to load and is skipped
    // everywhere else (render loop, cleanup). This lets worker threads
    // write to disjoint slots with no locking.
    g_shards = malloc(files_to_load * sizeof(Shard));
    g_shard_count = files_to_load;

    long nproc = sysconf(_SC_NPROCESSORS_ONLN);
    int num_threads = (nproc > 0) ? (int)nproc : 8;
    if (num_threads > 32) num_threads = 32;
    if ((size_t)num_threads > files_to_load) num_threads = (int)files_to_load;
    if (num_threads < 1) num_threads = 1;

    printf("Loading shards using %d threads (I/O-bound: opening/mmap-ing %zu files is far faster in parallel)...\n",
           num_threads, files_to_load);

    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    LoadWorkerArgs *workers = calloc(num_threads, sizeof(LoadWorkerArgs));
    size_t chunk = (files_to_load + num_threads - 1) / num_threads;

    for (int t = 0; t < num_threads; t++) {
        workers[t].glob_result = &glob_result;
        workers[t].start = (size_t)t * chunk;
        workers[t].end = workers[t].start + chunk;
        if (workers[t].end > files_to_load) workers[t].end = files_to_load;
        workers[t].files_to_load = files_to_load;
        pthread_create(&threads[t], NULL, LoadWorker, &workers[t]);
    }

    double sumX = 0, sumY = 0, sumZ = 0, sumWeight = 0;
    size_t total_known_stars = 0;
    size_t shards_without_lod = 0;
    for (int t = 0; t < num_threads; t++) {
        pthread_join(threads[t], NULL);
        sumX += workers[t].sumX;
        sumY += workers[t].sumY;
        sumZ += workers[t].sumZ;
        sumWeight += workers[t].sumWeight;
        total_known_stars += workers[t].total_known_stars;
        shards_without_lod += workers[t].shards_without_lod;
    }
    free(threads);
    free(workers);

    globfree(&glob_result);

    if (total_known_stars == 0) {
        printf("Error: Loaded 0 shards. Cannot launch viewer.\n");
        free(g_shards);
        return 1;
    }

    Vector3 avgPos = (sumWeight > 0)
        ? (Vector3){ (float)(sumX / sumWeight), (float)(sumY / sumWeight), (float)(sumZ / sumWeight) }
        : (Vector3){ 0.0f, 0.0f, 0.0f };

    printf("Successfully loaded %zu shards, ~%zu total stars (%zu shards missing LOD data).\n",
           g_shard_count, total_known_stars, shards_without_lod);
    printf("Average direction of loaded catalog sector: (%.1f, %.1f, %.1f) pc\n", avgPos.x, avgPos.y, avgPos.z);
    printf("Launching Raylib 3D Viewer...\n");

    // Initialize Raylib window
    const int screenWidth = 1280;
    const int screenHeight = 720;
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(screenWidth, screenHeight, "Gaia 3D Star Catalog Visualizer");

    g_starTexture = CreateStarTexture();

    // Setup camera pointing directly at the center of the loaded stars sector!
    Camera3D camera = { 0 };
    camera.position = (Vector3){ 0.0f, 0.0f, 0.0f }; // Centered at Sun/Earth
    camera.target = avgPos; // Look directly at the loaded star sector
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    float speed = 20.0f; // Speed in parsecs per second
    SetTargetFPS(60);

    // Set custom near/far clip planes so distant stars up to 50,000 pc are visible
    const double nearClip = 0.1, farClip = 50000.0;
    rlSetClipPlanes(nearClip, farClip);

    while (!WindowShouldClose()) {
        float deltaTime = GetFrameTime();

        // Handle custom free-flying camera controls
        UpdateFreeCamera(&camera, &speed, deltaTime);

        // Handle window resizing
        int currentWidth = GetScreenWidth();
        int currentHeight = GetScreenHeight();
        float aspect = (float)currentWidth / (float)currentHeight;

        // Manual LOD tuning: '[' = more detail/slower, ']' = coarser/faster
        if (IsKeyDown(KEY_LEFT_BRACKET))  g_lod_pixel_target *= (1.0f - 1.5f * deltaTime);
        if (IsKeyDown(KEY_RIGHT_BRACKET)) g_lod_pixel_target *= (1.0f + 1.5f * deltaTime);

        // Gentle auto-adaptation toward a comfortable frame time
        if (deltaTime > 1.0f / 30.0f) g_lod_pixel_target *= 1.01f;
        else if (deltaTime < 1.0f / 55.0f) g_lod_pixel_target *= 0.998f;

        if (g_lod_pixel_target < LOD_PIXEL_TARGET_MIN) g_lod_pixel_target = LOD_PIXEL_TARGET_MIN;
        if (g_lod_pixel_target > LOD_PIXEL_TARGET_MAX) g_lod_pixel_target = LOD_PIXEL_TARGET_MAX;

        // Build this frame's view frustum for subtree culling
        Matrix matView = MatrixLookAt(camera.position, camera.target, camera.up);
        Matrix matProj = MatrixPerspective(camera.fovy * DEG2RAD, aspect, nearClip, farClip);
        Matrix matViewProj = MatrixMultiply(matView, matProj);
        Plane frustum[6];
        ExtractFrustumPlanes(matViewProj, frustum);

        // Subtrees collapse once they'd subtend fewer than this many pixels
        float radPerPixel = (camera.fovy * DEG2RAD) / (float)currentHeight;
        float angleThreshold = radPerPixel * g_lod_pixel_target;

        // Recomputed once per frame (not per star) so every star's billboard
        // faces the camera regardless of view direction.
        Vector3 camForward = Vector3Normalize(Vector3Subtract(camera.target, camera.position));
        g_billboardRight = Vector3Normalize(Vector3CrossProduct(camForward, camera.up));
        g_billboardUp = Vector3Normalize(Vector3CrossProduct(g_billboardRight, camForward));

        g_points_drawn = 0;
        g_nodes_expanded = 0;
        g_nodes_collapsed = 0;
        g_budget_hit = 0;

        BeginDrawing();
        ClearBackground(BLACK);

        BeginMode3D(camera);
            // Draw axis reference for spatial awareness (X=Red, Y=Green, Z=Blue)
            DrawLine3D((Vector3){0,0,0}, (Vector3){100,0,0}, RED);
            DrawLine3D((Vector3){0,0,0}, (Vector3){0,100,0}, GREEN);
            DrawLine3D((Vector3){0,0,0}, (Vector3){0,0,100}, BLUE);

            // Walk each shard's kd-tree: cull whole subtrees against the view
            // frustum, collapse distant ones to a single representative
            // point, and only descend to individual stars where it matters.
            // Stars are billboarded quads (see DrawStarPoint), so backface
            // culling is disabled for this batch - a quad built from the
            // camera's own right/up vectors always faces the camera, but
            // winding order isn't worth reasoning about for something this cheap.
            rlDisableBackfaceCulling();
            rlSetTexture(g_starTexture.id);
            rlBegin(RL_QUADS);
            for (size_t s = 0; s < g_shard_count; s++) {
                Shard *shard = &g_shards[s];
                if (!shard->nodes) continue; // this file failed to load at startup
                if (shard->lod) {
                    CullAndCollect(shard, 0, frustum, camera.position, angleThreshold);
                } else {
                    DrawShardBruteForce(shard, camera.position);
                }
            }
            rlEnd();
            rlSetTexture(rlGetTextureIdDefault());
            rlEnableBackfaceCulling();
        EndMode3D();

        // If the hard point budget was hit this frame, some subtree's bounding
        // box was too large relative to distance for angular-size culling to
        // collapse it fast enough - jump the LOD threshold up sharply rather
        // than waiting on the gentle auto-adapt below, so it recovers in a
        // frame or two instead of staying pegged at the budget cap.
        if (g_budget_hit) g_lod_pixel_target *= 1.5f;

        // Render HUD Overlay
        DrawFPS(10, 10);
        DrawText(TextFormat("Points Drawn This Frame: %zu%s (tree nodes: %zu expanded / %zu collapsed)",
                             g_points_drawn, g_budget_hit ? " [BUDGET CAP HIT]" : "",
                             g_nodes_expanded, g_nodes_collapsed), 10, 35, 18, g_budget_hit ? ORANGE : GREEN);
        DrawText(TextFormat("Catalog Total: ~%zu stars across %zu shards", total_known_stars, g_shard_count), 10, 58, 16, RAYWHITE);
        DrawText(TextFormat("Cam Position: (%.1f, %.1f, %.1f) pc", camera.position.x, camera.position.y, camera.position.z), 10, 80, 16, RAYWHITE);
        DrawText(TextFormat("Looking at Loaded Sector: (%.1f, %.1f, %.1f) pc", camera.target.x, camera.target.y, camera.target.z), 10, 100, 16, RAYWHITE);
        DrawText(TextFormat("Flight Speed: %.1f pc/s", speed), 10, 120, 16, YELLOW);
        DrawText(TextFormat("LOD Target: %.2f px/subtree", g_lod_pixel_target), 10, 140, 16, YELLOW);

        DrawText("Controls:", 10, currentHeight - 110, 16, SKYBLUE);
        DrawText("W / S / A / D / Q / E : Fly Forward/Backward/Left/Right/Up/Down", 10, currentHeight - 90, 14, RAYWHITE);
        DrawText("Mouse Left/Right Click & Drag: Orbit / Look Around", 10, currentHeight - 70, 14, RAYWHITE);
        DrawText("UP / DOWN Arrow Keys : Adjust flight speed (exponentially)", 10, currentHeight - 50, 14, RAYWHITE);
        DrawText("[ / ] : More detail (slower) / Coarser detail (faster)", 10, currentHeight - 30, 14, RAYWHITE);

        EndDrawing();
    }

    UnloadTexture(g_starTexture);
    CloseWindow();

    for (size_t s = 0; s < g_shard_count; s++) {
        if (!g_shards[s].nodes) continue; // this file failed to load, nothing to unmap
        munmap(g_shards[s].nodes, g_shards[s].nodes_map_size);
        if (g_shards[s].lod_map_base) munmap(g_shards[s].lod_map_base, g_shards[s].lod_map_size);
    }
    free(g_shards);

    printf("Viewer closed successfully.\n");
    return 0;
}
