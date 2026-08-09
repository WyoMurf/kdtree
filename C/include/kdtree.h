#ifndef KDTREE_H
#define KDTREE_H

#include <stdint.h>
#include <stddef.h>

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
#pragma pack(push, 1)
typedef struct {
    uint64_t source_id;
    int32_t size[4];
    int32_t lo_min_bound;
    int32_t hi_max_bound;
    int32_t other_bound;
    int64_t left_child;
    int64_t right_child;
} kd_2d_32_mmap_node;
#pragma pack(pop)

typedef int32_t kd_2d_32_box[4];
typedef struct kd_priority_2d_32 { double dist; kd_generic elem; } kd_priority_2d_32;

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
extern int kd_2d_32_serialize(kd_tree tree, const char *filename);
extern int kd_2d_32_get_bounds(kd_tree tree, kd_2d_32_box bounds);
extern int kd_2d_32_get_serialized_bounds(const char *filename, kd_2d_32_box bounds);
extern int kd_2d_32_get_mmap_bounds(const kd_2d_32_mmap_node *nodes, size_t node_count, kd_2d_32_box bounds);
extern int kd_2d_32_nearest(kd_tree tree, int32_t x, int32_t y, int m, kd_priority_2d_32 **alist);
extern void kd_2d_32_print_nearest(kd_tree tree, int32_t x, int32_t y, int m);

/* 2D 64-bit API */
#pragma pack(push, 1)
typedef struct {
    uint64_t source_id;
    int64_t size[4];
    int64_t lo_min_bound;
    int64_t hi_max_bound;
    int64_t other_bound;
    int64_t left_child;
    int64_t right_child;
} kd_2d_64_mmap_node;
#pragma pack(pop)

typedef int64_t kd_2d_64_box[4];
typedef struct kd_priority_2d_64 { double dist; kd_generic elem; } kd_priority_2d_64;

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
extern int kd_2d_64_serialize(kd_tree tree, const char *filename);
extern int kd_2d_64_get_bounds(kd_tree tree, kd_2d_64_box bounds);
extern int kd_2d_64_get_serialized_bounds(const char *filename, kd_2d_64_box bounds);
extern int kd_2d_64_get_mmap_bounds(const kd_2d_64_mmap_node *nodes, size_t node_count, kd_2d_64_box bounds);
extern int kd_2d_64_nearest(kd_tree tree, int64_t x, int64_t y, int m, kd_priority_2d_64 **alist);
extern void kd_2d_64_print_nearest(kd_tree tree, int64_t x, int64_t y, int m);

/* 2D 128-bit API */
#pragma pack(push, 1)
typedef struct {
    uint64_t source_id;
    __int128 size[4];
    __int128 lo_min_bound;
    __int128 hi_max_bound;
    __int128 other_bound;
    int64_t left_child;
    int64_t right_child;
} kd_2d_128_mmap_node;
#pragma pack(pop)

typedef __int128 kd_2d_128_box[4];
typedef struct kd_priority_2d_128 { double dist; kd_generic elem; } kd_priority_2d_128;

extern kd_tree kd_2d_128_create(void);
extern kd_tree kd_2d_128_build(int (*itemfunc)(kd_generic arg, kd_generic *val, kd_2d_128_box size), kd_generic );
extern void kd_2d_128_destroy(kd_tree this_one, void (*delfunc)(kd_generic item));
extern kd_status kd_2d_128_is_member(kd_tree , kd_generic , kd_2d_128_box );
extern void kd_2d_128_insert(kd_tree , kd_generic , kd_2d_128_box, kd_generic );
extern kd_status kd_2d_128_delete(kd_tree , kd_generic , kd_2d_128_box );
extern kd_status kd_2d_128_really_delete(kd_tree theTree, kd_generic data, kd_2d_128_box old_size, int *num_tries, int *num_del);
extern kd_gen kd_2d_128_start(kd_tree tree, kd_2d_128_box size);
extern kd_status kd_2d_128_next(kd_gen , kd_generic *, kd_2d_128_box);
extern int kd_2d_128_finish(kd_gen);
extern int kd_2d_128_count(kd_tree tree);
extern void kd_2d_128_print(kd_tree);
extern void kd_2d_128_badness(kd_tree);
extern kd_tree kd_2d_128_rebuild(kd_tree);
extern int kd_2d_128_serialize(kd_tree tree, const char *filename);
extern int kd_2d_128_get_bounds(kd_tree tree, kd_2d_128_box bounds);
extern int kd_2d_128_get_serialized_bounds(const char *filename, kd_2d_128_box bounds);
extern int kd_2d_128_get_mmap_bounds(const kd_2d_128_mmap_node *nodes, size_t node_count, kd_2d_128_box bounds);
extern int kd_2d_128_nearest(kd_tree tree, __int128 x, __int128 y, int m, kd_priority_2d_128 **alist);
extern void kd_2d_128_print_nearest(kd_tree tree, __int128 x, __int128 y, int m);

