/*
 *
 * David Harrison
 * University of California,  Berkeley
 * 1988
 *
 * This is an implementation of k-d trees as described by Rosenberg
 * in "Geographical Data Structures", IEEE Transactions on CAD, Vol. CAD-4,
 * No. 1, January 1985.  His work is based on that of Jon Bentley in
 * "Multidimensional Binary Search Trees used for Associative Searching",
 * CACM, Vol. 18, No. 9, pp. 509-517, Sept. 1975.
 *
 *
 * extensive upgrades, enhancements, fixes, and optimizations made by
 * Steve Murphy. See Documentation in kd.c for information
 * on the routines contained herein:
 * A list of my changes:
 * + build used the nodes son's links to form lists, rather than the list package.
 *   This saves time in that malloc is called much less often.
 * + build uses the geometric mean criteria for finding central nodes, rather than
 *   the centroid of the bounding box. This, on the average, halves the depth of the
 *   tree. Research on random boxes shows that halving the depth of the tree decreases
 *   search traversal 15% Thus are kd trees resilient to degradation.
 * + Added nearest neighbor search routine. TODO: allow the user to pass in pointer
 *   to distance function.
 * + Added rebuild routine. Faster than a build from scratch.
 * + Added node deletion routine. For those purists who hate dead nodes in the tree.
 * + Some routines to give stats on tree health, info about tree, etc.
 * + I may even have inserted some comments to explain some tricky stuff happening
 *   in the code...

 TODO:
 generate a version that uses "buckets", to cut tree depth and make the in-memory rep more
 suitable for disk databasing and caching.
 Do a 3-d version --- NOTE: Done. available now. Oops, then lost!
 Do some disk read/write routines to make the data "persistent".
 */

#ifndef OCTTOOLS_COPYRIGHT_H
#define OCTTOOLS_COPYRIGHT_H
/*
 * Oct Tools Distribution 3.0
 *
 * Copyright (c) 1988, 1989, Regents of the University of California.
 * All rights reserved.
 *
 * Use and copying of this software and preparation of derivative works
 * based upon this software are permitted.  However, any distribution of
 * this software or derivative works must include the above copyright
 * notice.
 *
 * This software is made available AS IS, and neither the Electronics
 * Research Laboratory or the University of California make any
 * warranty about the software, its performance or its conformity to
 * any specification.
 *
 * Suggestions, comments, or improvements are welcome and should be
 * addressed to:
 *
 *   octtools@eros.berkeley.edu
 *   ..!ucbvax!eros!octtools

 * And, to Complicate matters, all modifications and additions done
 * to this code since Oct Tools 3.0, is:
 *
 * Copyright (c) 2014, Steven Michael Murphy, All rights reserved.
 *
 * murf@parsetree.com
 * 
 * This software is licensed to anyone wishing to use it, under
 * the LGPL v. 2.
 *
 * With the absolute same absence of any warranty or guarantee.
 * Use at your own risk. Worry at night if you do use it. 
 * Worry a LOT. And share horror stories and fixes with me, please!
 *
 */
#endif
/* Modern standard headers — replaces the old OctTools port.h portability layer */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include <limits.h>
#include <stdarg.h>
#include <inttypes.h>

#include "kd.h"

/*
 * Simple fatal error handler — replaces the OctTools errtrap package.
 * Library callers can check return codes; these are for truly fatal
 * conditions (out of memory, corrupted tree structure, etc.)
 */
static void kd_fatal(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "kd: fatal: ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);
    exit(1);
}
#if defined(COORD_64)
#define MAXINT	INT64_MAX
#define MININT	INT64_MIN
#else
#define MAXINT	INT32_MAX
#define MININT	INT32_MIN
#endif

#define MIN(a, b)	((a) < (b) ? (a) : (b))
#define MAX(a, b)	((a) > (b) ? (a) : (b))
#define ABS(a)		((a) < 0 ? -(a) : (a))

char *kd_pkg_name = "kd";

static char *mem_ret;		/* Memory allocation */
static int kd_data_tries;

/* Note: these are NOT multi-callable */
#define ALLOC(type) \
((mem_ret = malloc(sizeof(type))) ? (type *) mem_ret : (type *) kd_fault(KDF_M))

#define MULTALLOC(type, num) \
((mem_ret = malloc((unsigned) (sizeof(type) * (num)))) ? \
 (type *) mem_ret : (type *) kd_fault(KDF_M))

#define REALLOC(type, ptr, newsize) \
((mem_ret = realloc((char *) ptr, (unsigned) (sizeof(type) * (newsize)))) ? \
 (type *) mem_ret : (type *) kd_fault(KDF_M))

#define FREE(ptr)		free((char *) ptr)

#define BOXINTERSECT(b1, b2) \
  (((b1)[KD_RIGHT] >= (b2)[KD_LEFT]) && \
   ((b2)[KD_RIGHT] >= (b1)[KD_LEFT]) && \
   ((b1)[KD_TOP] >= (b2)[KD_BOTTOM]) && \
   ((b2)[KD_TOP] >= (b1)[KD_BOTTOM]) && \
   ((b1)[KD_CEIL] >= (b2)[KD_FLOOR]) && \
   ((b2)[KD_CEIL] >= (b1)[KD_FLOOR]))

static char kd_err_buf[1024];
static int kd_build_depth = 100000; /* can you imagine a tree deeper than this? */

#define Sprintf		(void) sprintf
#define Printf		(void) printf

/* Forward declarations */
/* Forward declarations moved after type definitions below */


/*
 * The kd_tree type is actually (KDTree *).
 */

#define KD_LOSON	0
#define KD_HISON	1

typedef struct {
    KDElem *tree;		/* K-d tree itself      */
    int item_count;		/* Number of nodes in tree */
    int dead_count;		/* Number of dead nodes */
	kd_box extent;      /* extents for the entire tree */
	int items_balanced; /* how many where in the tree when built */
} KDTree;

/*
 * K-d tree  generators are actually a pointer to (KDState).
 * Since these generators must trace the heirarchy
 * of the tree,  the generator contains a stack representing
 * the activation records of a depth first recursive tracing
 * of the tree.
 */

#define KD_INIT_STACK	15	/* Initial size of stack                */
#define KD_GROWSIZE(s)	10	/* Linear expansion                     */
#define	KD_THIS_ONE	-1	/* Indicates going through this element */
#define KD_DONE		2	/* Entirely done searching this element */

typedef struct kd_save {
    short disc;			/* Discriminator             */
    short state;		/* Current state (see above) */
    KDElem *item;		/* Element saved             */
	kd_box Bp;          /* for nearest neighbor, a saved bounds info */
	kd_box Bn;          /* for nearest neighbor, a saved bounds info */
} KDSave;

typedef struct kd_state {
    kd_box extent;		/* Search area 		     */
    short stack_size;		/* Allocated size of stack   */
    short top_index;		/* Top of the stack          */
    KDSave *stk;		/* Stack of active states    */
} KDState;



static char *kd_fault(int t)
/* Fatal faults - raises an error using the error handling package */
{
    switch(t)
	{
    case KDF_M:
	kd_fatal("out of memory");
	/* NOTREACHED */
	break;
    case KDF_ZEROID:
	kd_fatal("attempt to insert null data");
	/* NOTREACHED */
	break;
    case KDF_MD:
	kd_fatal("bad median");
	/* NOTREACHED */
	break;
    case KDF_F:
	kd_fatal("bad father node");
	/* NOTREACHED */
	break;
    case KDF_DUPL:
	kd_fatal("attempt to insert duplicate item");
	/* NOTREACHED */
    default:
	kd_fatal("unknown fault: %d", t);
	/* NOTREACHED */
	break;
    }
    return (char *) 0;
}

static int kd_set_error(int err)
// int err;			/* Error number */
/* Sets an error message in an area for later retrieval */
{
    switch (err) {
    case KD_NOTFOUND:
	Sprintf(kd_err_buf, "k-d error: data not found");
	break;
    default:
	Sprintf(kd_err_buf, "k-d error: unknown error %d", err);
	break;
    }
    return err;
}

char *kd_err_string(void)
/* Returns a textual description of an error */
{
    return kd_err_buf;
}


static KDElem *kd_new_node(kd_generic item, kd_box size, int lomin, int himax, int other, KDElem *loson, KDElem *hison)
// kd_generic item;		/* New node value */
// kd_box size;			/* Size of item   */
// int lomin, himax, other;	/* Bounds info    */
// KDElem *loson, *hison;		/* Sons           */
/* Allocates and initializes a new node element */
{
    KDElem *newElem;

    newElem = ALLOC(KDElem);
    newElem->item = item;
    newElem->size[0] = size[0];
    newElem->size[1] = size[1];
    newElem->size[2] = size[2];
    newElem->size[3] = size[3];
    newElem->size[4] = size[4];
    newElem->size[5] = size[5];
    newElem->lo_min_bound = lomin;
    newElem->hi_max_bound = himax;
    newElem->other_bound = other;
    newElem->sons[0] = loson;
    newElem->sons[1] = hison;
    return newElem;
}


/*
 * K-d tree creation and deletion.
 */


kd_tree kd_create(void)
/*
 * Creates a new k-d tree and returns its handle.  This handle is
 * used by all other k-d tree operations.  It can be freed using
 * kd_destroy().
 */
{
    KDTree *newTree;
	
    newTree = ALLOC(KDTree);
    newTree->tree = (KDElem *) 0;
    newTree->item_count = newTree->dead_count = 0;
    return (kd_tree) newTree;
}



/*
 * kd_build() requires a simple linked list.  This page implements
 * that list.

 * One thing that could be done here is get rid of the kd_list and kd_info structs, since
 * they contain nothing that a KDElem doesn't, really. The list could form thru the
 * sons[0] pointer or some such atrocity.
 */


typedef KDElem kd_list;

static kd_list *kd_tmp_ptr;

#define NIL			(kd_list *) 0
#define CAR(list)		(list)->item
#define CDR(list)		(list ? (list)->sons[0] : NIL)
#define CONS(Item, list) \
  (kd_tmp_ptr = Item, \
   kd_tmp_ptr->sons[0] = (list),   \
   kd_tmp_ptr)
/*
 * The following moves the front of `list1' to the front of `list2' and
 * returns `list1'.  This is a destructive operation: both `list1' and
 * `list2' are changed.
 */
#define CMV(list1, list2) \
  (kd_tmp_ptr = CDR(list1),                 \
   (list1 ? (list1)->sons[0] = (list2) : NIL), \
   (list2) = (list1),                       \
   (list1) = kd_tmp_ptr)
