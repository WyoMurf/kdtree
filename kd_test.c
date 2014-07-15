/*
 * K-d tree package testing program
 *
 * David Harrison
 * University of California, Berkeley
 * 1988
 *
 * This program puts k-d through its paces.  It builds a tree
 * of random boxes and performs a number of queries on it
 * comparing the results with those obtained from a linear
 * search of the same space.  It then deletes every item
 * from the tree and makes sure none are left.  If everything
 * is ok,  it returns zero.
 * 
 * With some improvements and expansion by Steve Murphy
 * from 1990 to 2014.
 */

#include "kd.h"
#include <sys/time.h>

/* for errtrap */
char *optProgName = "";

#define KD_BOXES	1000000
#define KD_REGIONS      1000

#define MIN_RANGE	-100000
#define MAX_RANGE	100000
#define RANGE_SPAN	(MAX_RANGE - MIN_RANGE + 1)
#define BOX_RANGE	1000

static kd_box boxes[KD_BOXES];
static void gen_boxes();
static int gen_box();
static void rand_box();

#define BOXINTERSECT(b1, b2) \
  (((b1)[KD_RIGHT] >= (b2)[KD_LEFT]) && \
   ((b2)[KD_RIGHT] >= (b1)[KD_LEFT]) && \
   ((b1)[KD_TOP] >= (b2)[KD_BOTTOM]) && \
   ((b2)[KD_TOP] >= (b1)[KD_BOTTOM]))

static void rand_box(kd_box box)
/* Generates a random box in `box' */
{
    static int init = 0;
    long random();

    if (!init) {
	struct timeval tp;
	struct timezone whocares;

	/* Randomize generator */
	(void) gettimeofday(&tp, &whocares);
	(void) srandom((int) (tp.tv_sec + tp.tv_usec));
	init = 1;
    }

    box[KD_LEFT] = (random() % RANGE_SPAN) + MIN_RANGE;
    box[KD_BOTTOM] = (random() % RANGE_SPAN) + MIN_RANGE;
    box[KD_RIGHT] = box[KD_LEFT] + (random() % BOX_RANGE);
    box[KD_TOP] = box[KD_BOTTOM] + (random() % BOX_RANGE);
}


static int gen_box(kd_generic arg, kd_generic *val, kd_box size)
/*
 * Generates next box for insertion into tree
 */
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
/* Generates a whole bunch of random boxes */
{
    int i;

    for (i = 0;  i < KD_BOXES;  i++) {
	rand_box(boxes[i]);
    }
}