/* 2D float64 API */
#pragma pack(push, 1)
typedef struct {
    uint64_t source_id;
    double size[4];
    double lo_min_bound;
    double hi_max_bound;
    double other_bound;
    int64_t left_child;
    int64_t right_child;
} kd_2d_f64_mmap_node;
#pragma pack(pop)

typedef double kd_2d_f64_box[4];
typedef struct kd_priority_2d_f64 { double dist; kd_generic elem; } kd_priority_2d_f64;

extern kd_tree kd_2d_f64_create(void);
extern kd_tree kd_2d_f64_build(int (*itemfunc)(kd_generic arg, kd_generic *val, kd_2d_f64_box size), kd_generic );
extern void kd_2d_f64_destroy(kd_tree this_one, void (*delfunc)(kd_generic item));
extern kd_status kd_2d_f64_is_member(kd_tree , kd_generic , kd_2d_f64_box );
extern void kd_2d_f64_insert(kd_tree , kd_generic , kd_2d_f64_box, kd_generic );
extern kd_status kd_2d_f64_delete(kd_tree , kd_generic , kd_2d_f64_box );
extern kd_status kd_2d_f64_really_delete(kd_tree theTree, kd_generic data, kd_2d_f64_box old_size, int *num_tries, int *num_del);
extern kd_gen kd_2d_f64_start(kd_tree tree, kd_2d_f64_box size);
extern kd_status kd_2d_f64_next(kd_gen , kd_generic *, kd_2d_f64_box);
extern int kd_2d_f64_finish(kd_gen);
extern int kd_2d_f64_count(kd_tree tree);
extern void kd_2d_f64_print(kd_tree);
extern void kd_2d_f64_badness(kd_tree);
extern kd_tree kd_2d_f64_rebuild(kd_tree);
extern int kd_2d_f64_serialize(kd_tree tree, const char *filename);
extern int kd_2d_f64_get_bounds(kd_tree tree, kd_2d_f64_box bounds);
extern int kd_2d_f64_get_serialized_bounds(const char *filename, kd_2d_f64_box bounds);
extern int kd_2d_f64_get_mmap_bounds(const kd_2d_f64_mmap_node *nodes, size_t node_count, kd_2d_f64_box bounds);
extern int kd_2d_f64_nearest(kd_tree tree, double x, double y, int m, kd_priority_2d_f64 **alist);
extern void kd_2d_f64_print_nearest(kd_tree tree, double x, double y, int m);

/* 3D 32-bit API */
#pragma pack(push, 1)
typedef struct {
    uint64_t source_id;
    int32_t size[6];
    int32_t lo_min_bound;
    int32_t hi_max_bound;
    int32_t other_bound;
    int64_t left_child;
    int64_t right_child;
} kd_3d_32_mmap_node;
#pragma pack(pop)

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
extern int kd_3d_32_serialize(kd_tree tree, const char *filename);
extern int kd_3d_32_get_bounds(kd_tree tree, kd_3d_32_box bounds);
extern int kd_3d_32_get_serialized_bounds(const char *filename, kd_3d_32_box bounds);
extern int kd_3d_32_get_mmap_bounds(const kd_3d_32_mmap_node *nodes, size_t node_count, kd_3d_32_box bounds);
extern int kd_3d_32_nearest(kd_tree tree, int32_t x, int32_t y, int32_t z, int m, kd_priority_3d_32 **alist);
extern void kd_3d_32_print_nearest(kd_tree tree, int32_t x, int32_t y, int32_t z, int m);

/* 3D 64-bit API */
#pragma pack(push, 1)
typedef struct {
    uint64_t source_id;
    int64_t size[6];
    int64_t lo_min_bound;
    int64_t hi_max_bound;
    int64_t other_bound;
    int64_t left_child;
    int64_t right_child;
} kd_3d_64_mmap_node;
#pragma pack(pop)

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
extern int kd_3d_64_serialize(kd_tree tree, const char *filename);
extern int kd_3d_64_get_bounds(kd_tree tree, kd_3d_64_box bounds);
extern int kd_3d_64_get_serialized_bounds(const char *filename, kd_3d_64_box bounds);
extern int kd_3d_64_get_mmap_bounds(const kd_3d_64_mmap_node *nodes, size_t node_count, kd_3d_64_box bounds);
extern int kd_3d_64_nearest(kd_tree tree, int64_t x, int64_t y, int64_t z, int m, kd_priority_3d_64 **alist);
extern void kd_3d_64_print_nearest(kd_tree tree, int64_t x, int64_t y, int64_t z, int m);

