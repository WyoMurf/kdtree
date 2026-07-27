#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <glob.h>
#include <limits.h>
#include <unistd.h>
#include <pthread.h>
#include "kdtree.h"

// Builds a "kd-tree of kd-trees": a small meta-index whose items are shard
// files, not stars. Each item's box is the shard's own whole-file bounding
// box (read via kd_3d_64_get_serialized_bounds, which is O(1) - it just
// reads the sentinel bounds node kd_serialize() already appends to every
// shard, no need to touch any star data). The meta-tree itself is written
// with kd_3d_64_serialize(), so it's a completely ordinary 3D-64 kdtree file
// - kd2lod, or any other existing tool, works on it unmodified.
//
// A meta-tree node's source_id does not hold a Gaia source_id like a normal
// shard's leaves do; it holds (manifest index + 1) - an index into the
// accompanying .manifest file (one absolute shard path per line, indexed
// from 0) - so a consumer can recover which shard file a given meta-tree
// node refers to. The +1 offset matters: the library treats an item value
// of 0 (a NULL kd_generic) as "no data" and rejects it outright, and
// manifest index 0 is a real, valid shard.

typedef struct {
    int64_t bbox[6];
    char *path; // NULL if this slot's shard failed to yield bounds
} ShardEntry;

typedef struct {
    glob_t *g;
    size_t start, end;
    ShardEntry *entries;
} BoundsWorkerArgs;

static void *BoundsWorker(void *arg) {
    BoundsWorkerArgs *w = (BoundsWorkerArgs *)arg;
    for (size_t i = w->start; i < w->end; i++) {
        const char *path = w->g->gl_pathv[i];
        kd_3d_64_box bounds;
        if (kd_3d_64_get_serialized_bounds(path, bounds) != KD_OK) {
            w->entries[i].path = NULL;
            continue;
        }
        for (int d = 0; d < 6; d++) w->entries[i].bbox[d] = bounds[d];

        char resolved[PATH_MAX];
        w->entries[i].path = realpath(path, resolved) ? strdup(resolved) : strdup(path);
    }
    return NULL;
}

typedef struct {
    ShardEntry *entries; // compacted, valid entries only, indexed 0..total-1
    size_t total;
    size_t pos;
} BuildCtx;

static int item_func(kd_generic arg, kd_generic *val, kd_3d_64_box size) {
    BuildCtx *ctx = (BuildCtx *)arg;
    if (ctx->pos >= ctx->total) return 0;
    *val = (kd_generic)(intptr_t)(ctx->pos + 1); // +1: 0 would serialize as a NULL item
    for (int d = 0; d < 6; d++) size[d] = ctx->entries[ctx->pos].bbox[d];
    ctx->pos++;
    return KD_OK;
}

int main(int argc, char **argv) {
    const char *dir = (argc > 1) ? argv[1] : ".";
    const char *prefix = (argc > 2) ? argv[2] : "catalog";

    char pattern[600];
    snprintf(pattern, sizeof(pattern), "%s/GaiaSource_Filtered_*-[0-9].kdtree", dir);

    printf("Scanning %s...\n", pattern);
    glob_t g;
    if (glob(pattern, GLOB_ERR, NULL, &g) != 0 || g.gl_pathc == 0) {
        fprintf(stderr, "No shard files found matching %s\n", pattern);
        return 1;
    }
    size_t total = g.gl_pathc;
    printf("Found %zu shard files. Extracting whole-file bounds (O(1) per file via the sentinel node)...\n", total);

    ShardEntry *entries = malloc(total * sizeof(ShardEntry));

    long nproc = sysconf(_SC_NPROCESSORS_ONLN);
    int num_threads = (nproc > 0) ? (int)nproc : 8;
    if (num_threads > 32) num_threads = 32;
    if ((size_t)num_threads > total) num_threads = (int)total;
    if (num_threads < 1) num_threads = 1;

    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    BoundsWorkerArgs *workers = malloc(num_threads * sizeof(BoundsWorkerArgs));
    size_t chunk = (total + num_threads - 1) / num_threads;
    for (int t = 0; t < num_threads; t++) {
        workers[t].g = &g;
        workers[t].start = (size_t)t * chunk;
        workers[t].end = workers[t].start + chunk;
        if (workers[t].end > total) workers[t].end = total;
        workers[t].entries = entries;
        pthread_create(&threads[t], NULL, BoundsWorker, &workers[t]);
    }
    for (int t = 0; t < num_threads; t++) pthread_join(threads[t], NULL);
    free(threads);
    free(workers);
    globfree(&g);

    // Compact out any shard whose bounds we couldn't read, preserving order.
    size_t valid = 0;
    for (size_t i = 0; i < total; i++) {
        if (!entries[i].path) continue;
        if (valid != i) entries[valid] = entries[i];
        valid++;
    }
    if (valid < total) {
        printf("Warning: %zu of %zu shard files had unreadable bounds and were skipped.\n", total - valid, total);
    }
    if (valid == 0) {
        fprintf(stderr, "Error: no shard yielded valid bounds, nothing to index.\n");
        free(entries);
        return 1;
    }

    printf("Building meta kd-tree over %zu shards...\n", valid);
    BuildCtx ctx = { entries, valid, 0 };
    kd_tree tree = kd_3d_64_build(item_func, (kd_generic)&ctx);
    if (!tree) {
        fprintf(stderr, "Error: kd_3d_64_build failed.\n");
        return 1;
    }

    char metatree_path[512], manifest_path[512];
    snprintf(metatree_path, sizeof(metatree_path), "%s.metatree", prefix);
    snprintf(manifest_path, sizeof(manifest_path), "%s.manifest", prefix);

    printf("Serializing meta-tree to %s...\n", metatree_path);
    if (kd_3d_64_serialize(tree, metatree_path) != 0) {
        fprintf(stderr, "Error: failed to serialize meta-tree to %s\n", metatree_path);
        kd_3d_64_destroy(tree, NULL);
        return 1;
    }
    kd_3d_64_destroy(tree, NULL);

    printf("Writing manifest to %s...\n", manifest_path);
    FILE *mf = fopen(manifest_path, "w");
    if (!mf) {
        perror("fopen manifest");
        return 1;
    }
    for (size_t i = 0; i < valid; i++) {
        fprintf(mf, "%s\n", entries[i].path);
    }
    fclose(mf);

    printf("Done. %zu shards indexed into %s (manifest: %s).\n", valid, metatree_path, manifest_path);
    printf("Next: run kd2lod on %s to get subtree bounds for meta-tree traversal.\n", metatree_path);
    return 0;
}
