#ifndef KD2LOD_H
#define KD2LOD_H

#include <stdint.h>

// Sidecar LOD-annotation format for .kdtree files (produced by kd2lod.c).
// Record i corresponds exactly to node i in the source .kdtree file - both
// use the same pre-order indexing that kd_serialize() writes, so a viewer
// can mmap both files and use left_child/right_child from the .kdtree file
// to walk this array directly.

#define KD2LOD_MAGIC   0x32444F4Cu /* "LOD2" little-endian */
#define KD2LOD_VERSION 1u

#pragma pack(push, 1)
typedef struct {
    uint32_t magic;
    uint32_t version;
    uint64_t source_size; // st_size of the source .kdtree file at annotation time
    uint64_t node_count;  // number of records that follow (excludes the bounds sentinel)
} kd2lod_header;

typedef struct {
    int64_t min[3];
    int64_t max[3];
    uint32_t count;
    uint32_t _pad;
} kd2lod_record;
#pragma pack(pop)

#endif