/* 3D 128-bit API */
#pragma pack(push, 1)
typedef struct {
    uint64_t source_id;
    __int128 size[6];
    __int128 lo_min_bound;
    __int128 hi_max_bound;
    __int128 other_bound;
    int64_t left_child;
    int64_t right_child;
} kd_3d_128_mmap_node;
#pragma pack(pop)

typedef __int128 kd_3d_128_box[6];
typedef struct kd_priority_3d_128 { double dist; void *elem; } kd_priority_3d_128;

extern kd_tree kd_3d_128_create(void);
extern kd_tree kd_3d_128_build(int (*itemfunc)(kd_generic arg, kd_generic *val, kd_3d_128_box size), kd_generic );
extern void kd_3d_128_destroy(kd_tree this_one, void (*delfunc)(kd_generic item));
extern kd_status kd_3d_128_is_member(kd_tree , kd_generic , kd_3d_128_box );
extern void kd_3d_128_insert(kd_tree , kd_generic , kd_3d_128_box, kd_generic );
extern kd_status kd_3d_128_delete(kd_tree , kd_generic , kd_3d_128_box );
extern kd_status kd_3d_128_really_delete(kd_tree theTree, kd_generic data, kd_3d_128_box old_size, int *num_tries, int *num_del);
extern kd_gen kd_3d_128_start(kd_tree tree, kd_3d_128_box size);
extern kd_status kd_3d_128_next(kd_gen , kd_generic *, kd_3d_128_box);
extern int kd_3d_128_finish(kd_gen);
extern int kd_3d_128_count(kd_tree tree);
extern void kd_3d_128_print(kd_tree);
extern void kd_3d_128_badness(kd_tree);
extern kd_tree kd_3d_128_rebuild(kd_tree);
extern int kd_3d_128_serialize(kd_tree tree, const char *filename);
extern int kd_3d_128_get_bounds(kd_tree tree, kd_3d_128_box bounds);
extern int kd_3d_128_get_serialized_bounds(const char *filename, kd_3d_128_box bounds);
extern int kd_3d_128_get_mmap_bounds(const kd_3d_128_mmap_node *nodes, size_t node_count, kd_3d_128_box bounds);
extern int kd_3d_128_nearest(kd_tree tree, __int128 x, __int128 y, __int128 z, int m, kd_priority_3d_128 **alist);
extern void kd_3d_128_print_nearest(kd_tree tree, __int128 x, __int128 y, __int128 z, int m);

/* 3D float64 API */
#pragma pack(push, 1)
typedef struct {
    uint64_t source_id;
    double size[6];
    double lo_min_bound;
    double hi_max_bound;
    double other_bound;
    int64_t left_child;
    int64_t right_child;
} kd_3d_f64_mmap_node;
#pragma pack(pop)

typedef double kd_3d_f64_box[6];
typedef struct kd_priority_3d_f64 { double dist; void *elem; } kd_priority_3d_f64;

extern kd_tree kd_3d_f64_create(void);
extern kd_tree kd_3d_f64_build(int (*itemfunc)(kd_generic arg, kd_generic *val, kd_3d_f64_box size), kd_generic );
extern void kd_3d_f64_destroy(kd_tree this_one, void (*delfunc)(kd_generic item));
extern kd_status kd_3d_f64_is_member(kd_tree , kd_generic , kd_3d_f64_box );
extern void kd_3d_f64_insert(kd_tree , kd_generic , kd_3d_f64_box, kd_generic );
extern kd_status kd_3d_f64_delete(kd_tree , kd_generic , kd_3d_f64_box );
extern kd_status kd_3d_f64_really_delete(kd_tree theTree, kd_generic data, kd_3d_f64_box old_size, int *num_tries, int *num_del);
extern kd_gen kd_3d_f64_start(kd_tree tree, kd_3d_f64_box size);
extern kd_status kd_3d_f64_next(kd_gen , kd_generic *, kd_3d_f64_box);
extern int kd_3d_f64_finish(kd_gen);
extern int kd_3d_f64_count(kd_tree tree);
extern void kd_3d_f64_print(kd_tree);
extern void kd_3d_f64_badness(kd_tree);
extern kd_tree kd_3d_f64_rebuild(kd_tree);
extern int kd_3d_f64_serialize(kd_tree tree, const char *filename);
extern int kd_3d_f64_get_bounds(kd_tree tree, kd_3d_f64_box bounds);
extern int kd_3d_f64_get_serialized_bounds(const char *filename, kd_3d_f64_box bounds);
extern int kd_3d_f64_get_mmap_bounds(const kd_3d_f64_mmap_node *nodes, size_t node_count, kd_3d_f64_box bounds);
extern int kd_3d_f64_nearest(kd_tree tree, double x, double y, double z, int m, kd_priority_3d_f64 **alist);
extern void kd_3d_f64_print_nearest(kd_tree tree, double x, double y, double z, int m);

#ifdef __cplusplus
}
#endif

#endif /* KDTREE_H */
