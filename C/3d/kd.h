/*
 * Headers for k-d tree geometric data structure
 */

#ifndef KD_HEADER
#define KD_HEADER

#include <stdio.h>
#include <stdint.h>

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
 * 
 * 
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

#include "../include/kdtree.h"

/* 32-bit vs 64-bit vs 128-bit vs float64 precision configuration */
#if defined(COORD_F64)
typedef double coord_t;
#elif defined(COORD_128)
typedef __int128 coord_t;
#elif defined(COORD_64)
typedef int64_t coord_t;
#else
typedef int32_t coord_t;
#endif

/* Unified function names with dimension and bit-width suffixes */
#if defined(COORD_F64)
#define kd_create         kd_3d_f64_create
#define kd_build          kd_3d_f64_build
#define kd_destroy        kd_3d_f64_destroy
#define kd_is_member      kd_3d_f64_is_member
#define kd_insert         kd_3d_f64_insert
#define kd_delete         kd_3d_f64_delete
#define kd_really_delete  kd_3d_f64_really_delete
#define kd_start          kd_3d_f64_start
#define kd_next           kd_3d_f64_next
#define kd_finish         kd_3d_f64_finish
#define kd_count          kd_3d_f64_count
#define kd_print          kd_3d_f64_print
#define kd_badness        kd_3d_f64_badness
#define kd_rebuild        kd_3d_f64_rebuild
#define kd_serialize      kd_3d_f64_serialize
#define kd_get_bounds     kd_3d_f64_get_bounds
#define kd_get_serialized_bounds kd_3d_f64_get_serialized_bounds
#define kd_get_mmap_bounds kd_3d_f64_get_mmap_bounds
#define kd_mmap_node      kd_3d_f64_mmap_node
#define kd_priority       kd_priority_3d_f64
#define kd_nearest        kd_3d_f64_nearest
#define kd_print_nearest  kd_3d_f64_print_nearest
#define collect_nodes     kd_3d_f64_collect_nodes
#define kd_delete_stats   kd_3d_f64_delete_stats
#define kd_do_delete      kd_3d_f64_do_delete
#define kd_err_string     kd_3d_f64_err_string
#define kd_print_path     kd_3d_f64_print_path
#define kd_set_build_depth kd_3d_f64_set_build_depth
#define NEW_PATH          kd_3d_f64_NEW_PATH
#define unload_items      kd_3d_f64_unload_items
#define kd_pkg_name       kd_3d_f64_pkg_name
#elif defined(COORD_128)
#define kd_create         kd_3d_128_create
#define kd_build          kd_3d_128_build
#define kd_destroy        kd_3d_128_destroy
#define kd_is_member      kd_3d_128_is_member
#define kd_insert         kd_3d_128_insert
#define kd_delete         kd_3d_128_delete
#define kd_really_delete  kd_3d_128_really_delete
#define kd_start          kd_3d_128_start
#define kd_next           kd_3d_128_next
#define kd_finish         kd_3d_128_finish
#define kd_count          kd_3d_128_count
#define kd_print          kd_3d_128_print
#define kd_badness        kd_3d_128_badness
#define kd_rebuild        kd_3d_128_rebuild
#define kd_serialize      kd_3d_128_serialize
#define kd_get_bounds     kd_3d_128_get_bounds
#define kd_get_serialized_bounds kd_3d_128_get_serialized_bounds
#define kd_get_mmap_bounds kd_3d_128_get_mmap_bounds
#define kd_mmap_node      kd_3d_128_mmap_node
#define kd_priority       kd_priority_3d_128
#define kd_nearest        kd_3d_128_nearest
#define kd_print_nearest  kd_3d_128_print_nearest
#define collect_nodes     kd_3d_128_collect_nodes
#define kd_delete_stats   kd_3d_128_delete_stats
#define kd_do_delete      kd_3d_128_do_delete
#define kd_err_string     kd_3d_128_err_string
#define kd_print_path     kd_3d_128_print_path
#define kd_set_build_depth kd_3d_128_set_build_depth
#define NEW_PATH          kd_3d_128_NEW_PATH
#define unload_items      kd_3d_128_unload_items
#define kd_pkg_name       kd_3d_128_pkg_name
#elif defined(COORD_64)
#define kd_create         kd_3d_64_create
#define kd_build          kd_3d_64_build
#define kd_destroy        kd_3d_64_destroy
#define kd_is_member      kd_3d_64_is_member
#define kd_insert         kd_3d_64_insert
#define kd_delete         kd_3d_64_delete
#define kd_really_delete  kd_3d_64_really_delete
#define kd_start          kd_3d_64_start
#define kd_next           kd_3d_64_next
#define kd_finish         kd_3d_64_finish
#define kd_count          kd_3d_64_count
#define kd_print          kd_3d_64_print
#define kd_badness        kd_3d_64_badness
#define kd_rebuild        kd_3d_64_rebuild
#define kd_serialize      kd_3d_64_serialize
#define kd_get_bounds     kd_3d_64_get_bounds
#define kd_get_serialized_bounds kd_3d_64_get_serialized_bounds
#define kd_get_mmap_bounds kd_3d_64_get_mmap_bounds
#define kd_mmap_node      kd_3d_64_mmap_node
#define kd_priority       kd_priority_3d_64
#define kd_nearest        kd_3d_64_nearest
#define kd_print_nearest  kd_3d_64_print_nearest
#define collect_nodes     kd_3d_64_collect_nodes
#define kd_delete_stats   kd_3d_64_delete_stats
#define kd_do_delete      kd_3d_64_do_delete
#define kd_err_string     kd_3d_64_err_string
#define kd_print_path     kd_3d_64_print_path
#define kd_set_build_depth kd_3d_64_set_build_depth
#define NEW_PATH          kd_3d_64_NEW_PATH
#define unload_items      kd_3d_64_unload_items
#define kd_pkg_name       kd_3d_64_pkg_name
#else
#define kd_create         kd_3d_32_create
#define kd_build          kd_3d_32_build
#define kd_destroy        kd_3d_32_destroy
#define kd_is_member      kd_3d_32_is_member
#define kd_insert         kd_3d_32_insert
#define kd_delete         kd_3d_32_delete
#define kd_really_delete  kd_3d_32_really_delete
#define kd_start          kd_3d_32_start
#define kd_next           kd_3d_32_next
#define kd_finish         kd_3d_32_finish
#define kd_count          kd_3d_32_count
#define kd_print          kd_3d_32_print
#define kd_badness        kd_3d_32_badness
#define kd_rebuild        kd_3d_32_rebuild
#define kd_serialize      kd_3d_32_serialize
#define kd_get_bounds     kd_3d_32_get_bounds
#define kd_get_serialized_bounds kd_3d_32_get_serialized_bounds
#define kd_get_mmap_bounds kd_3d_32_get_mmap_bounds
#define kd_mmap_node      kd_3d_32_mmap_node
#define kd_priority       kd_priority_3d_32
#define kd_nearest        kd_3d_32_nearest
#define kd_print_nearest  kd_3d_32_print_nearest
#define collect_nodes     kd_3d_32_collect_nodes
#define kd_delete_stats   kd_3d_32_delete_stats
#define kd_do_delete      kd_3d_32_do_delete
#define kd_err_string     kd_3d_32_err_string
#define kd_print_path     kd_3d_32_print_path
#define kd_set_build_depth kd_3d_32_set_build_depth
#define NEW_PATH          kd_3d_32_NEW_PATH
#define unload_items      kd_3d_32_unload_items
#define kd_pkg_name       kd_3d_32_pkg_name
#endif