/*
 * Destructively replaces the next item of list1 with list2.
 * Returns modified list1.
 */
#define RCDR(list1, list2) \
  ((list1) ? (((list1)->sons[0] = (list2)), (list1)) : (list2))




/* Forward declarations */
static kd_list *load_items(int (*itemfunc)(kd_generic arg, kd_generic *val, kd_box size), kd_generic arg, kd_box extent, int *length, double *mean);
static KDElem *build_node(kd_list *items, int num, kd_box extent, int disc, int level, int max_level, kd_list **spares, int *treecount, double mean);
static void sel_k(kd_list *items, coord_t k, int disc, kd_list **lo, kd_list **eq, kd_list **hi, double *lomean, double *himean, long *locount, long *hicount);
static void resolve(kd_list **lo, kd_list **eq, kd_list **hi, int disc, double *lomean, double *himean, long *locount, long *hicount);
static int get_min_max(kd_list *list, int disc, coord_t *b_min, coord_t *b_max);
static void del_elem(KDElem *elem, void (*delfunc)(kd_generic item));
static kd_status del_element(KDTree *tree, KDElem *elem, int spot);
static KDElem *find_item(KDElem *elem, int disc, kd_generic item, kd_box size, int search_p, KDElem *items_elem);
static void bounds_update(KDElem *elem, int disc, kd_box size);
static int find_min_max_node(int j, KDElem **kd_minval_node, KDElem **kd_minval_nodesdad, int *dir, int *newj);
  
int kd_set_build_depth(int depth)
{
	int retval = kd_build_depth;
	kd_build_depth = depth;
	return retval;
}

kd_tree kd_build(int (*itemfunc)(kd_generic arg, kd_generic *val, kd_box size), kd_generic arg)
// int (*itemfunc)();		/* Returns new items       */
// kd_generic arg;			/* Data to itemfunc        */
/*
 * This routine builds a new, reasonably balanced k-d tree
 * from a list of items.  This list of items is collected
 * by repeatedly calling `itemfunc' using the following
 * arguments:
 *   int itemfunc(arg, val, size)
 *   kd_generic arg;
 *   kd_generic *val;
 *   kd_box size;
 * Each time the itemfunc is called,  it should return the
 * next item to be placed in the tree in `val',  and the size (bounding box)
 * of the item in `size'.  When there are no more items,  the
 * routine should return zero.  `itemfunc' is guaranteed to be
 * called for all items. `arg' is passed as a convenience (usually
 * for state information).
 */
{
    KDTree *newTree = (KDTree *) kd_create();
    kd_list *items, *spares = (kd_list *)0;
    kd_box extent;
	int item_count = 0,count;
	double mean;
    /* First build up list of items and their overall extent */
    items = load_items(itemfunc, arg, extent, &item_count, &mean);
    if (item_count > 0) mean /= item_count;
    if (!items)
	{
		(void) kd_fault(KDF_ZEROID);
		/* NOTREACHED */
    }

    /* Then recursively fill the tree */
	if( kd_build_depth )
	{
		newTree->tree = build_node(items, item_count, extent, 0, 1,
								   kd_build_depth, &spares,
								   &(newTree->item_count), mean);
		newTree->items_balanced = newTree->item_count;
	}
	else
	{
		extent[KD_LEFT] = extent[KD_BOTTOM] = extent[KD_FLOOR] = MAXINT;
		extent[KD_RIGHT] = extent[KD_TOP] = extent[KD_CEIL] = MININT;
		spares = items;
	}
	
	newTree->extent[0] = extent[0];
	newTree->extent[1] = extent[1];
	newTree->extent[2] = extent[2];
	newTree->extent[3] = extent[3];
	newTree->extent[4] = extent[4];
	newTree->extent[5] = extent[5];

	count = 0;
	
	while( spares )
	{
		kd_list *ptr;
		ptr = CDR(spares);
		/* count++; */
		/*if( count % 50000 == 0 )
			printf(".%d", count),fflush(stdout);*/
		
		kd_insert((kd_tree)newTree,(kd_generic)spares->item,spares->size,(kd_generic)spares);
		spares = ptr;
	}
    return (kd_tree) newTree;
}


static kd_list *load_items(int (*itemfunc)(kd_generic arg, kd_generic *val, kd_box size), kd_generic arg, kd_box extent, int *length, double *mean)
// int (*itemfunc)();		/* Generate next item       */
// kd_generic arg;			/* State passed to itemfunc */
// kd_box extent;			/* Overall extent           */
// int *length;			/* List length (returned)   */
// double *mean;				/* Geometric mean of Left sides */
/*
 * This routine uses `itemfunc' to generate all of the items
 * that are to be loaded into the new k-d tree and places them
 * in a simple linked list.  This list will be used to build
 * a perfectly balanced initial tree.  The routine also
 * stores the size of all items and the overall size of
 * all items.  The routine will return zero if any of the
 * items returned have no size.
 */
{
    kd_list *new_list = (kd_list *) 0;
    KDElem  *new_item;
    int add_flag = 1;

    *mean = 0;
    *length = 0;
    extent[KD_LEFT] = extent[KD_BOTTOM] = extent[KD_FLOOR] = MAXINT;
    extent[KD_RIGHT] = extent[KD_TOP] = extent[KD_CEIL] = MININT;
    for (;;)
	{
		new_item = ALLOC(KDElem);
		if ((*itemfunc)(arg, &new_item->item, new_item->size))
		{
			if (!new_item->item) add_flag = 0;
			if (add_flag)
			{
				/* Add to list */
				if (new_item->size[KD_LEFT] < extent[KD_LEFT])
					extent[KD_LEFT] = new_item->size[KD_LEFT];
				if (new_item->size[KD_BOTTOM] < extent[KD_BOTTOM])
					extent[KD_BOTTOM] = new_item->size[KD_BOTTOM];
				if (new_item->size[KD_FLOOR] < extent[KD_FLOOR])
					extent[KD_FLOOR] = new_item->size[KD_FLOOR];
				if (new_item->size[KD_RIGHT] > extent[KD_RIGHT])
					extent[KD_RIGHT] = new_item->size[KD_RIGHT];
				if (new_item->size[KD_TOP] > extent[KD_TOP])
					extent[KD_TOP] = new_item->size[KD_TOP];
				if (new_item->size[KD_CEIL] > extent[KD_CEIL])
					extent[KD_CEIL] = new_item->size[KD_CEIL];
				new_list = CONS(new_item, new_list);
				(*mean) += new_item->size[KD_LEFT];
				(*length)++;
			}
			else
				free(new_item);
		}
		else
		{
			free(new_item);
			break;
		}
    }
    if (!add_flag)
	{
		kd_list *ptr;
		
		/* Destroy list and return */
		/* WHY? why would you, if you got a non-zero result from itemfunc, but no
		   new item, would you destroy the entire list and return nothing? */
		while (new_list)
		{
			ptr = new_list;
			new_list = CDR(new_list);
			FREE(ptr);
			(*length)--;
		}
    }
	(*mean) /= (*length);
    return new_list;
}


#define NEXTDISC(val)       (((val)+1)%6)

static KDElem *build_node(kd_list *items, int num, kd_box extent, int disc, int level, int max_level, kd_list **spares, int *treecount, double mean)
// kd_list *items;			/* Items to insert          */
// int num;			/* Number of items          */
// kd_box extent;			/* Extent of items          */
// int disc;			/* Discriminator            */
// int level;			/* To keep track of which level we're at.  */
// int max_level;		/* To help in limiting in the depth of the
// 					   balanced tree building  */
// kd_list **spares;   /* ptr to a list head to attach spares to */
// int *treecount;     /* keep a record of each node built */
// double mean;          /* the geometric mean of the data under the node based on disc number */
/*
 * This routine builds a new node by finding an approximate median of
 * the items according to the edge given by `disc' and
 * making that the node.  Items less than the median are
 * recursively placed in the lower son,  items greater
 * than the median are recursively placed in the upper son.
 * Bounds information is also updated.  The node is deleted
 * from the list once placed.
 */
{
    KDElem *loson, *hison;
    KDElem *lo, *eq, *hi;
    coord_t lo_min_bound, lo_max_bound, hi_min_bound, hi_max_bound;
    int num_lo, num_hi;
    int hort;
    coord_t tmp, m;
	double lomean, himean;
	long locnt,hicnt;
	
    if (num == 0) return (KDElem *) 0;


    /* Find (disc)-median of items */
    hort = disc % 3;
/*    m = (extent[hort] + extent[hort+3]) >> 1;*/ /* this criteria will
	  use the geographic mean! */
	m = mean;
	
    sel_k(items, m, disc, &lo, &eq, &hi, &lomean, &himean, &locnt, &hicnt);

    /* If more than one median -- try to distinguish them */
    if (CDR(eq)) resolve(&lo, &eq, &hi, disc,  &lomean, &himean, &locnt, &hicnt);

    /* Find min-max boundaries based on discriminator */
    RCDR(eq, lo);
    num_lo = get_min_max(eq, disc, &lo_min_bound, &lo_max_bound) - 1;

    RCDR(eq, hi);
    num_hi = get_min_max(eq, disc, &hi_min_bound, &hi_max_bound) - 1;
	
	if( level < max_level )
	{
		if( lomean )
			lomean /= locnt;
		if( himean )
			himean /= hicnt;
		
		tmp = extent[hort+3];  extent[hort+3] = m;
		loson = build_node(lo, num_lo, extent, NEXTDISC(disc), level+1, max_level, spares, treecount, lomean);
		extent[hort+3] = tmp;

		tmp = extent[hort];    extent[hort] = m;
		hison = build_node(hi, num_hi, extent, NEXTDISC(disc), level+1, max_level, spares, treecount, himean);
		extent[hort] = tmp;
	}
	else
	{
		/* here, we need to take the lists of unused elements, and pass them back
		   up the calling sequence, or attach them to a global. */
		while( lo )
		{
			CMV(lo,*spares);
		}
		while( hi )
		{
			CMV(hi,*spares);
		}
		hison = loson = (KDElem *)0;
	}
  	
    /* Make new node with appropriate values */
	eq->lo_min_bound = lo_min_bound;
	eq->hi_max_bound = hi_max_bound;
	eq->other_bound = ((disc >= 3) ? hi_min_bound : lo_max_bound);
	eq->sons[0] = loson;
	eq->sons[1] = hison;
	(*treecount)++;
    return eq;
}



