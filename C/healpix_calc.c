#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#include "geo_utils.h"

/*
 * Thin CLI wrapper around geo_utils.h's healpix_nested_index() -- the
 * actual RA/Dec-to-HEALPix conversion (and its interleave_bits() Morton-code
 * helper) used to live here directly, duplicated again in geonames2kd.c for
 * the terrestrial (lon/lat) case. Promoted to the shared library so both
 * (and any future caller) share one implementation.
 */
int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s <ra_deg> <dec_deg> <level>\n", argv[0]);
        printf("Example: %s 217.4290 -62.6795 12\n", argv[0]);
        return 1;
    }
    
    double ra = atof(argv[1]);
    double dec = atof(argv[2]);
    int level = atoi(argv[3]);
    
    if (level < 0 || level > 29) {
        fprintf(stderr, "Error: HEALPix level must be between 0 and 29\n");
        return 1;
    }
    
    uint64_t index = healpix_nested_index(ra, dec, level);
    printf("HEALPix Level %d NESTED Index: %llu\n", level, (unsigned long long)index);
    return 0;
}
