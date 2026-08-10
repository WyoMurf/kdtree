#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "kdtree.h"
#include "geo_utils.h"

/*
 * Reads a GeoNames "cities1000.txt"-style tab-separated dump (geonameid,
 * name, asciiname, alternatenames, latitude, longitude, feature class,
 * feature code, country code, cc2, admin1..4, population, elevation, dem,
 * timezone, modification date) and buckets every row into a HEALPix level-3
 * NESTED cell (768 cells total) via geo_utils.h's healpix_nested_index(),
 * building one kd_2d_f64 tree per non-empty cell (city_tile_<cellid>.kdtree).
 * Also writes cities.names, a flat geonameid -> name/population lookup for
 * the viewer's label rendering (kd-tree items here are just the geonameid,
 * matching how the Gaia shards use source_id -- names/populations live in
 * this side file instead).
 */

#define HEALPIX_LEVEL 3
#define NUM_CELLS (12 * (1 << (2 * HEALPIX_LEVEL))) /* 12 * nside^2 = 768 */

typedef struct {
    uint64_t geonameid;
    double lon, lat;
} CityPoint;

typedef struct {
    CityPoint *points;
    int count;
    int capacity;
} Cell;

typedef struct {
    CityPoint *points;
    int index;
    int count;
} BuildCtx;

/* kd_2d_f64_build's item callback: hands back one point per call via an
 * index+count closure (rather than a sentinel value baked into the data,
 * since a geonameid of 0 -- which this library's convention treats as
 * NULL/no-data -- is not something we can rule out as cleanly as build_metatree.c
 * can rule out manifest index 0 with a +1 offset). */
static int item_func(kd_generic arg, kd_generic *val, kd_2d_f64_box size) {
    BuildCtx *ctx = (BuildCtx *)arg;
    if (ctx->index >= ctx->count) return 0;
    CityPoint *p = &ctx->points[ctx->index];
    *val = (kd_generic)(intptr_t)p->geonameid;
    size[0] = p->lon; /* LEFT */
    size[1] = p->lat; /* BOTTOM */
    size[2] = p->lon; /* RIGHT */
    size[3] = p->lat; /* TOP */
    ctx->index++;
    return KD_OK;
}

static void add_point(Cell *cell, uint64_t geonameid, double lon, double lat) {
    if (cell->count >= cell->capacity) {
        size_t new_capacity = cell->capacity ? (size_t)cell->capacity * 2 : 16;
        CityPoint *new_points = realloc(cell->points, new_capacity * sizeof(CityPoint));
        if (!new_points) {
            fprintf(stderr, "add_point: out of memory growing cell to %zu points\n", new_capacity);
            exit(1);
        }
        cell->points = new_points;
        cell->capacity = (int)new_capacity;
    }
    cell->points[cell->count].geonameid = geonameid;
    cell->points[cell->count].lon = lon;
    cell->points[cell->count].lat = lat;
    cell->count++;
}

typedef struct { double value; int idx; } SortEntry;

static int cmp_sort_entry(const void *a, const void *b) {
    double da = ((const SortEntry *)a)->value, db = ((const SortEntry *)b)->value;
    return (da > db) - (da < db);
}

/* kd_build's median-selection heuristic (2d/kd.c's sel_k) faults with "bad
 * median" when two points end up exactly tied on the axis currently being
 * partitioned -- not just full (lon,lat) duplicates, but any two points that
 * merely share an exact value on ONE axis, which real-world GeoNames data
 * hits often enough (rounded/coincidental coordinates) to make ingestion
 * fail outright. This is a pre-existing limitation of that shared library
 * code, not something to work around by modifying it here. Instead, before
 * building each cell's tree, deterministically nudge any tied values on a
 * given axis apart by the smallest amount needed (processing in sorted
 * order and bumping anything <= its predecessor to predecessor+1e-9 degrees,
 * ~0.1mm -- imperceptible, and this chains correctly through runs of more
 * than two ties since each comparison is against the already-bumped
 * previous value, not the original). */