#define KD_SIZE(val)	(val)->size

#ifdef OLD_SELECT
static void sel_k(items, k, disc, lo, eq, hi)
kd_list *items;			/* Items to examine                 */
int k;				/* Look for item close to `k'       */
int disc;			/* Discriminator                    */
kd_list **lo;			/* Returned items less than `k'th   */
kd_list **eq;			/* Returned items equal to `k'th    */
kd_list **hi;			/* Returned items larger than `k'th */
/*
 * This routine uses a heuristic to attempt to find a rough median
 * using an inline comparison function.  The routine takes a `target'
 * number `k' and places all items that are `disc'-less than `k' in
 * lo, equal in `eq', and greater in `hi'.
 */
{
    register kd_list *idx;
    register int cmp_val;
    register kd_list *median;
    int lo_val;

    idx = items;
    lo_val = MAXINT;
    /* First find closest to median value */
    while (idx) {
	cmp_val = KD_SIZE(idx)[disc] - k;
	cmp_val = ABS(cmp_val);
	if (cmp_val < lo_val) {
	    median = idx;
	    lo_val = cmp_val;
	}
	idx = CDR(idx);
    }
    /* Now divide based on median */
    *lo = *eq = *hi = NIL;
    idx = items;
    while (idx) {
	cmp_val = KD_SIZE(idx)[disc] - KD_SIZE(median)[disc];
	if (cmp_val < 0) {
	    CMV(idx, *lo);
	} else if (cmp_val > 0) {
	    CMV(idx, *hi);
	} else {
	    CMV(idx, *eq);
	}
    }
}
#endif

static void sel_k(kd_list *items, coord_t k, int disc, kd_list **lo, kd_list **eq, kd_list **hi, double *lomean, double *himean, long *locount, long *hicount)
// kd_list *items;			/* Items to examine                 */
// int k;				/* Look for item close to `k'       */
// int disc;			/* Discriminator                    */
// kd_list **lo;			/* Returned items less than `k'th   */
// kd_list **eq;			/* Returned items equal to `k'th    */
// kd_list **hi;			/* Returned items larger than `k'th */
// double *lomean,*himean;   /* the total values of all the Kj's */
// long *locount,*hicount; /* the counts to get an average     */
/*
 * This routine uses a heuristic to attempt to find a rough median
 * using an inline comparison function.  The routine takes a `target'
 * number `k' and places all items that are `disc'-less than `k' in
 * lo, equal in `eq', and greater in `hi'.
 */
{
    register kd_list *idx, *median;
    coord_t cmp_val;
    coord_t lo_val;

    idx = items;
    *lo = *eq = *hi = NIL;
	*lomean = *himean = 0.0;
	*locount = *hicount = 0;
    lo_val = MAXINT;
    median = NIL;
    while (idx)
	{
		/* Check to see if new median */
		cmp_val = KD_SIZE(idx)[disc] - k;
		if (ABS(cmp_val) < lo_val)
		{
			lo_val = ABS(cmp_val);
			median = idx;
			while (*eq)
			{
				cmp_val = KD_SIZE(*eq)[disc] - KD_SIZE(median)[disc];
				if (cmp_val < 0)
				{
					CMV(*eq, *lo);
					(*lomean) += KD_SIZE(*lo)[NEXTDISC(disc)];
					(*locount)++;
				} else if (cmp_val > 0)
				{
					CMV(*eq, *hi);
					(*himean) += KD_SIZE(*hi)[NEXTDISC(disc)];
					(*hicount)++;
				} else
				{
					(void) kd_fault(KDF_MD);
				}}
		}
		/* Place element in list */
		if (median)
		{
			cmp_val = KD_SIZE(idx)[disc] - KD_SIZE(median)[disc];
		}
		if (cmp_val < 0)
		{
			CMV(idx, *lo);
			(*lomean) += KD_SIZE(*lo)[NEXTDISC(disc)];
			(*locount)++;
			
		} else if (cmp_val > 0)
		{
			CMV(idx, *hi);
			(*himean) += KD_SIZE(*hi)[NEXTDISC(disc)];
			(*hicount)++;
		} else
		{
			CMV(idx, *eq);
		}
	}
}


#define KD_BB(val)	(val)->size

static void resolve(register kd_list **lo, register kd_list **eq, register kd_list **hi, int disc, double *lomean, double *himean, long *locount, long *hicount)
// register kd_list **lo, **eq, **hi; /* Lists for examination */
// int disc;
// double *lomean,*himean;   /* the total values of all the Kj's */
// long *locount,*hicount; /* the counts to get an average     */
/*
 * This routine is called if more than one possible median
 * was found.  The first is chosen as the actual median.
 * The rest are reclassified using cyclical comparison.
 */
/* correction: the rest are put to help balance lo and hi sides. */
{
    kd_list *others;
    int cur_disc;
    coord_t val=0;

    others = CDR(*eq);
    RCDR(*eq, NIL);
    while (others)
	{
		cur_disc = NEXTDISC(disc); /* since all the eq's disc val are the same, maybe
									  if we use the next disc, we can get some random
									  numbers to seperate the goats from the sheep. */
		while (cur_disc != disc)
		{
			val = KD_BB(others)[cur_disc] - KD_BB(*eq)[cur_disc];
			if (val != 0) break;
			cur_disc = NEXTDISC(cur_disc);
		}
		if (val < 0)
		{
			CMV(others, *lo);
			(*lomean) += KD_SIZE(*lo)[NEXTDISC(disc)];
			(*locount)++;
		} else
		{
			CMV(others, *hi);
			(*himean) += KD_SIZE(*hi)[NEXTDISC(disc)];
			(*hicount)++;
		}
		/* this stuff could be a sensible criterion, but there's no way on earth
		   I can replicate the direction to go when I'm looking for a certain node!
		if( hicount > locount )
		{
			CMV(others, *lo);
			(*lomean) += KD_SIZE(*lo)[NEXTDISC(disc)];
			(*locount)++;
		}
		else
		{
			CMV(others, *hi);
			(*himean) += KD_SIZE(*hi)[NEXTDISC(disc)];
			(*hicount)++;
		} */
	}
}




static int get_min_max(kd_list *list, int disc, coord_t  *b_min, coord_t *b_max)
/*
 * This routine examines all of the items in `list' and
 * finds the lowest and highest edges based on the discriminator
 * `disc'.  If the discriminator is 0, 1, or 2, the corresponding
 * min and max edges are examined.
 */
{
    KDElem *item;
    int count;
    int dim;

    *b_min = MAXINT;
    *b_max = MININT;

    dim = disc % 3;
    count = 0;
    while (list) {
        item = list;
        if (item->size[dim] < *b_min) *b_min = item->size[dim];
        if (item->size[dim+3] > *b_max) *b_max = item->size[dim+3];
        list = CDR(list);
        count++;
    }
    return count;
}



static void del_elem(KDElem *elem, void (*delfunc)(kd_generic item))
// KDElem *elem;			/* Element to release */
// void (*delfunc)();		/* Free function      */
/*
 * Recursively releases resources associated with `elem'.
 * User data items are freed using `delfunc'.
 */
{
    int i;

    /* If the tree does not exist,  return normally */
    if (!elem) return;

    /* If there are children,  recursively destroy them first */
    for (i = 0;  i < 2;  i++) {
	del_elem(elem->sons[i], delfunc);
    }

    /* Now get rid of the rest of it */
    if (delfunc /* 18.02.98 Liburkin add this terrible:*/ && elem->item) (*delfunc)(elem->item);
    FREE(elem);
}

void kd_destroy(kd_tree this_one, void (*delfunc)(kd_generic item))
// kd_tree this_one;		/* k-d tree to destroy */
// void (*delfunc)();		/* Free function called on user data */
/*
 * This routine frees all resources associated with the
 * specified kd-tree.
 */
{
	
    del_elem(((KDTree *) this_one)->tree, delfunc);
	FREE(this_one);
}




/*
 * Insertion
 */

/* Forward declaration */


void kd_insert(kd_tree theTree, kd_generic data, kd_box size, kd_generic datas_elem)
// kd_tree theTree;		/* k-d tree for insertion */
// kd_generic data;		/* User supplied data     */
// kd_box size;			/* Size of item           */
// kd_generic datas_elem;		/* k-d tree for insertion */
/*
 * Inserts a new data item into the specified k-d tree.  The `data'
 * item cannot be zero.  This value is used internally by the package.
 * Fatal errors:
 *   KDF_ZEROID: attempt to insert an item with a null generic pointer.
 *   KDF_DUPL:   an exact duplicate is already in the tree.
 */
{
    KDTree *realTree = (KDTree *) theTree;
	KDElem *elem = (KDElem *)datas_elem;
	
    if (!data) (void) kd_fault(KDF_ZEROID);
    if (realTree->tree)
	{
		if (find_item(realTree->tree, 0, data, size, 0, elem))
		{
			realTree->item_count += 1;
			if( size[KD_LEFT] < realTree->extent[KD_LEFT] ) /* the area doesn't contract with deletions,     */
				realTree->extent[KD_LEFT] = size[KD_LEFT];  /* but that's OK, it's theoretically no big deal */
			if( size[KD_RIGHT] > realTree->extent[KD_RIGHT] )
				realTree->extent[KD_RIGHT] = size[KD_RIGHT];
			if( size[KD_TOP] > realTree->extent[KD_TOP] )
				realTree->extent[KD_TOP] = size[KD_TOP];
			if( size[KD_BOTTOM] < realTree->extent[KD_BOTTOM] )
				realTree->extent[KD_BOTTOM] = size[KD_BOTTOM];
		}
		else
		{
			(void) kd_fault(KDF_DUPL);
		}
    }
	else
	{
		if( elem )
		{
			realTree->tree = elem;
			realTree->tree->item = data;
			realTree->tree->size[0] = size[0];
			realTree->tree->size[1] = size[1];
			realTree->tree->size[2] = size[2];
			realTree->tree->size[3] = size[3];
			realTree->tree->lo_min_bound = size[0];
			realTree->tree->hi_max_bound = size[2];
			realTree->tree->other_bound = size[0];
			realTree->tree->sons[0] = 0;
			realTree->tree->sons[1] = 0;
		}
		else
			realTree->tree = kd_new_node(data, size, size[0], size[2], size[0],
										 (KDElem *) 0, (KDElem *) 0);
		realTree->extent[0] = size[0];
		realTree->extent[1] = size[1];
		realTree->extent[2] = size[2];
		realTree->extent[3] = size[3];
		realTree->item_count += 1;
    }
}