main(int argc, char **argv)
{
    kd_tree tree;
    kd_box region, size;
    kd_gen gen;
    int local[KD_BOXES];
    int idx, i, j, k, n, item;

    int s1;
    long s2;
    char s3;
    short s4;
    long long s5;
    int *s6;
    char *s7;
    long *s8;
    short *s9;
    long long *s10;
    float s11;
    double s12;
    long double s13;
    float *s14;
    double *s15;
    long double *s16;

    printf(" sizeof(int) = %zd, sizeof(int*)=%zd\n", sizeof(s1), sizeof(s6));
    printf(" sizeof(long) = %zd, sizeof(long*)=%zd\n", sizeof(s2), sizeof(s8));
    printf(" sizeof(char) = %zd, sizeof(char*)=%zd\n", sizeof(s3), sizeof(s7));
    printf(" sizeof(short) = %zd, sizeof(short*)=%zd\n", sizeof(s4), sizeof(s9));
    printf(" sizeof(long long) = %zd, sizeof(long long*)=%zd\n", sizeof(s5), sizeof(s10));
    printf(" sizeof(float) = %zd, sizeof(float*)=%zd\n", sizeof(s11), sizeof(s14));
    printf(" sizeof(double) = %zd, sizeof(double*)=%zd\n", sizeof(s12), sizeof(s15));
    printf(" sizeof(long double) = %zd, sizeof(long double*)=%zd\n", sizeof(s13), sizeof(s16));

  
    optProgName = argv[0];
    gen_boxes();
    idx = 0;
    tree = kd_build(gen_box, (kd_generic) &idx);
   
    kd_badness(tree); 
    /* Phase one: random generations of boxes */
    for (i = 0;  i < KD_REGIONS;  i++) {
	rand_box(region);
	gen = kd_start(tree, region);
	n = 0;
	while (kd_next(gen, (kd_generic *) &(local[n]), size) == KD_OK) {
	    n++;
	}
	kd_finish(gen);
        if (i%100 == 0) printf("Region %d: %d boxes found\n", i, n);
	/* Now the test of truth: is it right? */
	for (j = 0;  j < KD_BOXES;  j++) {
	    if (BOXINTERSECT(region, boxes[j])) {
		for (k = 0;  k < n;  k++) {
		    if (local[k] == j+1) {
			local[k] = -1;
			break;
		    }
		}
		if (k >= n) errRaise("kd_test", 0, "missing item");
	    }
	}
	for (k = 0;  k < n;  k++) {
	    if (local[k] >= 0) errRaise("kd_test", 0, "extra item");
	}
    }
    printf("Phase one complete. %d Regions searched.\n", KD_REGIONS);

    /* Phase two:  delete each item in reverse order... */
    for (i = KD_BOXES-1;  i >= 0;  i--) {
	if (kd_delete(tree, (kd_generic) (long)(i+1), boxes[i]) != KD_OK) {
            printf("FAILED TO DELETE item %d\n", i);
	}
    }
    printf("Phase two complete. %d items deleted from the tree.\n", KD_BOXES);
    kd_badness(tree); 
    /* and ask for everything in the tree - shouldn't be any */
    region[KD_LEFT] = MIN_RANGE-1;
    region[KD_BOTTOM] = MIN_RANGE-1;
    region[KD_RIGHT] = MAX_RANGE+1;
    region[KD_TOP] = MAX_RANGE+1;
    gen = kd_start(tree, region);
    while (kd_next(gen, (kd_generic *) &item, size) == KD_OK) {
	errRaise("kd_test", 0, "incomplete delete");
    }
    printf("...And none were found in the tree afterwards!\n");
    kd_destroy(tree, NULL);

    gen_boxes();
    idx = 0;
    tree = kd_build(gen_box, (kd_generic) &idx);
   
    kd_badness(tree); 
    /* Phase one: random generations of boxes */
    for (i = 0;  i < KD_REGIONS;  i++) {
	rand_box(region);
	gen = kd_start(tree, region);
	n = 0;
	while (kd_next(gen, (kd_generic *) &(local[n]), size) == KD_OK) {
	    n++;
	}
	kd_finish(gen);
        if (i%100 == 0) printf("Region %d: %d boxes found\n", i, n);
	/* Now the test of truth: is it right? */
	for (j = 0;  j < KD_BOXES;  j++) {
	    if (BOXINTERSECT(region, boxes[j])) {
		for (k = 0;  k < n;  k++) {
		    if (local[k] == j+1) {
			local[k] = -1;
			break;
		    }
		}
		if (k >= n) errRaise("kd_test", 0, "missing item");
	    }
	}
	for (k = 0;  k < n;  k++) {
	    if (local[k] >= 0) errRaise("kd_test", 0, "extra item");
	}
    }
    printf("Phase one complete. %d Regions searched.\n", KD_REGIONS);
    int num_tries, num_del;
    int tot_tries, tot_dels;
    tot_tries = 0;
    tot_dels = 0;
    /* Phase two:  delete each item in reverse order... */
    int j2 = KD_BOXES -1;
    for (i = KD_BOXES-1;  i >= 0;  i--) {
	if (i != j2 || i <= 0) printf("Hey, i=%d; j2=%d;\n", i,j2);
        if (i%10000==0) printf("i=%d;\n", i);
	if (kd_really_delete(tree, (kd_generic) (long)(i+1), boxes[i], &num_tries, &num_del) != KD_OK) {
	    printf("FAILED TO REALLY DELETE item #%d, j2=%d\n", i,j2);
	    // errRaise("kd_test", 0, "failed to really delete item #%d", i);
	}
	j2--;
        tot_tries += num_tries;
        tot_dels += num_del;
    }
    printf("Phase two complete. %d items deleted from the tree; tries = %d; dels = %d\n", KD_BOXES, tot_tries, tot_dels);
    kd_badness(tree); 
    /* and ask for everything in the tree - shouldn't be any */
    region[KD_LEFT] = MIN_RANGE-1;
    region[KD_BOTTOM] = MIN_RANGE-1;
    region[KD_RIGHT] = MAX_RANGE+1;
    region[KD_TOP] = MAX_RANGE+1;
    gen = kd_start(tree, region);
    while (kd_next(gen, (kd_generic *) &item, size) == KD_OK) {
	errRaise("kd_test", 0, "incomplete delete");
    }
    printf("...And none were found in the tree afterwards!\n");
    kd_destroy(tree, NULL);





    return 0;
}


