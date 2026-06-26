#ifndef KDTREE_H
#define KDTREE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef int kd_status;
typedef char *kd_generic;

typedef struct kd_dummy_defn {
    int dummy;
} kd_dummy;

typedef kd_dummy *kd_tree;
typedef kd_dummy *kd_gen;

/* Return values */
#define KD_OK       1
#define KD_NOMORE   2
#define KD_NOTIMPL  -3
#define KD_NOTFOUND -4 

/* 2D 32-bit API */
typedef int32_t kd_2d_32_box[4];
typedef struct { double dist; kd_generic elem; } kd_priority_2d_32;

extern kd_tree kd_2d_32_create(void);
extern kd_tree kd_2d_32_build(int (*itemfunc)(kd_generic arg, kd_generic *val, kd_2d_32_box size), kd_generic );
extern void kd_2d_32_destroy(kd_tree this_one, void (*delfunc)(kd_generic item));
extern kd_status kd_2d_32_is_member(kd_tree , kd_generic , kd_2d_32_box );
extern void kd_2d_32_insert(kd_tree , kd_generic , kd_2d_32_box, kd_generic );
extern kd_status kd_2d_32_delete(kd_tree , kd_generic , kd_2d_32_box );
extern kd_status kd_2d_32_really_delete(kd_tree theTree, kd_generic data, kd_2d_32_box old_size, int *num_tries, int *num_del);
extern kd_gen kd_2d_32_start(kd_tree tree, kd_2d_32_box size);
extern kd_status kd_2d_32_next(kd_gen , kd_generic *, kd_2d_32_box);
extern int kd_2d_32_finish(kd_gen);
extern int kd_2d_32_count(kd_tree tree);
extern void kd_2d_32_print(kd_tree);
extern void kd_2d_32_badness(kd_tree);
extern kd_tree kd_2d_32_rebuild(kd_tree);
extern int kd_2d_32_nearest(kd_tree tree, int32_t x, int32_t y, int m, kd_priority_2d_32 **alist);
extern void kd_2d_32_print_nearest(kd_tree tree, int32_t x, int32_t y, int m);

/* 2D 64-bit API */
typedef int64_t kd_2d_64_box[4];
typedef struct { double dist; kd_generic elem; } kd_priority_2d_64;

extern kd_tree kd_2d_64_create(void);
extern kd_tree kd_2d_64_build(int (*itemfunc)(kd_generic arg, kd_generic *val, kd_2d_64_box size), kd_generic );
extern void kd_2d_64_destroy(kd_tree this_one, void (*delfunc)(kd_generic item));
extern kd_status kd_2d_64_is_member(kd_tree , kd_generic , kd_2d_64_box );
extern void kd_2d_64_insert(kd_tree , kd_generic , kd_2d_64_box, kd_generic );
extern kd_status kd_2d_64_delete(kd_tree , kd_generic , kd_2d_64_box );
extern kd_status kd_2d_64_really_delete(kd_tree theTree, kd_generic data, kd_2d_64_box old_size, int *num_tries, int *num_del);
extern kd_gen kd_2d_64_start(kd_tree tree, kd_2d_64_box size);
extern kd_status kd_2d_64_next(kd_gen , kd_generic *, kd_2d_64_box);
extern int kd_2d_64_finish(kd_gen);
extern int kd_2d_64_count(kd_tree tree);
extern void kd_2d_64_print(kd_tree);
extern void kd_2d_64_badness(kd_tree);
extern kd_tree kd_2d_64_rebuild(kd_tree);
extern int kd_2d_64_nearest(kd_tree tree, int64_t x, int64_t y, int m, kd_priority_2d_64 **alist);
extern void kd_2d_64_print_nearest(kd_tree tree, int64_t x, int64_t y, int m);

/* 3D 32-bit API */
typedef int32_t kd_3d_32_box[6];
typedef struct kd_priority_3d_32 { double dist; void *elem; } kd_priority_3d_32;

extern kd_tree kd_3d_32_create(void);
extern kd_tree kd_3d_32_build(int (*itemfunc)(kd_generic arg, kd_generic *val, kd_3d_32_box size), kd_generic );
extern void kd_3d_32_destroy(kd_tree this_one, void (*delfunc)(kd_generic item));
extern kd_status kd_3d_32_is_member(kd_tree , kd_generic , kd_3d_32_box );
extern void kd_3d_32_insert(kd_tree , kd_generic , kd_3d_32_box, kd_generic );
extern kd_status kd_3d_32_delete(kd_tree , kd_generic , kd_3d_32_box );
extern kd_status kd_3d_32_really_delete(kd_tree theTree, kd_generic data, kd_3d_32_box old_size, int *num_tries, int *num_del);
extern kd_gen kd_3d_32_start(kd_tree tree, kd_3d_32_box size);
extern kd_status kd_3d_32_next(kd_gen , kd_generic *, kd_3d_32_box);
extern int kd_3d_32_finish(kd_gen);
extern int kd_3d_32_count(kd_tree tree);
extern void kd_3d_32_print(kd_tree);
extern void kd_3d_32_badness(kd_tree);
extern kd_tree kd_3d_32_rebuild(kd_tree);
extern int kd_3d_32_nearest(kd_tree tree, int32_t x, int32_t y, int32_t z, int m, kd_priority_3d_32 **alist);
extern void kd_3d_32_print_nearest(kd_tree tree, int32_t x, int32_t y, int32_t z, int m);

/* 3D 64-bit API */
typedef int64_t kd_3d_64_box[6];
typedef struct kd_priority_3d_64 { double dist; void *elem; } kd_priority_3d_64;

extern kd_tree kd_3d_64_create(void);
extern kd_tree kd_3d_64_build(int (*itemfunc)(kd_generic arg, kd_generic *val, kd_3d_64_box size), kd_generic );
extern void kd_3d_64_destroy(kd_tree this_one, void (*delfunc)(kd_generic item));
extern kd_status kd_3d_64_is_member(kd_tree , kd_generic , kd_3d_64_box );
extern void kd_3d_64_insert(kd_tree , kd_generic , kd_3d_64_box, kd_generic );
extern kd_status kd_3d_64_delete(kd_tree , kd_generic , kd_3d_64_box );
extern kd_status kd_3d_64_really_delete(kd_tree theTree, kd_generic data, kd_3d_64_box old_size, int *num_tries, int *num_del);
extern kd_gen kd_3d_64_start(kd_tree tree, kd_3d_64_box size);
extern kd_status kd_3d_64_next(kd_gen , kd_generic *, kd_3d_64_box);
extern int kd_3d_64_finish(kd_gen);
extern int kd_3d_64_count(kd_tree tree);
extern void kd_3d_64_print(kd_tree);
extern void kd_3d_64_badness(kd_tree);
extern kd_tree kd_3d_64_rebuild(kd_tree);
extern int kd_3d_64_nearest(kd_tree tree, int64_t x, int64_t y, int64_t z, int m, kd_priority_3d_64 **alist);
extern void kd_3d_64_print_nearest(kd_tree tree, int64_t x, int64_t y, int64_t z, int m);

#ifdef __cplusplus
}
#endif

#endif /* KDTREE_H */