/*
 * The find_item() routine optionally produces a path down to the
 * item in the following global dynamic array if search_p is true.
 * find_item() uses NEW_PATH to record the decent down the tree
 * until it finds the object.  It uses LAST_PATH to mark the
 * last item.
 */

static int path_length = 0;
static int path_alloc = 0;
static int path_reset = 1;
static KDElem **path_to_item = (KDElem **) 0;

#define PATH_INIT	50
#define PATH_INCR	10

void NEW_PATH(KDElem *elem)
{
	if (path_reset)
	{
		path_length = 0;
		path_reset = 0;
	}
	if (path_length >= path_alloc)
	{
		if (path_alloc == 0)
		{
			path_alloc = PATH_INIT;
			path_to_item = MULTALLOC(KDElem *,path_alloc);
		}
		else
		{
			path_alloc += PATH_INCR;
			path_to_item = REALLOC(KDElem *, path_to_item, path_alloc);
		}
	}
	path_to_item[path_length++] = (elem);
}

/*
  #define NEW_PATH(elem)	\
  if (path_reset) { path_length = 0;  path_reset = 0; } \
  if (path_length >= path_alloc) { \
  if (path_alloc == 0) {path_alloc = PATH_INIT; path_to_item = MULTALLOC(KDElem *,path_alloc); } \
  else { path_alloc += PATH_INCR; \
  path_to_item = REALLOC(KDElem *, path_to_item, path_alloc);} \
  } \
  path_to_item[path_length++] = (elem)
*/

#define LAST_PATH 	path_reset = 1

void kd_print_path(void) /* this routine is for debug */
{
	int i;
	for(i=0;i<path_length;i++)
	{
		KDElem *elem;
		elem = path_to_item[i];
		printf("%d: \tElem: %ld [%lx] lo=%lld hi=%lld, other=%lld, size= \t(%lld\t%lld\t%lld\t%lld\t%lld\t%lld)  Loson:%lx[%ld]  HiSon:%lx[%ld]\n",
			   i,(long)elem->item, (unsigned long)elem,
			   (long long)elem->lo_min_bound, (long long)elem->hi_max_bound, (long long)elem->other_bound,
			   (long long)elem->size[0],(long long)elem->size[1],(long long)elem->size[2],(long long)elem->size[3],(long long)elem->size[4],(long long)elem->size[5],
			   (long)elem->sons[0],elem->sons[0]?(long)elem->sons[0]->item:0,
			   (long)elem->sons[1],elem->sons[1]?(long)elem->sons[1]->item:0);
	}
}



/* bounds_update declared in forward declarations block above */

static KDElem *find_item(KDElem *elem, int disc, kd_generic item, kd_box size, int search_p, KDElem *items_elem)
// KDElem *elem;			/* Search location */
// int disc;			/* Discriminator   */
// kd_generic item;		/* Item to insert  */
// kd_box size;			/* geographic Size of item    */
// int search_p;			/* Search or insert */
// KDElem *items_elem;		/* pre-malloc'd container for item */
/*
 * This routine either searches for or inserts `item'
 * into the node `elem'.  The size of `item' is passed
 * in as `size'.  The function for returning the size
 * of an item is `s_func'.  If `search_p' is non-zero,
 * the routine expects to find the item rather than
 * insert it.  The routine returns either the newly
 * created element or the element found (zero if
 * it couldn't be found).
 */
{
    KDElem *result;
    coord_t val; int new_disc, vert;

    /* Compare current element against the one we are looking for */
    if (item == elem->item)
	{
		if (search_p)
		{
			LAST_PATH;
			if (elem->item) return elem;
			else return (KDElem *) 0;
		}
		else
			return (KDElem *) 0;
    }
	else
	{
		/* Determine successor */
		val = size[disc] - elem->size[disc];
		if (val == 0)
		{
			/* Cyclical comparison required */
			new_disc = NEXTDISC(disc);
			while (new_disc != disc)
			{
				val = size[new_disc] - elem->size[new_disc];
				if (val != 0) break;
				new_disc = NEXTDISC(new_disc);
			}
			if (val == 0) val = 1; /* Force upward if equal */
		}
		val = (val >= 0);
		if (elem->sons[val])
		{
			if (search_p) NEW_PATH(elem);
			result = find_item(elem->sons[val], NEXTDISC(disc), item,
							   size, search_p, items_elem);
			/* Bounds update if insert */
			if (!search_p) bounds_update(elem, disc, size);
			/* ^ this is where we jump up the tree after insert and fix the
			   bounds above us in the tree */
			return result;
		}
		else if (search_p)
		{
			LAST_PATH;
			return (KDElem *) 0;
		}
		else
		{
		        /* Insert here */
		        vert = NEXTDISC(disc) % 3;
		        if( items_elem )
		        {
		                elem->sons[val] = items_elem;
		                items_elem->size[0] = size[0];
		                items_elem->size[1] = size[1];
		                items_elem->size[2] = size[2];
		                items_elem->size[3] = size[3];
		                items_elem->size[4] = size[4];
		                items_elem->size[5] = size[5];
		                items_elem->lo_min_bound = size[vert];
		                items_elem->hi_max_bound = size[vert+3];
		                items_elem->other_bound = ((NEXTDISC(disc)>=3) ? size[vert] : size[vert+3]);
		                items_elem->sons[0] = 0;
		                items_elem->sons[1] = 0;

		        }
		        else
		                elem->sons[val] =
		                        kd_new_node(item, size, size[vert], size[vert+3],
		                                                ((NEXTDISC(disc)>=3) ? size[vert] : size[vert+3]),
		                                                (KDElem *) 0, (KDElem *) 0);
		        /* Bounds update */
		        bounds_update(elem, disc, size);
		        return elem->sons[val];
		}    }
}



static void bounds_update(KDElem *elem, int disc, kd_box size)
// KDElem *elem;			/* Element to update */
// int disc;			/* Discriminator     */
// kd_box size;			/* Size of new item  */
/*
 * This routine updates the bounds information of `elem'
 * using `disc' and `size'.
 */
{
    int vert;

    vert = disc % 3;
    elem->lo_min_bound = MIN(elem->lo_min_bound, size[vert]);
    elem->hi_max_bound = MAX(elem->hi_max_bound, size[vert+3]);
    if (disc >= 3) {
        /* hi_min_bound */
        elem->other_bound = MIN(elem->other_bound, size[vert]);
    } else {
        /* lo_max_bound */
        elem->other_bound = MAX(elem->other_bound, size[vert+3]);
    }
}


int kd_nearest(kd_tree tree, coord_t x, coord_t y, coord_t z, int m, kd_priority **alist);

void kd_print_nearest(kd_tree tree, coord_t x, coord_t y, coord_t z, int m)
{
	kd_priority *list;
	int xz,i;
	
	xz = kd_nearest(tree, x, y, z, m, (kd_priority **)&list);
	fprintf(stderr,"Nearest Search: visited %d nodes to find the %d closest objects.\n",
			xz, m);
	for(i=0;i<m;i++)
	{
		// This is the original fprintf; for it to work, we would have to store the kd_elem in the kd_priority struct.
		//
	//	fprintf(stderr,"Nearest Neighbor: dist to center: %g units. elem=%lx.  ([%lld,%lld,%lld]->[%lld,%lld.%lld])\n",
	//			list[i].dist, (unsigned long)list[i].elem,
	//			list[i].elem->size[KD_LEFT],
	//			list[i].elem->size[KD_BOTTOM],
	//			list[i].elem->size[KD_FLOOR],
	//			list[i].elem->size[KD_RIGHT],
	//			list[i].elem->size[KD_TOP],
	//			list[i].elem->size[KD_CEIL]);
		fprintf(stderr,"Nearest Neighbor: dist to center: %g units. elem=%lx.\n",
				list[i].dist, (unsigned long)list[i].elem);
	}
	free(list);
}

#define PREVDISC(x) (x==0?5:x-1)
/*
 * Returns the SQUARED edge-to-edge distance from query point Xq to the
 * bounding box of elem.  Using squared distance throughout the nearest-
 * neighbor search avoids expensive sqrt/hypot calls and keeps the metric
 * consistent with coord_dist (used by bounds_overlap_ball for pruning).
 * The final results are converted back to actual distance in kd_neighbor.
 */
static double KDdist(kd_box Xq, KDElem *elem)
{
        double dx = 0.0, dy = 0.0, dz = 0.0;

        if( Xq[KD_LEFT] > elem->size[KD_RIGHT] )
                dx = (double)(Xq[KD_LEFT] - elem->size[KD_RIGHT]);
        else if( Xq[KD_RIGHT] < elem->size[KD_LEFT] )
                dx = (double)(elem->size[KD_LEFT] - Xq[KD_RIGHT]);

        if( Xq[KD_BOTTOM] > elem->size[KD_TOP] )
                dy = (double)(Xq[KD_BOTTOM] - elem->size[KD_TOP]);
        else if( Xq[KD_TOP] < elem->size[KD_BOTTOM] )
                dy = (double)(elem->size[KD_BOTTOM] - Xq[KD_TOP]);

        if( Xq[KD_FLOOR] > elem->size[KD_CEIL] )
                dz = (double)(Xq[KD_FLOOR] - elem->size[KD_CEIL]);
        else if( Xq[KD_CEIL] < elem->size[KD_FLOOR] )
                dz = (double)(elem->size[KD_FLOOR] - Xq[KD_CEIL]);

        return dx*dx + dy*dy + dz*dz;
}

static double coord_dist(coord_t x, coord_t y)
{
        double d;
        d=x-y;
        d *= d;
        return d;
}


static int bounds_overlap_ball(kd_box Xq, kd_box Bp, kd_box Bn, int m, kd_priority *list)
{
	int d1;
	double sum;
	sum = 0.0;
	for(d1 = 0; d1 < 3; d1++)
	{
		if( Xq[d1] < Bn[d1] )
		{
			sum += coord_dist(Xq[d1],Bn[d1]);
			if( sum > list[m-1].dist )
				return 0;
		}
		else if( Xq[d1] > Bp[d1] )
		{
			sum += coord_dist(Xq[d1],Bp[d1]);
			if( sum > list[m-1].dist )
				return 0;
		}
	}
	return 1;
}