extern char *kd_pkg_name;	/* For error handling */

#define KD_LEFT		0
#define KD_BOTTOM	1
#define KD_FLOOR	2
#define KD_RIGHT	3
#define KD_TOP		4
#define KD_CEIL		5
#define KD_BOX_MAX	6

typedef coord_t kd_box[6];
typedef coord_t *kd_box_r;
/* Fatal Faults */
#define KDF_M		0	/* Memory fault    */
#define KDF_ZEROID	1	/* Insert zero     */
#define KDF_MD		2	/* Bad median      */
#define KDF_F		3	/* Father fault    */
#define KDF_DUPL	4	/* Duplicate entry */
#define KDF_UNKNOWN	99	/* Unknown error   */

#define KD_DISC(lev) (lev%6)

extern char *kd_err_string(void);
  /* Returns a textual description of a k-d error */

extern kd_tree kd_create(void);
  /* Creates a new empty kd-tree */

extern kd_tree kd_build(int (*itemfunc)(kd_generic arg, kd_generic *val, kd_box size), kd_generic );
  /* Makes a new kd-tree from a given set of items */

extern void kd_destroy(kd_tree this_one, void (*delfunc)(kd_generic item));
  /* Destroys an existing k-d tree */

extern kd_status kd_is_member(kd_tree , kd_generic , kd_box );
  /* Tries to find a specific item in a tree */

extern void kd_insert(kd_tree , kd_generic , kd_box, kd_generic );
  /* Inserts a new node into a k-d tree */

extern kd_status kd_delete(kd_tree , kd_generic , kd_box );
  /* Deletes a node from a k-d tree */

extern kd_status kd_really_delete (kd_tree theTree, kd_generic data, kd_box old_size, int *num_tries, int *num_del);

extern kd_gen kd_start (kd_tree tree, kd_box size);
  /* Initializes a generation of items in a region */

extern kd_status kd_next (kd_gen , kd_generic *, kd_box);
  /* Generates the next item in a region */

extern int kd_finish (kd_gen);
  /* Ends generation of items in a region */

extern int kd_count (kd_tree tree);
  /* Returns the number of objects stored in tree */

extern void kd_print (kd_tree);

extern void kd_badness (kd_tree);

extern kd_tree kd_rebuild ( kd_tree );

typedef struct KDElem {
    kd_generic item;            /* Actual item at this node */
    kd_box size;                /* Size of item             */
    coord_t lo_min_bound;       /* Lower minimum boundary   */
    coord_t hi_max_bound;       /* High maximum boundary    */
    coord_t other_bound;        /* Discriminator dependent  */
    struct KDElem *sons[2];     /* Children                 */
} KDElem;

extern int kd_nearest (kd_tree tree, coord_t x, coord_t y, coord_t z, int m, kd_priority **alist);
extern void kd_print_nearest (kd_tree tree, coord_t x, coord_t y, coord_t z, int m);

extern int kd_serialize(kd_tree tree, const char *filename);
extern int kd_get_bounds(kd_tree tree, kd_box bounds);
extern int kd_get_serialized_bounds(const char *filename, kd_box bounds);
extern int kd_get_mmap_bounds(const kd_mmap_node *nodes, size_t node_count, kd_box bounds);

#endif /* KD_HEADER */
