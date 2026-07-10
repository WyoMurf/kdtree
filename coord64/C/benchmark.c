#include <stdio.h>
#include <stdlib.h>
#include "kd.h"

int main() {
    kd_tree tree = kd_create();
    srand(42);

    long long (*boxes)[4] = malloc(1000000 * sizeof(*boxes));

    for (int i = 0; i < 1000000; i++) {
        long long x1 = ((long long)rand() * rand()) % 10000000000LL;
        long long y1 = ((long long)rand() * rand()) % 10000000000LL;
        long long x2 = x1 + (rand() % 100) + 1;
        long long y2 = y1 + (rand() % 100) + 1;
        
        boxes[i][0] = x1; boxes[i][1] = y1; boxes[i][2] = x2; boxes[i][3] = y2;
        kd_insert(tree, (void *)(long)(i + 1), boxes[i], NULL);
    }

    long long search_area[4] = {0, 0, 5000000000LL, 5000000000LL};
    kd_gen gen = kd_start(tree, search_area);
    char *data;
    long long found_size[4];
    int found_count = 0;
    
    while (kd_next(gen, &data, found_size) == KD_OK) {
        found_count++;
    }
    kd_finish(gen);
    printf("Found %d boxes in search area.\n", found_count);

    int tries, deleted;
    for (int i = 0; i < 1000; i++) {
        kd_really_delete(tree, (void *)(long)(i + 1), boxes[i], &tries, &deleted);
    }

    free(boxes);
    return 0;
}