#define KD_PUSH(gen, elem, dk) \
    if ((gen)->top_index >= (gen)->stack_size) {                     \
        (gen)->stack_size += KD_GROWSIZE((gen)->stack_size);         \
        (gen)->stk = REALLOC(KDSave, (gen)->stk, (gen)->stack_size); \
    }                                                                \
    (gen)->stk[(gen)->top_index].disc = (dk);                        \
    (gen)->stk[(gen)->top_index].state = KD_THIS_ONE;                \
    (gen)->stk[(gen)->top_index].item = (elem);                      \
    (gen)->top_index += 1;

#define KD_PUSHB(gen, elem, dk, Bxn, Bxp) \
    if ((gen)->top_index >= (gen)->stack_size) {                     \
        (gen)->stack_size += KD_GROWSIZE((gen)->stack_size);         \
        (gen)->stk = REALLOC(KDSave, (gen)->stk, (gen)->stack_size); \
    }                                                                \
    (gen)->stk[(gen)->top_index].disc = (dk);                        \
    (gen)->stk[(gen)->top_index].state = KD_THIS_ONE;                \
    (gen)->stk[(gen)->top_index].item = (elem);                      \
    (gen)->stk[(gen)->top_index].Bn[0] = Bxn[0];                             \
    (gen)->stk[(gen)->top_index].Bn[1] = Bxn[1];                             \
    (gen)->stk[(gen)->top_index].Bn[2] = Bxn[2];                             \
    (gen)->stk[(gen)->top_index].Bn[3] = Bxn[3];                             \
    (gen)->stk[(gen)->top_index].Bn[4] = Bxn[4];                             \
    (gen)->stk[(gen)->top_index].Bn[5] = Bxn[5];                             \
    (gen)->stk[(gen)->top_index].Bp[0] = Bxp[0];                             \
    (gen)->stk[(gen)->top_index].Bp[1] = Bxp[1];                             \
    (gen)->stk[(gen)->top_index].Bp[2] = Bxp[2];                             \
    (gen)->stk[(gen)->top_index].Bp[3] = Bxp[3];                             \
    (gen)->stk[(gen)->top_index].Bp[4] = Bxp[4];                             \
    (gen)->stk[(gen)->top_index].Bp[5] = Bxp[5];                             \
    (gen)->top_index += 1

static void add_priority(int m, kd_priority *P, kd_box Xq, KDElem *elem);

kd_gen kd_start(kd_tree theTree, kd_box area)
// kd_tree theTree;             /* Tree to generate from */
// kd_box area;                 /* Area to search        */
/*
 * This routine allocates a generator which can be used
 * to generate all items intersecting `area'.
 * The items are actually returned by kd_next.  Once the
 * sequence is finished,  kd_end should be called.
 */
{
    KDElem *realTree = ((KDTree *) theTree)->tree;
    KDState *newState;
    int i;

    newState = ALLOC(KDState);

        kd_data_tries = 0;
    for (i = 0;  i < KD_BOX_MAX;  i++) newState->extent[i] = area[i];

    newState->stack_size = KD_INIT_STACK;
    newState->top_index = 0;
    newState->stk = MULTALLOC(KDSave, KD_INIT_STACK);

    /* Initialize search state */
    if (realTree)
        {
                KD_PUSH(newState, realTree, 0);
    }
        else
        {
                newState->top_index = -1;
    }
    return (kd_gen) newState;
}

kd_status kd_next(kd_gen theGen, kd_generic *data, kd_box size)
// kd_gen theGen;                       /* Current generator */
// kd_generic *data;            /* Returned data     */
// kd_box size;                 /* Optional size     */
/*
 * Returns the next item in the generator sequence.  If
 * `size' is non-zero,  it will be filled with the item's
 * size.  If there are no more items,  it returns KD_NOMORE.
 */
{
    register KDState *realGen = (KDState *) theGen;
    register KDSave *top_elem;
    register KDElem *top_item;
    short hort,m;

    while (realGen->top_index > 0) {
        top_elem = &(realGen->stk[realGen->top_index-1]);
        top_item = top_elem->item;
        hort = top_elem->disc % 3;/* the split line is zero: vertical, one: horizontal, two: floor */
        m = top_elem->disc;

        switch (top_elem->state) {
        case KD_THIS_ONE:
    /* Check this one */
    kd_data_tries++;

            if (top_item->item && BOXINTERSECT(realGen->extent, top_item->size)) {
                *data = top_item->item;
                if (size) {
                    size[0] = top_item->size[0];  size[1] = top_item->size[1];
                    size[2] = top_item->size[2];  size[3] = top_item->size[3];
                    size[4] = top_item->size[4];  size[5] = top_item->size[5];
                }
                top_elem->state += 1;
                return KD_OK;
            } else {
                top_elem->state += 1;
            }
            break;
        case KD_LOSON:
            /* See if we push on the loson */
            if (top_item->sons[KD_LOSON] &&
                ((m >= 3) ?                   /* RIGHT, TOP or CEIL */
                 ((realGen->extent[hort] <= top_item->size[m]) && /* min region less than key */
                  (realGen->extent[hort+3] >= top_item->lo_min_bound)) /* max region greater than lominbound */
                 :                                              /* LEFT, BOTTOM or FLOOR */
                 ((realGen->extent[hort] <= top_item->other_bound) && /* min region less than obound */
                  (realGen->extent[hort+3] >= top_item->lo_min_bound)))) /* max region greater than lominbound */
    {
    top_elem->state += 1;
    KD_PUSH(realGen, top_item->sons[KD_LOSON],
    NEXTDISC(m));
            } else
    {
    top_elem->state += 1;
            }
            break;
        case KD_HISON:
            /* See if we push on the hison */
            if (top_item->sons[KD_HISON] &&
                ((m >= 3) ?                   /* RIGHT, TOP or CEIL */
                 ((realGen->extent[hort] <= top_item->hi_max_bound) && /* min region less than himax */
                  (realGen->extent[hort+3] >= top_item->other_bound)) /* max region greater than obound */
                 :                                              /* LEFT, BOTTOM or FLOOR */
                 ((realGen->extent[hort] <= top_item->hi_max_bound) && /* min region less than himax */
                  (realGen->extent[hort+3] >= top_item->size[m])))) /* max region greater than key */
    {
    top_elem->state += 1;
    KD_PUSH(realGen, top_item->sons[KD_HISON],
    NEXTDISC(m));
            } else
    {
    top_elem->state += 1;
            }
            break;        default:
            /* We have exhausted this node -- pop off the next one */
            realGen->top_index -= 1;
            break;
        }
    }
    return KD_NOMORE;
}

int kd_finish(kd_gen theGen)
// kd_gen theGen;                       /* Generator to destroy */
/*
 * Frees resources consumed by the specified generator.
 * This routine is NOT automatically called at the end
 * of a sequence.  Thus,  the user should ALWAYS calls
 * kd_finish to end a generation sequence.
 */
{
    KDState *realGen = (KDState *) theGen;

    FREE(realGen->stk);
    FREE(realGen);
        return kd_data_tries;
}

#define KDR(t)  ((KDTree *) (t))

int kd_count(kd_tree tree)
// kd_tree tree;
/* Returns the number of items in the specified tree */
{
    return KDR(tree)->item_count - KDR(tree)->dead_count;
}


static int  kd_neighbor(KDElem *node, kd_box Xq, int m, kd_priority *list, kd_box Bp, kd_box Bn)
{
    KDState *realGen;
    coord_t p;
    int d;
    register KDSave *top_elem;
    register KDElem *top_item;
    short hort,vert;
	
    realGen = ALLOC(KDState);
	
    kd_data_tries = 0;
	
    realGen->stack_size = KD_INIT_STACK;
    realGen->top_index = 0;
    realGen->stk = MULTALLOC(KDSave, KD_INIT_STACK);

    /* Initialize search state */
    if (node)
    {
	KD_PUSHB(realGen, node, 0,Bn,Bp);
    }
    else
    {
	realGen->top_index = -1;
    }

	while (realGen->top_index > 0)
	{
		top_elem = &(realGen->stk[realGen->top_index-1]);
		top_item = top_elem->item;
		d = top_elem->disc;
		p = top_item->size[d];

		hort = d % 3;
		vert = (d >= 3);

		switch (top_elem->state)
		{
		case KD_THIS_ONE:
		        /* Check this one */
		        kd_data_tries++;
		        if( top_item->item ) /* really shouldn't add dead nodes to the list! */
		                add_priority(m,list,Xq,top_item);
		        top_elem->state += 1;
		        break;
		case KD_LOSON:
		        /* calc bounds */
		        /* See if we push on the loson */
		        if( Xq[d] <= p )
		        {
		                if( top_item->sons[KD_LOSON])
		                {
		                        coord_t old_Bn = top_elem->Bn[hort];
		                        coord_t old_Bp = top_elem->Bp[hort];
		                        if (vert)
		                        {
		                                top_elem->Bp[hort] = top_item->size[d];
		                                top_elem->Bn[hort] = top_item->lo_min_bound;
		                        }
		                        else
		                        {
		                                top_elem->Bp[hort] = top_item->other_bound;
		                                top_elem->Bn[hort] = top_item->lo_min_bound;
		                        }
		                        if( bounds_overlap_ball(Xq,top_elem->Bp,top_elem->Bn,m,list))
		                        {
		                                top_elem->state += 1;
		                                KD_PUSHB(realGen, top_item->sons[KD_LOSON],
		                                                 NEXTDISC(d),top_elem->Bn,top_elem->Bp);
		                        }
		                        else
		                                top_elem->state++;
		                        top_elem->Bn[hort] = old_Bn;
		                        top_elem->Bp[hort] = old_Bp;
		                }
		                else
		                        top_elem->state += 1;
		        }
		        else
		        {
		                if( top_item->sons[KD_HISON] )
		                {
		                        coord_t old_Bn = top_elem->Bn[hort];
		                        coord_t old_Bp = top_elem->Bp[hort];
		                        if (vert)
		                        {
		                                top_elem->Bp[hort] = top_item->hi_max_bound;
		                                top_elem->Bn[hort] = top_item->other_bound;
		                        }
		                        else
		                        {
		                                top_elem->Bp[hort] = top_item->hi_max_bound;
		                                top_elem->Bn[hort] = top_item->size[d];
		                        }
		                        if( bounds_overlap_ball(Xq,top_elem->Bp,top_elem->Bn,m,list))
		                        {
		                                top_elem->state += 1;
		                                KD_PUSHB(realGen, top_item->sons[KD_HISON],
		                                                 NEXTDISC(d),top_elem->Bn,top_elem->Bp);
		                        }
		                        else
		                                top_elem->state++;
		                        top_elem->Bn[hort] = old_Bn;
		                        top_elem->Bp[hort] = old_Bp;
		                }
		                else
		                        top_elem->state += 1;
		        }
		        break;
		case KD_HISON:
		        /* See if we push on the hison */
		        if( Xq[d] <= p )
		        {
		                if( top_item->sons[KD_HISON] )
		                {
		                        coord_t old_Bn = top_elem->Bn[hort];
		                        coord_t old_Bp = top_elem->Bp[hort];
		                        if (vert)
		                        {
		                                top_elem->Bp[hort] = top_item->hi_max_bound;
		                                top_elem->Bn[hort] = top_item->other_bound;
		                        }
		                        else
		                        {
		                                top_elem->Bp[hort] = top_item->hi_max_bound;
		                                top_elem->Bn[hort] = top_item->size[d];
		                        }
		                        if( bounds_overlap_ball(Xq,top_elem->Bp,top_elem->Bn,m,list))
		                        {
		                                top_elem->state += 1;
		                                KD_PUSHB(realGen, top_item->sons[KD_HISON],
		                                                 NEXTDISC(d),top_elem->Bn,top_elem->Bp);
		                        }
		                        else
		                                top_elem->state++;
		                        top_elem->Bn[hort] = old_Bn;
		                        top_elem->Bp[hort] = old_Bp;
		                }
		                else
		                        top_elem->state += 1;
		        }
		        else
		        {
		                if( top_item->sons[KD_LOSON] )
		                {
		                        coord_t old_Bn = top_elem->Bn[hort];
		                        coord_t old_Bp = top_elem->Bp[hort];
		                        if (vert)
		                        {
		                                top_elem->Bp[hort] = top_item->size[d];
		                                top_elem->Bn[hort] = top_item->lo_min_bound;
		                        }
		                        else
		                        {
		                                top_elem->Bp[hort] = top_item->other_bound;
		                                top_elem->Bn[hort] = top_item->lo_min_bound;
		                        }
		                        if( bounds_overlap_ball(Xq,top_elem->Bp,top_elem->Bn,m,list))
		                        {
		                                top_elem->state += 1;
		                                KD_PUSHB(realGen, top_item->sons[KD_LOSON],
		                                                NEXTDISC(d),top_elem->Bn,top_elem->Bp);
		                        }
		                        else
		                                top_elem->state++;
		                        top_elem->Bn[hort] = old_Bn;
		                        top_elem->Bp[hort] = old_Bp;
		                }
		                else
		                        top_elem->state += 1;
		        }
		        break;		default:
			/* We have exhausted this node -- pop off the next one */
			realGen->top_index -= 1;
			break;
		}
	}
	FREE(realGen->stk);
	FREE(realGen);
	/* Convert squared distances back to actual distances, and change
	   KDElem * to kd_generic for the user's results. */
	for(p=0;p<m;p++)
	{
		list[p].dist = sqrt(list[p].dist);
		list[p].elem = (KDElem *)(list[p].elem)->item;
	}
	return kd_data_tries;
}

