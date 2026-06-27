/*
 * K-d tree test: nearest neighbor search (kd_nearest)
 *
 * Builds a tree of random boxes, then for several random query points,
 * finds the m nearest neighbors and verifies the results against a
 * brute-force linear scan.
 * Returns 0 on success, non-zero on failure.
 */

#include "kd.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>

#ifdef _WIN32
#define rand() rand()
#define srand(x) srand(x)
#endif

#define KD_BOXES	10000
#define NUM_QUERIES	100
#define MAX_NEIGHBORS	20

#define MIN_RANGE	-100000
#define MAX_RANGE	100000
#define RANGE_SPAN	(MAX_RANGE - MIN_RANGE + 1)
#define BOX_RANGE	1000

static kd_box boxes[KD_BOXES];

static void rand_box(kd_box box)
{
    static int init = 0;

    if (!init) {
	(void) srand((int) time(NULL));
	init = 1;
    }

    box[KD_LEFT] = (rand() % RANGE_SPAN) + MIN_RANGE;
    box[KD_BOTTOM] = (rand() % RANGE_SPAN) + MIN_RANGE;
    box[KD_FLOOR] = (rand() % RANGE_SPAN) + MIN_RANGE;
    box[KD_RIGHT] = box[KD_LEFT] + (rand() % BOX_RANGE);
    box[KD_TOP] = box[KD_BOTTOM] + (rand() % BOX_RANGE);
    box[KD_CEIL] = box[KD_FLOOR] + (rand() % BOX_RANGE);
}

static int gen_box(kd_generic arg, kd_generic *val, kd_box size)
{
    int *offsetp = ((int *) arg);
    int offset = *((int *) arg);

    if (offset < KD_BOXES) {
	*val = (kd_generic) (long)(offset+1);
	size[KD_LEFT] = boxes[offset][KD_LEFT];
	size[KD_BOTTOM] = boxes[offset][KD_BOTTOM];
	size[KD_FLOOR] = boxes[offset][KD_FLOOR];
	size[KD_RIGHT] = boxes[offset][KD_RIGHT];
	size[KD_TOP] = boxes[offset][KD_TOP];
	size[KD_CEIL] = boxes[offset][KD_CEIL];
	*offsetp += 1;	return 1;
    } else {
	return 0;
    }
}

static void gen_boxes(void)
{
    int i;
    for (i = 0;  i < KD_BOXES;  i++) {
	rand_box(boxes[i]);
    }
}

/*
 * Compute the edge-to-edge distance from point (qx,qy) to bounding box.
 * This must match the distance metric used by kd_nearest internally.
 */
static double box_dist(coord_t qx, coord_t qy, coord_t qz, kd_box box)
{
    double dx = 0.0, dy = 0.0, dz = 0.0;

    if (qx < box[KD_LEFT])
        dx = (double)box[KD_LEFT] - qx;
    else if (qx > box[KD_RIGHT])
        dx = (double)qx - box[KD_RIGHT];

    if (qy < box[KD_BOTTOM])
        dy = (double)box[KD_BOTTOM] - qy;
    else if (qy > box[KD_TOP])
        dy = (double)qy - box[KD_TOP];

    if (qz < box[KD_FLOOR])
        dz = (double)box[KD_FLOOR] - qz;
    else if (qz > box[KD_CEIL])
        dz = (double)qz - box[KD_CEIL];

    return sqrt(dx*dx + dy*dy + dz*dz);
}
/* Compare doubles for qsort */
static int cmp_double(const void *a, const void *b)
{
    double da = *(const double *)a;
    double db = *(const double *)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

int main(int argc, char **argv)
{
    kd_tree tree;
    int idx, i, q, m;
    kd_priority *list;
    double brute_dists[KD_BOXES];

    (void)argc; (void)argv;

    gen_boxes();
    idx = 0;
    tree = kd_build(gen_box, (kd_generic) &idx);

    printf("[nearest] Built tree with %d boxes\n", KD_BOXES);

    /* Test with various neighbor counts */
    for (m = 1; m <= MAX_NEIGHBORS; m *= 2) {
	for (q = 0; q < NUM_QUERIES; q++) {
	    coord_t qx = (rand() % RANGE_SPAN) + MIN_RANGE;
	    coord_t qy = (rand() % RANGE_SPAN) + MIN_RANGE;
	    coord_t qz = (rand() % RANGE_SPAN) + MIN_RANGE;
	    int visited;

	    visited = kd_nearest(tree, qx, qy, qz, m, &list);
	    (void)visited;

	    /* Verify results are sorted by distance (non-decreasing) */
	    for (i = 1; i < m; i++) {
		if (list[i].dist < list[i-1].dist - 1e-9) {
		    fprintf(stderr, "[nearest] FAIL: results not sorted at m=%d q=%d i=%d "
			    "(dist[%d]=%g > dist[%d]=%g)\n",
			    m, q, i, i-1, list[i-1].dist, i, list[i].dist);
		    free(list);
		    return 1;
		}
	    }

	    /* Brute-force: verify kd_nearest found the true m closest */
	    for (i = 0; i < KD_BOXES; i++) {
	        brute_dists[i] = box_dist(qx, qy, qz, boxes[i]);
	    }	    qsort(brute_dists, KD_BOXES, sizeof(double), cmp_double);

	    if (list[m-1].dist > brute_dists[m-1] + 1e-6) {
		fprintf(stderr, "[nearest] FAIL: kd_nearest missed closer item at m=%d q=%d "
			"(kd furthest=%g, brute m-th=%g)\n",
			m, q, list[m-1].dist, brute_dists[m-1]);
		free(list);
		return 1;
	    }

	    free(list);
	}
	printf("[nearest] m=%d: %d queries passed\n", m, NUM_QUERIES);
    }

    /* Edge case: query point inside a box (distance should be 0) */
    {
	coord_t qx = (boxes[0][KD_LEFT] + boxes[0][KD_RIGHT]) / 2;
	coord_t qy = (boxes[0][KD_BOTTOM] + boxes[0][KD_TOP]) / 2;
	coord_t qz = (boxes[0][KD_FLOOR] + boxes[0][KD_CEIL]) / 2;
	int visited = kd_nearest(tree, qx, qy, qz, 1, &list);
	(void)visited;
	if (list[0].dist > 1e-9) {
	    fprintf(stderr, "[nearest] FAIL: query inside box[0] but nearest dist=%g\n",
		    list[0].dist);
	    free(list);
	    return 1;
	}
	free(list);
	printf("[nearest] Edge case (point inside box): PASS\n");
    }

    printf("[nearest] All tests passed. PASS\n");
    kd_destroy(tree, NULL);

    return 0;
}
