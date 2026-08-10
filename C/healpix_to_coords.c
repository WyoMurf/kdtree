#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "geo_utils.h"

/*
 * Thin CLI wrapper around geo_utils.h's healpix_nested_index_to_coords()/
 * healpix_ring_index_to_coords() -- the actual index-to-coordinates
 * conversion (nest-to-ring remapping, de-interleaving, and the ring2coords
 * projection math) used to live here directly. Promoted to the shared
 * library alongside its forward counterpart, healpix_nested_index()
 * (originally in healpix_calc.c/geonames2kd.c), for one implementation
 * either direction.
 */
int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s <index> <level> <type_0_for_nested_1_for_ring>\n", argv[0]);
        printf("Example: %s 134053741 12 0\n", argv[0]);
        return 1;
    }

    uint64_t index = strtoull(argv[1], NULL, 10);
    int level = atoi(argv[2]);
    int is_ring = atoi(argv[3]);

    if (level < 0 || level > 29) {
        fprintf(stderr, "Error: HEALPix level must be between 0 and 29\n");
        return 1;
    }

    double ra, dec;
    int status = is_ring
        ? healpix_ring_index_to_coords(level, index, &ra, &dec)
        : healpix_nested_index_to_coords(level, index, &ra, &dec);
    if (status != 0) {
        fprintf(stderr, "Error: Invalid %s index %llu for level %d\n",
                is_ring ? "RING" : "NESTED", (unsigned long long)index, level);
        return 1;
    }

    printf("HEALPix Level %d %s Index: %llu\n", level, is_ring ? "RING" : "NESTED", (unsigned long long)index);
    printf("Decoded Coordinates:\n");
    printf("  RA:  %12.6f°\n", ra);
    printf("  Dec: %12.6f°\n", dec);
    return 0;
}