static void add_priority(int m, kd_priority *P, kd_box Xq, KDElem *elem)
{
        int x;
        double d;
        d = KDdist(Xq,elem);
        for(x=m-1;x>=0;x--)
        {
                if( d < P[x].dist )
                {
                        if(x != m-1 )
                        {
                                P[x+1] = P[x];
                        }
                        P[x].dist = d;
                        P[x].elem = elem;
                }
                else
                        break;
        }
}

int kd_nearest(kd_tree tree, coord_t x, coord_t y, coord_t z, int m, kd_priority **alist)
{
	kd_box Bp,Bn,Xq;
	int i;
	KDTree *realTree = (KDTree *) tree;
	kd_priority **list = (kd_priority **)alist;
	Xq[KD_LEFT] = x;
	Xq[KD_BOTTOM] = y;
	Xq[KD_FLOOR] = z;
	Xq[KD_RIGHT] = x;
	Xq[KD_TOP] = y;
	Xq[KD_CEIL] = z;
	*list = (kd_priority *)calloc(sizeof(struct kd_priority),m);
	for(i=0;i<m;i++)
	{
		(*list)[i].dist = 1.79769313486231470e+308;
	}
	
	for(i=0;i<KD_BOX_MAX;i++)
	{
		Bp[i] = MAXINT;
		Bn[i] = MININT;
	}
	return kd_neighbor(realTree->tree,Xq,m,*list,Bp,Bn);
}

/* ************** kd_tree_badness -- some metrics of tree health   ************************************** */
/* Coded by Steve Murphy,  Sept 1990                                                  */

static double kd_tree_badness_factor1 = 0.0;
static double kd_tree_badness_factor2 = 0.0;
static double kd_tree_badness_factor3 = 0.0;  /* count of one-son nodes */
static int kd_tree_max_levels = 0;

static void kd_tree_badness_level(KDElem *elem, int level)
{
    if( !elem )
        return;
    if( (elem->sons[1] || elem->sons[0]) && !(elem->sons[1] && elem->sons[0]))
        {
                kd_tree_badness_factor3++;
        }
        if( level > kd_tree_max_levels )
                kd_tree_max_levels = level;
        
        if( elem->sons[0] )
                kd_tree_badness_level(elem->sons[0], level+1);
        if( elem->sons[1] )
                kd_tree_badness_level(elem->sons[1], level+1);
}

/* right now, fact1 is not used. fact2 is the ratio of current tree max depth to minimum possible depth with number
   of nodes tree contains. fact3 is the number of nodes with one son. levs is the max depth.*/
static void kd_tree_badness(KDTree *tree, double *fact1, double *fact2, double *fact3, int *levs)
{
        double targdepth,log(double),floor(double);
        kd_tree_badness_factor1 = 0.0;
        kd_tree_badness_factor2 = 0.0;
        kd_tree_badness_factor3 = 0.0;
        kd_tree_max_levels = 0;
        targdepth = tree->item_count;
        targdepth = log(targdepth)/log(2.0);
        targdepth = floor(targdepth);
        targdepth++;
        kd_tree_badness_level(tree->tree,1);
        *fact1 = kd_tree_badness_factor1;
        targdepth = (double)kd_tree_max_levels/targdepth;
        *fact2 = targdepth;
        *fact3 = kd_tree_badness_factor3;
        *levs = kd_tree_max_levels;
}

void kd_badness(kd_tree tree)
{
        int lev;
        double a1,a2,a3;
        kd_tree_badness((KDTree *)tree,&a1,&a2,&a3,&lev);
        fprintf(stdout,"balance ratio=%g (the closer to 1.0, the better), #of nodes with only one branch=%g (%g), max depth=%d, dead=%d (%g)\n",
                        a2,a3,(double)(a3/(double)((KDTree *)(tree))->item_count*100.00),lev,
                        ((KDTree *)(tree))->dead_count,
                        (double)(((double)((KDTree *)(tree))->dead_count)/(double)((KDTree *)(tree))->item_count*100.00));
}

void unload_items(kd_tree, kd_list **, kd_box, long *, double *);
void collect_nodes(kd_tree, kd_list *, kd_list **, kd_box, long *, double *);

/* ************** kd_rebuild -- functions to rebuild a tree       ********************************** */
/* Coded by Steve Murphy, Sept 1990                                                  */
/* NOT TESTED YET!  */

kd_tree kd_rebuild(kd_tree Tree)
{
        KDTree *newTree= (KDTree *)Tree;

    kd_list *items = (kd_list *)0, *spares = (kd_list *)0;
    kd_box extent;
        long item_count = 0,count=0;
        double mean=0.0;
        /* rip the tree apart, discarding dead nodes, and rebuild it */

    /* First build up list of items and their overall extent */
    unload_items((kd_tree)newTree, &items, newTree->extent, &item_count, &mean);


        /* rebuild the tree */
    if (!items)
        {
                return (kd_tree) newTree;
    }

    /* Then recursively fill the tree */
        if( kd_build_depth )
        {
                newTree->tree = build_node(items, item_count, extent, 0, 1, kd_build_depth, &spares, &(newTree->item_count), mean);
                newTree->items_balanced = newTree->item_count;
        }
        else
        {
                spares = items;
        }

        count = 0;

        while( spares )
        {
                kd_list *ptr;
                ptr = CDR(spares);
                /* count++; */
                /*if( count % 50000 == 0 )
                        printf(".%d", count),fflush(stdout);*/

                kd_insert((kd_tree)newTree,(kd_generic)spares->item,spares->size,(kd_generic)spares);
                spares = ptr;
        }
    return (kd_tree) newTree;
}

void unload_items(kd_tree tree, kd_list **nodelist, kd_box extent, long *items, double *mean)
{
	/* traverse the tree and collect the nodes bottom-up into a single list; delete
	   dead nodes, freeing them */
	extent[KD_LEFT] = MAXINT;
	extent[KD_BOTTOM] = MAXINT;
	extent[KD_FLOOR] = MAXINT;
	extent[KD_RIGHT] = -MAXINT;
	extent[KD_TOP] = -MAXINT;
	extent[KD_CEIL] = -MAXINT;


	collect_nodes(tree,((KDTree *)tree)->tree,nodelist,extent,items,mean);

	*mean /= *items;
}

void collect_nodes(kd_tree atree, kd_list *nodeptr, kd_list **nodelist, kd_box extent, long *items, double *mean)
{
	KDTree *tree = (KDTree *)atree;

	if( nodeptr->sons[KD_LOSON] )
		collect_nodes(atree,(kd_list *)nodeptr->sons[KD_LOSON],nodelist,extent,items,mean);
	if( nodeptr->sons[KD_HISON] )
		collect_nodes(atree,(kd_list *)nodeptr->sons[KD_HISON],nodelist,extent,items,mean);
	/* Here I am at a son with no kids, sort of */

	if( ! nodeptr->item ) /* a dead node */
	{
		/* free it and move on */
		free((char *)nodeptr);
		tree->dead_count--;
		tree->item_count--;
	}
	else
	{
		/* add to the list */
		nodeptr->sons[0] = *nodelist;
		*nodelist = nodeptr;
		/* make sure the leftover ptr is nil */
		nodeptr->sons[1] = 0;
		/* the tree is a node smaller */
		tree->item_count--;
		/* recalc the extents using this node */
		if( nodeptr->size[KD_LEFT] < extent[KD_LEFT])
			extent[KD_LEFT] = nodeptr->size[KD_LEFT];
		if (nodeptr->size[KD_BOTTOM] < extent[KD_BOTTOM])
			extent[KD_BOTTOM] = nodeptr->size[KD_BOTTOM];
		if (nodeptr->size[KD_FLOOR] < extent[KD_FLOOR])
			extent[KD_FLOOR] = nodeptr->size[KD_FLOOR];
		if (nodeptr->size[KD_RIGHT] > extent[KD_RIGHT])
			extent[KD_RIGHT] = nodeptr->size[KD_RIGHT];
		if (nodeptr->size[KD_TOP] > extent[KD_TOP])
			extent[KD_TOP] = nodeptr->size[KD_TOP];
		if (nodeptr->size[KD_CEIL] > extent[KD_CEIL])
			extent[KD_CEIL] = nodeptr->size[KD_CEIL];
		/* calc the passed in values to help rebuild the tree */
		(*items)++;
		(*mean) += nodeptr->size[KD_LEFT];
	}
}


