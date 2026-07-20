#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <assert.h>
#include "kd.h"

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

typedef struct {
    int id;
    coord_t x, y, z;
} StarCoord;

int item_func(kd_generic arg, kd_generic *val, kd_box size) {
    StarCoord **stars = (StarCoord **)arg;
    StarCoord *star = *stars;
    
    if (star->id == 0) { // sentinel
        return 0;
    }
    
    *val = (kd_generic)(intptr_t)star->id; // Using item as ID
    size[KD_LEFT] = star->x;
    size[KD_BOTTOM] = star->y;
    size[KD_FLOOR] = star->z;
    size[KD_RIGHT] = star->x;
    size[KD_TOP] = star->y;
    size[KD_CEIL] = star->z;
    
    (*stars)++;
    return KD_OK;
}

int main() {
    StarCoord stars[] = {
        {1, 10, 10, 10},
        {2, 20, 20, 20},
        {3, 5, 5, 5},
        {0, 0, 0, 0} // sentinel
    };
    
    StarCoord *ptr = stars;
    kd_tree tree = kd_build(item_func, (kd_generic)&ptr);
    assert(tree != NULL);
    
    int count = kd_count(tree);
    assert(count == 3);

    // Test kd_get_bounds
    kd_box bounds;
    int bounds_ret = kd_get_bounds(tree, bounds);
    assert(bounds_ret == KD_OK);
    assert(bounds[KD_LEFT] == 5);
    assert(bounds[KD_BOTTOM] == 5);
    assert(bounds[KD_FLOOR] == 5);
    assert(bounds[KD_RIGHT] == 20);
    assert(bounds[KD_TOP] == 20);
    assert(bounds[KD_CEIL] == 20);
    printf("kd_get_bounds test passed.\n");
    
    const char *filename = "test_serialize.kdtree";
    int ret = kd_serialize(tree, filename);
    assert(ret == 0);

    // Test kd_get_serialized_bounds
    kd_box ser_bounds;
    int ser_ret = kd_get_serialized_bounds(filename, ser_bounds);
    assert(ser_ret == KD_OK);
    assert(ser_bounds[KD_LEFT] == 5);
    assert(ser_bounds[KD_BOTTOM] == 5);
    assert(ser_bounds[KD_FLOOR] == 5);
    assert(ser_bounds[KD_RIGHT] == 20);
    assert(ser_bounds[KD_TOP] == 20);
    assert(ser_bounds[KD_CEIL] == 20);
    printf("kd_get_serialized_bounds test passed.\n");
    
    kd_destroy(tree, NULL);
    
    // Now verify the file
    int fd = open(filename, O_RDONLY);
    assert(fd != -1);
    
    struct stat sb;
    fstat(fd, &sb);
    assert(sb.st_size == 3 * sizeof(kd_mmap_node));
    
    kd_mmap_node *array = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    assert(array != MAP_FAILED);
    
    // Check IDs
    int found[4] = {0};
    for(int i=0; i<3; i++) {
        uint64_t id = array[i].source_id;
        assert(id >= 1 && id <= 3);
        found[id] = 1;
    }
    assert(found[1] == 1 && found[2] == 1 && found[3] == 1);
    
    munmap(array, sb.st_size);
    close(fd);
    unlink(filename);
    
    printf("Serialize test passed.\n");
    return 0;
}
