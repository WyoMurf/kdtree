#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <assert.h>
#include "kd.h"

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
    
    const char *filename = "test_serialize.kdtree";
    int ret = kd_serialize(tree, filename);
    assert(ret == 0);
    
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