/* ************** find_min_max_node  -- for "real" deletion of a node in a kd-tree ***************** */
/* Coded by Steve Murphy, Sept 1990                                                  */

static int nodecmp(KDElem *a, KDElem *b, int disc)
{
	int val;
	int new_disc;

	val = a->size[disc] - b->size[disc];        if (val == 0)
        {
                /* Cyclical comparison required */
                new_disc = NEXTDISC(disc);
                while (new_disc != disc)
                {
                        val = a->size[new_disc] - b->size[new_disc];
                        if (val != 0) break;
                        new_disc = NEXTDISC(new_disc);
                }
                if (val == 0) val = 1; /* Force upward if equal */
        }
        val = (val >= 0);
        /* val = 0 means a<b, 1= a>b */
        return val;
}

int find_min_max_node(int j, KDElem **kd_minval_node, KDElem **kd_minval_nodesdad, int *dir, int *newj)
// KDElem **kd_minval_node,**kd_minval_nodesdad; /* q is the maximum loson, or the minimum hison, depending on dir, and qdad is q's dad. */
// int j,*dir,*newj; /* j is the discriminator index (0-3), dir is HISON or LOSON (qdad[dir]==q. newj is minval_node's disc*/
{
        KDState *realGen;
    int kd_minval = (*kd_minval_node)->size[j];
        
    realGen = ALLOC(KDState);
        
        kd_data_tries = 0;
        
    realGen->stack_size = KD_INIT_STACK;
    realGen->top_index = 0;
    realGen->stk = MULTALLOC(KDSave, KD_INIT_STACK);

    /* Initialize search state */
    if (*kd_minval_node)
        {
                KD_PUSH(realGen, *kd_minval_node, NEXTDISC(j));
    }
        else
        {
                realGen->top_index = -1;
    }

        if( *dir == KD_HISON ) /* we are trying to find the smallest value of k[j], in the HISON subtree; */
        {
                register KDSave *top_elem;
                register KDElem *top_item;
                short hort,m;

                while (realGen->top_index > 0)
                {
                        top_elem = &(realGen->stk[realGen->top_index-1]);
                        top_item = top_elem->item;
                        hort = top_elem->disc & 0x01;/* zero: vertical, one: horizontal */
                        m = top_elem->disc;
                                
                        switch (top_elem->state)
                        {
                        case KD_THIS_ONE:
                                /* Check this one */
                                kd_data_tries++;
                                
                                if (top_item->item && !nodecmp(top_item,*kd_minval_node,j) && top_item != *kd_minval_node)
                                {                               /* when items have equal discriminators, choose the deepest to the left */
                                        *kd_minval_node = top_item;
                                        *kd_minval_nodesdad = realGen->stk[realGen->top_index-2].item;
                                        if( *kd_minval_node == (*kd_minval_nodesdad)->sons[KD_LOSON] )
                                                *dir = KD_LOSON;
                                        else
                                                *dir = KD_HISON;
                                        *newj = m;
                                        kd_minval = top_item->size[j];
                                        top_elem->state += 1;
                                } else
                                {
                                        top_elem->state += 1;
                                }
                                break;
                        case KD_LOSON: /* we would always take the loson, to find a minimum value. */
                                /* See if we push on the loson */
                                if( top_item->sons[KD_LOSON] )
                                {
                                        top_elem->state += 1;
                                        KD_PUSH(realGen, top_item->sons[KD_LOSON],
                                                        NEXTDISC(m));
                                }
                                else
                                        top_elem->state += 1;
                                break;
                        case KD_HISON: /* we can disqualify the hison iff j == m && top_item->size[m] > (*kd_minval_node)->size[m] */
                                /* See if we push on the hison */
                                if (j == m && top_item->size[m] > (*kd_minval_node)->size[m])
                                {
                                        top_elem->state += 1;
                                }
                                else
                                {
                                        if( top_item->sons[KD_HISON] )
                                        {
                                                top_elem->state += 1;
                                                KD_PUSH(realGen, top_item->sons[KD_HISON],
                                                                NEXTDISC(m));
                                        }
                                        else
                                                top_elem->state += 1;
                                }
                                break;
                        default:
                                /* We have exhausted this node -- pop off the next one */
                                realGen->top_index -= 1;
                                break;
                        }
                }
                FREE(realGen->stk);
                FREE(realGen);
                return kd_data_tries;
        }
        else /* we are trying to find the maximal value of k[j], in the LOSON subree. */
        {
                register KDSave *top_elem;
                register KDElem *top_item;
                short hort,m;

                while (realGen->top_index > 0)
                {
                        top_elem = &(realGen->stk[realGen->top_index-1]);
                        top_item = top_elem->item;
                        hort = top_elem->disc & 0x01;/* zero: vertical, one: horizontal */
                        m = top_elem->disc;
                        
                        switch (top_elem->state)
                        {
                        case KD_THIS_ONE:
                                /* Check this one */
                                kd_data_tries++;
                                
                                if (top_item->item && nodecmp(top_item,*kd_minval_node,j) && top_item != *kd_minval_node)
                                {                               /* when items have equal discriminators, choose the deepest to the right */
                                        *kd_minval_node = top_item;
                                        *kd_minval_nodesdad = realGen->stk[realGen->top_index-2].item;
                                        kd_minval = top_item->size[j];
                                        if( *kd_minval_node == (*kd_minval_nodesdad)->sons[KD_LOSON] )
                                                *dir = KD_LOSON;
                                        else
                                                *dir = KD_HISON;
                                        *newj = m;
                                        top_elem->state += 1;
                                } else
                                {
                                        top_elem->state += 1;
                                }
                                break;
                        case KD_LOSON: /* we can disqualify the loson iff j == m && top_item->size[m] < (*kd_minval_node)->size[m] */
                                if (j == m && top_item->size[m] < (*kd_minval_node)->size[m] )
                                {
                                        top_elem->state += 1;
                                }
                                else                                {
                                        if( top_item->sons[KD_LOSON] )
                                        {
                                                top_elem->state += 1;
                                                KD_PUSH(realGen, top_item->sons[KD_LOSON],
                                                                NEXTDISC(m));
                                        }
                                        else
                                                top_elem->state += 1;
                                }
                                break;
                        case KD_HISON: /* we would always take the hison, to find a maximum value. */
                                if( top_item->sons[KD_HISON] )
                                {
                                        top_elem->state += 1;
                                        KD_PUSH(realGen, top_item->sons[KD_HISON],
                                                        NEXTDISC(m));
                                }
                                else
                                        top_elem->state += 1;
                                break;
                        default:
                                /* We have exhausted this node -- pop off the next one */
                                realGen->top_index -= 1;
                                break;
                        }
                }
                FREE(realGen->stk);
                FREE(realGen);
                return kd_data_tries;
        }
}

kd_status kd_is_member(kd_tree theTree, kd_generic data, kd_box size)
// kd_tree theTree;             /* Tree to examine  */
// kd_generic data;             /* Item to look for */
// kd_box size;                 /* Original size    */
/*
 * Returns KD_OK if `data' is stored in tree `theTree' with
 * the location `size' and KD_NOTFOUND if not.
 */
{
    KDTree *real_tree = (KDTree *) theTree;

    if (find_item(real_tree->tree, 0, data, size, 1, 0)) {
        return KD_OK;
    } else {
        return KD_NOTFOUND;
    }
}

/*
 * Deletion
 *
 * Since the preconditions of a k-d tree are moderately complex,  the
 * deletion routine simply marks a node as deleted if it has sons.
 * If it doesn't have sons,  it can be fully deleted.  The routine
 * updates count information in the tree for eventual rebuild once
 * the `badness' of the tree exceeds a threshold.  This is not
 * implemented initially.
 *
 * Items are marked for deletion by setting the item pointer
 * to zero.  This means zero data items are not allowed.
 */

kd_status kd_delete(kd_tree theTree, kd_generic data, kd_box old_size)
// kd_tree theTree;             /* Tree to delete from  */
// kd_generic data;             /* Item to delete       */
// kd_box old_size;             /* Original size        */
/*
 * Deletes the specified item from the specified k-d tree.  `old_size'
 * must be provided to efficiently locate the item.  May return
 * KD_NOTFOUND if the item is not in the tree.
 */
{
    KDTree *real_tree = (KDTree *) theTree;
    KDElem *elem;

    elem = find_item(real_tree->tree, 0, data, old_size, 1, 0);
    if (elem) {
        /* Delete element */
        elem->item = (kd_generic) 0;
        (real_tree->dead_count)++;
        return del_element(real_tree, elem, path_length);
    } else {
        return kd_set_error(KD_NOTFOUND);
    }
}

static long kddel_number_tried=0;
static long kddel_number_deld=0;

void kd_delete_stats(int *tries,int *levs)
{
        *tries = kddel_number_tried;
        *levs  = kddel_number_deld;
}

KDElem *kd_do_delete(KDTree *,KDElem *,int);

/* This routine and its subfuncs implement the recursive delete function
   described by JL Bentley on p. 515 of his article in the Comm. of the ACM,
   Sept 1975, Vol 18, No. 9.

   On the average, the comments about it's efficiency are correct. On the average,
   it's darned fast. Very few nodes affected. Searches for replacements are very
   short.


   Steve Murphy,  1990

   */