static void dedupe_axis(CityPoint *points, int count, int use_lon) {
    if (count < 2) return;
    SortEntry *entries = malloc((size_t)count * sizeof(SortEntry));
    if (!entries) {
        fprintf(stderr, "dedupe_axis: out of memory sorting %d points\n", count);
        exit(1);
    }
    for (int i = 0; i < count; i++) {
        entries[i].value = use_lon ? points[i].lon : points[i].lat;
        entries[i].idx = i;
    }
    qsort(entries, (size_t)count, sizeof(SortEntry), cmp_sort_entry);
    for (int i = 1; i < count; i++) {
        if (entries[i].value <= entries[i - 1].value) {
            entries[i].value = entries[i - 1].value + 1e-9;
            if (use_lon) points[entries[i].idx].lon = entries[i].value;
            else points[entries[i].idx].lat = entries[i].value;
        }
    }
    free(entries);
}

int main(int argc, char **argv) {
    const char *input_path = (argc > 1) ? argv[1] : "cities1000.txt";
    const char *names_path = "cities.names";

    FILE *in = fopen(input_path, "r");
    if (!in) { perror(input_path); return 1; }

    FILE *names = fopen(names_path, "w");
    if (!names) { perror(names_path); fclose(in); return 1; }

    Cell *cells = calloc(NUM_CELLS, sizeof(Cell));
    if (!cells) { fprintf(stderr, "out of memory\n"); fclose(in); fclose(names); return 1; }

    char line[4096];
    long total_rows = 0, bad_rows = 0;
    while (fgets(line, sizeof(line), in)) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) line[--len] = '\0';
        if (len == 0) continue;

        char *cols[20];
        int ncols = 0;
        char *p = line;
        cols[ncols++] = p;
        while (*p && ncols < 20) {
            if (*p == '\t') {
                *p = '\0';
                cols[ncols++] = p + 1;
            }
            p++;
        }
        if (ncols < 15) { bad_rows++; continue; }

        uint64_t geonameid = strtoull(cols[0], NULL, 10);
        const char *name = cols[1];
        double lat = atof(cols[4]);
        double lon = atof(cols[5]);
        long population = atol(cols[14]);

        total_rows++;
        fprintf(names, "%llu\t%s\t%ld\n", (unsigned long long)geonameid, name, population);

        uint64_t cell_id = healpix_nested_index(lon, lat, HEALPIX_LEVEL);
        if (cell_id >= NUM_CELLS) { bad_rows++; continue; } /* defensive; shouldn't happen for valid lat/lon */
        add_point(&cells[cell_id], geonameid, lon, lat);
    }
    fclose(in);
    fclose(names);

    printf("Parsed %ld rows (%ld skipped as malformed).\n", total_rows, bad_rows);

    int tiles_written = 0;
    long points_written = 0;
    for (int c = 0; c < NUM_CELLS; c++) {
        if (cells[c].count == 0) continue;
        dedupe_axis(cells[c].points, cells[c].count, 1); /* lon */
        dedupe_axis(cells[c].points, cells[c].count, 0); /* lat */
        BuildCtx ctx = { cells[c].points, 0, cells[c].count };
        kd_tree tree = kd_2d_f64_build(item_func, (kd_generic)&ctx);
        if (!tree) {
            fprintf(stderr, "Warning: kd_2d_f64_build failed for cell %d (%d points), skipping.\n", c, cells[c].count);
            continue;
        }
        char path[256];
        snprintf(path, sizeof(path), "city_tile_%d.kdtree", c);
        if (kd_2d_f64_serialize(tree, path) != 0) {
            fprintf(stderr, "Warning: failed to serialize %s\n", path);
        } else {
            tiles_written++;
            points_written += cells[c].count;
        }
        kd_2d_f64_destroy(tree, NULL);
    }

    printf("Wrote %d city_tile_*.kdtree files covering %ld points, plus %s.\n", tiles_written, points_written, names_path);

    for (int c = 0; c < NUM_CELLS; c++) free(cells[c].points);
    free(cells);
    return 0;
}
