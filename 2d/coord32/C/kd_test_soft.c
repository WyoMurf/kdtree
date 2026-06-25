/*
 * K-d tree test: soft delete (kd_delete)
 *
 * Builds a tree of random boxes, verifies search correctness,
 * then deletes every item using kd_delete (mark-as-deleted).
 * Returns 0 on success, non-zero on failure.
 */

#include "kd.h"
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32
#define random() rand()
#define srandom(x) srand(x)
#endif



#define KD_BOXES	1000000
#define KD_REGIONS      1000

#define MIN_RANGE	-100000
#define MAX_RANGE	100000
#define RANGE_SPAN	(MAX_RANGE - MIN_RANGE + 1)
#define BOX_RANGE	1000

static kd_box boxes[KD_BOXES];

#define BOXINTERSECT(b1, b2) \
  (((b1)[KD_RIGHT] >= (b2)[KD_LEFT]) && \
   ((b2)[KD_RIGHT] >= (b1)[KD_LEFT]) && \
   ((b1)[KD_TOP] >= (b2)[KD_BOTTOM]) && \
   ((b2)[KD_TOP] >= (b1)[KD_BOTTOM]))

static void rand_box(kd_box box)
{
    static int init = 0;

    if (!init) {
	(void) srandom((int) time(NULL));
	init = 1;
    }

    box[KD_LEFT] = (random() % RANGE_SPAN) + MIN_RANGE;
    box[KD_BOTTOM] = (random() % RANGE_SPAN) + MIN_RANGE;
    box[KD_RIGHT] = box[KD_LEFT] + (random() % BOX_RANGE);
    box[KD_TOP] = box[KD_BOTTOM] + (random() % BOX_RANGE);
}

static int gen_box(kd_generic arg, kd_generic *val, kd_box size)
{
    int *offsetp = ((int *) arg);
    int offset = *((int *) arg);

    if (offset < KD_BOXES) {
	*val = (kd_generic) (long)(offset+1);
	size[KD_LEFT] = boxes[offset][KD_LEFT];
	size[KD_BOTTOM] = boxes[offset][KD_BOTTOM];
	size[KD_RIGHT] = boxes[offset][KD_RIGHT];
	size[KD_TOP] = boxes[offset][KD_TOP];
	*offsetp += 1;
	return 1;
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

int main(int argc, char **argv)
{
    kd_tree tree;
    kd_box region, size;
    kd_gen gen;
    static int local[KD_BOXES];
    int idx, i, j, k, n, item;

    (void)argc; (void)argv;
    gen_boxes();
    idx = 0;
    tree = kd_build(gen_box, (kd_generic) &idx);

    kd_badness(tree);

    /* Phase one: search verification */
    for (i = 0;  i < KD_REGIONS;  i++) {
	rand_box(region);
	gen = kd_start(tree, region);
	n = 0;
	while (kd_next(gen, (kd_generic *) &(local[n]), size) == KD_OK) {
	    n++;
	}
	kd_finish(gen);
	if (i%100 == 0) printf("[soft] Region %d: %d boxes found\n", i, n);
	for (j = 0;  j < KD_BOXES;  j++) {
	    if (BOXINTERSECT(region, boxes[j])) {
		for (k = 0;  k < n;  k++) {
		    if (local[k] == j+1) {
			local[k] = -1;
			break;
		    }
		}
		if (k >= n) {
		    fprintf(stderr, "[soft] FAIL: missing item in search\n");
		    return 1;
		}
	    }
	}
	for (k = 0;  k < n;  k++) {
	    if (local[k] >= 0) {
		fprintf(stderr, "[soft] FAIL: extra item in search\n");
		return 1;
	    }
	}
    }
    printf("[soft] Search verification complete. %d regions searched.\n", KD_REGIONS);

    /* Phase two: soft delete every item */
    for (i = KD_BOXES-1;  i >= 0;  i--) {
	if (kd_delete(tree, (kd_generic) (long)(i+1), boxes[i]) != KD_OK) {
	    fprintf(stderr, "[soft] FAIL: could not delete item %d\n", i);
	    return 1;
	}
    }
    printf("[soft] Deleted %d items from the tree.\n", KD_BOXES);

    /* Verify tree is empty */
    region[KD_LEFT] = MIN_RANGE-1;
    region[KD_BOTTOM] = MIN_RANGE-1;
    region[KD_RIGHT] = MAX_RANGE+1;
    region[KD_TOP] = MAX_RANGE+1;
    gen = kd_start(tree, region);
    while (kd_next(gen, (kd_generic *) &item, size) == KD_OK) {
	fprintf(stderr, "[soft] FAIL: tree not empty after delete\n");
	return 1;
    }
    printf("[soft] Verified tree is empty. PASS\n");
    kd_destroy(tree, NULL);

    return 0;
}
