#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <glob.h>
#include <limits.h>
#include "kdtree.h"

/*
 * 2D-f64 fork of build_metatree.c's exact "kd-tree of kd-trees" pattern,
 * applied to the HEALPix city tiles produced by geonames2kd instead of Gaia
 * star shards. Each item's box is a tile's own whole-file bounding box, read
 * via kd_2d_f64_get_serialized_bounds -- O(1), just the sentinel node
 * kd_serialize() appends to every file, never touches city data. Single-
 * threaded here (unlike build_metatree.c's thread pool): at ~1000 tiles this
 * finishes in a fraction of a second, so the added complexity isn't worth it.
 *
 * A meta-tree node's item value holds (manifest index + 1), not a geonameid
 * -- an index into the accompanying .manifest file (one absolute tile path
 * per line, indexed from 0). The +1 offset matters: this library treats an
 * item value of 0 (a NULL kd_generic) as "no data" and rejects it outright,
 * and manifest index 0 is a real, valid tile.
 */

typedef struct {
    double bbox[4];
    char *path;
} TileEntry;

typedef struct {
    TileEntry *entries;
    size_t total;
    size_t pos;
} BuildCtx;

static int item_func(kd_generic arg, kd_generic *val, kd_2d_f64_box size) {
    BuildCtx *ctx = (BuildCtx *)arg;
    if (ctx->pos >= ctx->total) return 0;
    *val = (kd_generic)(intptr_t)(ctx->pos + 1); /* +1: 0 would serialize as a NULL item */
    for (int d = 0; d < 4; d++) size[d] = ctx->entries[ctx->pos].bbox[d];
    ctx->pos++;
    return KD_OK;
}

int main(int argc, char **argv) {
    const char *dir = (argc > 1) ? argv[1] : ".";
    const char *prefix = (argc > 2) ? argv[2] : "cities";

    char pattern[600];
    snprintf(pattern, sizeof(pattern), "%s/city_tile_*.kdtree", dir);

    printf("Scanning %s...\n", pattern);
    glob_t g;
    if (glob(pattern, GLOB_ERR, NULL, &g) != 0 || g.gl_pathc == 0) {
        fprintf(stderr, "No tile files found matching %s\n", pattern);
        return 1;
    }
    size_t total = g.gl_pathc;
    printf("Found %zu tile files. Extracting whole-file bounds (O(1) per file via the sentinel node)...\n", total);

    TileEntry *entries = malloc(total * sizeof(TileEntry));
    if (!entries) { fprintf(stderr, "out of memory\n"); globfree(&g); return 1; }

    size_t valid = 0;
    for (size_t i = 0; i < total; i++) {
        const char *path = g.gl_pathv[i];
        kd_2d_f64_box bounds;
        if (kd_2d_f64_get_serialized_bounds(path, bounds) != KD_OK) {
            fprintf(stderr, "Warning: could not read bounds for %s, skipping.\n", path);
            continue;
        }
        char resolved[PATH_MAX];
        entries[valid].path = realpath(path, resolved) ? strdup(resolved) : strdup(path);
        for (int d = 0; d < 4; d++) entries[valid].bbox[d] = bounds[d];
        valid++;
    }
    globfree(&g);

    if (valid == 0) {
        fprintf(stderr, "Error: no tile yielded valid bounds, nothing to index.\n");
        free(entries);
        return 1;
    }
    if (valid < total) {
        printf("Warning: %zu of %zu tile files had unreadable bounds and were skipped.\n", total - valid, total);
    }

    printf("Building meta kd-tree over %zu tiles...\n", valid);
    BuildCtx ctx = { entries, valid, 0 };
    kd_tree tree = kd_2d_f64_build(item_func, (kd_generic)&ctx);
    if (!tree) {
        fprintf(stderr, "Error: kd_2d_f64_build failed.\n");
        return 1;
    }

    char metatree_path[512], manifest_path[512];
    snprintf(metatree_path, sizeof(metatree_path), "%s.metatree", prefix);
    snprintf(manifest_path, sizeof(manifest_path), "%s.manifest", prefix);

    printf("Serializing meta-tree to %s...\n", metatree_path);
    if (kd_2d_f64_serialize(tree, metatree_path) != 0) {
        fprintf(stderr, "Error: failed to serialize meta-tree to %s\n", metatree_path);
        kd_2d_f64_destroy(tree, NULL);
        return 1;
    }
    kd_2d_f64_destroy(tree, NULL);

    printf("Writing manifest to %s...\n", manifest_path);
    FILE *mf = fopen(manifest_path, "w");
    if (!mf) { perror("fopen manifest"); return 1; }
    for (size_t i = 0; i < valid; i++) {
        fprintf(mf, "%s\n", entries[i].path);
    }
    fclose(mf);

    printf("Done. %zu tiles indexed into %s (manifest: %s).\n", valid, metatree_path, manifest_path);
    return 0;
}
