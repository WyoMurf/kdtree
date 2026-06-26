#define _POSIX_C_SOURCE 199309L
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "kd.h"

double get_time() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main() {
    kd_tree tree = kd_create();
    srand(42);

    coord_t (*boxes)[6] = malloc(1000000 * sizeof(*boxes));

    printf("Inserting 1000000 boxes...\n");
    double start = get_time();
    for (int i = 0; i < 1000000; i++) {
        coord_t x1 = ((unsigned int)rand() % 1000) * 1000000 + (rand() % 1000000);
        coord_t y1 = ((unsigned int)rand() % 1000) * 1000000 + (rand() % 1000000);
        coord_t z1 = ((unsigned int)rand() % 1000) * 1000000 + (rand() % 1000000);
        x1 %= 1000000000;
        y1 %= 1000000000;
        z1 %= 1000000000;
        coord_t x2 = x1 + (rand() % 100) + 1;
        coord_t y2 = y1 + (rand() % 100) + 1;
        coord_t z2 = z1 + (rand() % 100) + 1;
        
        boxes[i][0] = x1; 
	boxes[i][1] = y1; 
	boxes[i][2] = z1;
	
	boxes[i][3] = x2; 
	boxes[i][4] = y2;
	boxes[i][5] = z2;
        kd_insert(tree, (void *)(long)(i + 1), boxes[i], NULL);
    }
    printf("Insertion took %fs\n", get_time() - start);

    kd_box search_area = {0, 0, 0, 500000000, 500000000, 500000000};
    printf("Searching in area...\n");
    start = get_time();
    kd_gen gen = kd_start(tree, search_area);
    char *data;
    kd_box found_size;
    int found_count = 0;
    
    while (kd_next(gen, &data, found_size) == KD_OK) {
        found_count++;
    }
    kd_finish(gen);
    printf("Found %d boxes in search area. Search took %fs\n", found_count, get_time() - start);

    int tries, deleted;
    printf("Deleting 1000 items...\n");
    start = get_time();
    for (int i = 0; i < 1000; i++) {
        kd_really_delete(tree, (void *)(long)(i + 1), boxes[i], &tries, &deleted);
    }
    printf("Deletion took %fs\n", get_time() - start);

    free(boxes);
    return 0;
}