kd_status kd_really_delete(kd_tree theTree, kd_generic data, kd_box old_size, int *num_tries, int *num_del)
// kd_tree theTree;             /* Tree to delete from  */
// kd_generic data;             /* Item to delete       */
// kd_box old_size;             /* Original size        */
// int *num_tries, *num_del; /* stats returned about how much work it took */
/*
 * Deletes the specified item from the specified k-d tree.  `old_size'
 * must be provided to efficiently locate the item.  May return
 * KD_NOTFOUND if the item is not in the tree.
 */
{
    KDTree *real_tree = (KDTree *) theTree;
    KDElem *elem,*elemdad,*newelem;
        int j;
        kddel_number_tried = 0;
        kddel_number_deld = 1;

    elem = find_item(real_tree->tree, 0, data, old_size, 1,0);
    if (elem)
        {
                if (elem == real_tree->tree)
                {
                        /* Deleting the root node -- path_to_item has no ancestors recorded,
                           and path_length is stale. Root always has discriminator 0. */
                        j = 0;
                        newelem = kd_do_delete(real_tree, elem, j);
                        real_tree->tree = newelem;
                }
                else
                {
                        elemdad = path_to_item[path_length-1];
                        /* Delete element */
                        j = KD_DISC(path_length);
                        newelem = kd_do_delete(real_tree,elem,j);
                        if( elemdad->sons[KD_HISON] == elem )
                                elemdad->sons[KD_HISON] = newelem;
                        else
                                elemdad->sons[KD_LOSON] = newelem;
                }
                FREE(elem);
                real_tree->item_count--;
        }
        else
        {
                *num_tries = 0;
                *num_del = 0;
                return KD_NOTFOUND;
        }
        *num_tries = kddel_number_tried;
        *num_del = kddel_number_deld;
        return KD_OK;
}

KDElem *kd_do_delete(KDTree *real_tree, KDElem *elem, int j)
// KDTree *real_tree;           /* Tree to delete from  */
// KDElem *elem;           /* element to delete */
// int j;                  /* j is the disc of elem */
/*
 * Deletes the specified item from the specified k-d tree.  `old_size'
 * must be provided to efficiently locate the item.  May return
 * KD_NOTFOUND if the item is not in the tree.
 */
{
        KDElem *Q,*Qdad;
        int Qson;
        static char flip = 0;


        flip = !flip;

        /* Delete element */
        if( !elem->sons[KD_HISON] && !elem->sons[KD_LOSON])
        {
                return 0;
        }
        else
        {
                int newj;

                Qdad = elem;
                if( !elem->sons[KD_HISON])
                        flip = 0;
                else if( !elem->sons[KD_LOSON] )
                        flip = 1;
                if( !flip ) /* loson */
                {
                        Q = elem->sons[KD_LOSON];
                        Qson = KD_LOSON;
                        newj = NEXTDISC(j);
                        kddel_number_tried += find_min_max_node(j,&Q,&Qdad,&Qson,&newj);
                }
                else  /* hison */
                {
                        Q = elem->sons[KD_HISON];
                        Qson = KD_HISON;
                        newj = NEXTDISC(j);
                        kddel_number_tried += find_min_max_node(j,&Q,&Qdad,&Qson,&newj);
                }
                Qdad->sons[Qson] = kd_do_delete(real_tree, Q, newj);
                kddel_number_deld++;
                Q->sons[KD_LOSON] = elem->sons[KD_LOSON];
                Q->sons[KD_HISON] = elem->sons[KD_HISON];
                Q->lo_min_bound = elem->lo_min_bound; /* you have to inherit the bounds information as well */                Q->other_bound = elem->other_bound;
                Q->hi_max_bound = elem->hi_max_bound;
                /* fprintf(stderr,"<del=%d>",(int)(Q->item)+1); */
                return Q;
        }
}

static kd_status del_element(KDTree *tree, KDElem *elem, int spot)
// KDTree *tree;                        /* Tree
// KDElem *elem;                        /* Item to delete
// int spot;                    /* Last item in path
/*
 * This routine deletes `elem' from its tree.  It assumes that
 * the path information down to the element is stored in
 * path_to_item (see find_item() for details).  If the node
 * has no children,  it is deleted and the counts are modified
 * appropriately.  The routine is called recursively on its
 * parent.  If it has children,  it is left and the recursion
 * stops.
 */
{
    if (elem->item == (kd_generic) 0)
        {
                if (!elem->sons[KD_LOSON] && !elem->sons[KD_HISON])
                {
                        if (spot > 0)
                        {
                                if (path_to_item[spot-1]->sons[KD_LOSON] == elem)
                                {
                                        path_to_item[--spot]->sons[KD_LOSON] = (KDElem *) 0;
                                } else if (path_to_item[spot-1]->sons[KD_HISON] == elem)
                                {
                                        path_to_item[--spot]->sons[KD_HISON] = (KDElem *) 0;
                                } else
                                {
                                        (void) kd_fault(KDF_F);
                                }
                                FREE(elem);
                                (tree->dead_count)--;
                                (tree->item_count)--;
                                return del_element(tree, path_to_item[spot], spot);
                        } else
                        {
                                tree->tree = (KDElem *) 0;
                                FREE(elem);
                                (tree->dead_count)--;
                                (tree->item_count)--;
                                return KD_OK;
                        }
                }
    }
    /* No work required */
    return KD_OK;
}


/*
 * Generation of items
 */


// #define KD_PUSH kd_push

static void kd_push(KDState *gen, KDElem *elem, short disc)
// KDState *gen;                        /* Generator       */
// KDElem *elem;                        /* Element to push */
// short disc;                  /* Discriminator   */
/*
 * This routine pushes a new kd-tree element onto the generation
 * stack.  It also initializes the generation of items in
 * this element.
 */
{
    /* Allocate more space if necessary */
    if (gen->top_index >= gen->stack_size) {
        gen->stack_size += KD_GROWSIZE(gen->stack_size);
        gen->stk = REALLOC(KDSave, gen->stk, gen->stack_size);
    }
    gen->stk[gen->top_index].disc = disc;
    gen->stk[gen->top_index].state = KD_THIS_ONE;
    gen->stk[gen->top_index].item = elem;
    gen->top_index += 1;
}



#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>

#pragma pack(push, 1)
typedef struct kd_mmap_node {
    uint64_t source_id;
    coord_t size[6];
    coord_t lo_min_bound;
    coord_t hi_max_bound;
    coord_t other_bound;
    int64_t left_child;
    int64_t right_child;
} kd_mmap_node;
#pragma pack(pop)

extern int ftruncate(int fd, off_t length);

static int64_t kd_serialize_node(KDElem *node, kd_mmap_node *array, int64_t *current_idx) {
    if (!node || node->item == NULL) return -1;
    
    int64_t my_idx = *current_idx;
    (*current_idx)++;
    
    kd_mmap_node *mmap_node = &array[my_idx];
    mmap_node->source_id = (uint64_t)(uintptr_t)node->item;
    for(int i=0; i<6; i++) mmap_node->size[i] = node->size[i];
    mmap_node->lo_min_bound = node->lo_min_bound;
    mmap_node->hi_max_bound = node->hi_max_bound;
    mmap_node->other_bound = node->other_bound;
    
    mmap_node->left_child = kd_serialize_node(node->sons[0], array, current_idx);
    mmap_node->right_child = kd_serialize_node(node->sons[1], array, current_idx);
    
    return my_idx;
}

int kd_serialize(kd_tree tree, const char *filename) {
    if (!tree) return -1;
    KDTree *realTree = (KDTree *)tree;
    int64_t count = realTree->item_count - realTree->dead_count;
    if (count <= 0) return -1;
    
    int fd = open(filename, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (fd == -1) return -1;
    
    size_t file_size = count * sizeof(kd_mmap_node);
    if (ftruncate(fd, file_size) == -1) { close(fd); return -1; }
    
    kd_mmap_node *array = mmap(NULL, file_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (array == MAP_FAILED) { close(fd); return -1; }
    
    int64_t current_idx = 0;
    kd_serialize_node(realTree->tree, array, &current_idx);
    
    msync(array, file_size, MS_SYNC);
    munmap(array, file_size);
    close(fd);
    return 0;
}

static void get_bounds_recursive(KDElem *node, coord_t *bounds) {
    if (!node || node->item == NULL) return;
    
    for (int i = 0; i < 3; i++) {
        if (node->size[i] < bounds[i]) bounds[i] = node->size[i];
        if (node->size[i + 3] > bounds[i + 3]) bounds[i + 3] = node->size[i + 3];
    }
    
    get_bounds_recursive(node->sons[0], bounds);
    get_bounds_recursive(node->sons[1], bounds);
}

int kd_get_bounds(kd_tree tree, kd_box bounds) {
    if (!tree) return KD_NOTFOUND;
    KDTree *realTree = (KDTree *)tree;
    if (!realTree->tree || realTree->tree->item == NULL) {
        return KD_NOTFOUND;
    }
    
    for (int i = 0; i < 3; i++) {
        bounds[i] = realTree->tree->size[i];
        bounds[i + 3] = realTree->tree->size[i + 3];
    }
    
    get_bounds_recursive(realTree->tree, bounds);
    return KD_OK;
}

#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <unistd.h>

int kd_get_serialized_bounds(const char *filename, kd_box bounds) {
    int fd = open(filename, O_RDONLY);
    if (fd == -1) return KD_NOTFOUND;
    
    struct stat sb;
    if (fstat(fd, &sb) == -1 || sb.st_size == 0) {
        close(fd);
        return KD_NOTFOUND;
    }
    
    size_t node_count = sb.st_size / sizeof(kd_mmap_node);
    if (node_count == 0) {
        close(fd);
        return KD_NOTFOUND;
    }
    
    kd_mmap_node *nodes = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    if (nodes == MAP_FAILED) {
        close(fd);
        return KD_NOTFOUND;
    }
    
    int first = 1;
    for (size_t i = 0; i < node_count; i++) {
        if (nodes[i].source_id != 0) {
            if (first) {
                for (int d = 0; d < 3; d++) {
                    bounds[d] = nodes[i].size[d];
                    bounds[d + 3] = nodes[i].size[d + 3];
                }
                first = 0;
            } else {
                for (int d = 0; d < 3; d++) {
                    if (nodes[i].size[d] < bounds[d]) bounds[d] = nodes[i].size[d];
                    if (nodes[i].size[d + 3] > bounds[d + 3]) bounds[d + 3] = nodes[i].size[d + 3];
                }
            }
        }
    }
    
    munmap(nodes, sb.st_size);
    close(fd);
    
    return first ? KD_NOTFOUND : KD_OK;
}
