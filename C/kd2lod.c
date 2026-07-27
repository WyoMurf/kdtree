#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include "kdtree.h"
#include "kd2lod.h"

// Sidecar annotator for .kdtree files: for every node it adds the exact
// bounding box and star count of the subtree rooted there, so a viewer can
// cull whole subtrees against the view frustum and collapse distant ones
// to a single representative point instead of walking every leaf.
//
// kd_serialize() (3d/kd.c) writes nodes in pre-order, so a child's index is
// always greater than its parent's. Scanning backwards from the last node
// to the first therefore guarantees both children of a node are already
// computed by the time that node is reached - one linear pass, no recursion.

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <input.kdtree> [output.kdtree.lod]\n", argv[0]);
        return 1;
    }

    const char *in_path = argv[1];
    char out_path[1024];
    if (argc >= 3) {
        snprintf(out_path, sizeof(out_path), "%s", argv[2]);
    } else {
        snprintf(out_path, sizeof(out_path), "%s.lod", in_path);
    }

    int in_fd = open(in_path, O_RDONLY);
    if (in_fd == -1) { perror("open input"); return 1; }

    struct stat sb;
    if (fstat(in_fd, &sb) == -1 || sb.st_size == 0) {
        fprintf(stderr, "Error: empty or unreadable input file '%s'\n", in_path);
        close(in_fd);
        return 1;
    }

    if (sb.st_size % sizeof(kd_3d_64_mmap_node) != 0) {
        fprintf(stderr, "Error: '%s' is not a 3D 64-bit kdtree file (size not a multiple of %zu)\n",
                in_path, sizeof(kd_3d_64_mmap_node));
        close(in_fd);
        return 1;
    }

    size_t total_slots = sb.st_size / sizeof(kd_3d_64_mmap_node);
    kd_3d_64_mmap_node *nodes = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, in_fd, 0);
    if (nodes == MAP_FAILED) { perror("mmap input"); close(in_fd); return 1; }

    // The last slot may be the O(1) whole-tree bounds sentinel appended by
    // kd_serialize(); it is never referenced by any left_child/right_child
    // and is excluded from the annotated node range.
    size_t real_count = total_slots;
    if (nodes[total_slots - 1].source_id == UINT64_MAX) {
        real_count = total_slots - 1;
    }

    if (real_count == 0) {
        fprintf(stderr, "Error: no real tree nodes in '%s'\n", in_path);
        munmap(nodes, sb.st_size);
        close(in_fd);
        return 1;
    }

    printf("Annotating %zu nodes from %s...\n", real_count, in_path);

    int out_fd = open(out_path, O_RDWR | O_CREAT | O_TRUNC, 0666);
    if (out_fd == -1) {
        perror("open output");
        munmap(nodes, sb.st_size);
        close(in_fd);
        return 1;
    }

    size_t out_size = sizeof(kd2lod_header) + real_count * sizeof(kd2lod_record);
    if (ftruncate(out_fd, out_size) == -1) {
        perror("ftruncate");
        munmap(nodes, sb.st_size);
        close(in_fd);
        close(out_fd);
        return 1;
    }

    uint8_t *out_map = mmap(NULL, out_size, PROT_READ | PROT_WRITE, MAP_SHARED, out_fd, 0);
    if (out_map == MAP_FAILED) {
        perror("mmap output");
        munmap(nodes, sb.st_size);
        close(in_fd);
        close(out_fd);
        return 1;
    }

    kd2lod_header *hdr = (kd2lod_header *)out_map;
    hdr->magic = KD2LOD_MAGIC;
    hdr->version = KD2LOD_VERSION;
    hdr->source_size = (uint64_t)sb.st_size;
    hdr->node_count = (uint64_t)real_count;

    kd2lod_record *records = (kd2lod_record *)(out_map + sizeof(kd2lod_header));

    for (int64_t i = (int64_t)real_count - 1; i >= 0; i--) {
        kd_3d_64_mmap_node *n = &nodes[i];
        kd2lod_record *r = &records[i];

        for (int d = 0; d < 3; d++) {
            r->min[d] = n->size[d];
            r->max[d] = n->size[d + 3];
        }
        r->count = 1;

        int64_t L = n->left_child;
        if (L >= 0) {
            kd2lod_record *lr = &records[L];
            for (int d = 0; d < 3; d++) {
                if (lr->min[d] < r->min[d]) r->min[d] = lr->min[d];
                if (lr->max[d] > r->max[d]) r->max[d] = lr->max[d];
            }
            r->count += lr->count;
        }

        int64_t R = n->right_child;
        if (R >= 0) {
            kd2lod_record *rr = &records[R];
            for (int d = 0; d < 3; d++) {
                if (rr->min[d] < r->min[d]) r->min[d] = rr->min[d];
                if (rr->max[d] > r->max[d]) r->max[d] = rr->max[d];
            }
            r->count += rr->count;
        }
    }

    kd2lod_record *root = &records[0];
    printf("Root subtree: %u stars, bounds [%ld,%ld,%ld] to [%ld,%ld,%ld] (x1e-9 pc)\n",
           root->count,
           (long)root->min[0], (long)root->min[1], (long)root->min[2],
           (long)root->max[0], (long)root->max[1], (long)root->max[2]);

    msync(out_map, out_size, MS_SYNC);
    munmap(out_map, out_size);
    close(out_fd);
    munmap(nodes, sb.st_size);
    close(in_fd);

    printf("Wrote %s (%zu records, %.2f MB)\n", out_path, real_count, out_size / (1024.0 * 1024.0));
    return 0;
}
