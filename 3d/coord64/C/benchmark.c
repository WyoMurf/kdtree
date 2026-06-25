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

    long long (*boxes)[6] = malloc(1000000 * sizeof(*boxes));

    printf("Inserting 1000000 boxes...\n");
    double start = get_time();
    for (int i = 0; i < 1000000; i++) {
        long long x1 = ((long long)rand() * rand()) % 10000000000LL;
        long long y1 = ((long long)rand() * rand()) % 10000000000LL;
        long long z1 = ((long long)rand() * rand()) % 10000000000LL;
        long long x2 = x1 + (rand() % 100) + 1;
        long long y2 = y1 + (rand() % 100) + 1;
        long long z2 = z1 + (rand() % 100) + 1;
        
        boxes[i][0] = x1; 
	boxes[i][1] = y1; 
	boxes[i][2] = z1;
	
	boxes[i][3] = x2; 
	boxes[i][4] = y2;
	boxes[i][5] = z2;
        kd_insert(tree, (void *)(long)(i + 1), boxes[i], NULL);
    }
    printf("Insertion took %fs\n", get_time() - start);

    long long search_area[6] = {0, 0, 0, 5000000000LL, 5000000000LL, 5000000000LL};
    printf("Searching in area...\n");
    start = get_time();
    kd_gen gen = kd_start(tree, search_area);
    char *data;
    long long found_size[6];
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
