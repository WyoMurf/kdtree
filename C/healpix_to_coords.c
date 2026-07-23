#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// De-interleave the bits of a 64-bit integer to extract x and y coordinates
// (Inverse Z-order Morton curve)
static void deinterleave_bits(uint64_t ip, uint64_t *ix, uint64_t *iy) {
    *ix = 0;
    *iy = 0;
    for (int i = 0; i < 32; i++) {
        *ix |= ((ip >> (2 * i)) & 1) << i;
        *iy |= ((ip >> (2 * i + 1)) & 1) << i;
    }
}

// Convert a NESTED index to a RING index (0-based)
int64_t nest2ring(int64_t nside, int64_t ipnest) {
    int64_t npix = 12 * nside * nside;
    if (ipnest < 0 || ipnest >= npix) return -1;

    int64_t nside2 = nside * nside;
    int64_t face_num = ipnest / nside2;
    int64_t ipf = ipnest % nside2;

    uint64_t ix, iy;
    deinterleave_bits((uint64_t)ipf, &ix, &iy);

    // Face-dependent offsets for ring calculation
    static const int jrll[] = {2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4};
    static const int jpll[] = {1, 3, 5, 7, 0, 2, 4, 6, 1, 3, 5, 7};

    int64_t jr = (int64_t)jrll[face_num] * nside - (int64_t)ix - (int64_t)iy - 1;
    int64_t jp = (int64_t)jpll[face_num] * nside + (int64_t)ix - (int64_t)iy;

    int64_t ipring;
    if (jr < nside) { // North polar cap
        ipring = 2 * jr * (jr + 1) + (jp - 1) % (4 * jr) + 1;
    } else if (jr <= 3 * nside) { // Equatorial belt
        int64_t nr = jr - nside;
        int64_t kshift = (nr % 2 == 0) ? 1 : 0;
        ipring = 2 * nside * (nside - 1) + 4 * nside * nr + (jp - kshift) / 2 + 1;
    } else { // South polar cap
        int64_t jr_inv = 4 * nside - jr;
        ipring = npix - 2 * jr_inv * (jr_inv + 1) + (jp - 1) % (4 * jr_inv) + 1;
    }

    return ipring - 1;
}

// Convert a RING index (0-based) into continuous celestial coordinates RA and Dec (degrees)
void ring2coords(int64_t nside, int64_t ipring, double *ra, double *dec) {
    int64_t npix = 12 * nside * nside;
    int64_t ncap = 2 * nside * (nside - 1);
    
    double z = 0.0;
    double phi = 0.0;
    
    // We shift the 0-based index to 1-based to align with classic mathematical equations
    int64_t p = ipring + 1;

    if (p <= ncap) { // North Polar Cap
        // Calculate ring index (i) and pixel index inside ring (j)
        double i_double = floor(sqrt(((double)p - 0.5) / 2.0)) + 1.0;
        int64_t i = (int64_t)i_double;
        int64_t j = p - 2 * i * (i - 1);
        
        z = 1.0 - (double)(i * i) / (3.0 * nside * nside);
        phi = (M_PI / (2.0 * i)) * ((double)j - 0.5);
        
    } else if (p < npix - ncap + 1) { // Equatorial Belt
        int64_t p_eq = p - ncap - 1;
        int64_t i = p_eq / (4 * nside) + nside;
        int64_t j = p_eq % (4 * nside) + 1;
        
        z = (4.0 / 3.0) - (2.0 * i) / (3.0 * nside);
        
        double shift = ((i - nside + 1) % 2 == 0) ? 1.0 : 0.5;
        phi = (M_PI / (2.0 * nside)) * ((double)j - shift);
        
    } else { // South Polar Cap
        int64_t p_inv = npix - p + 1;
        double i_double = floor(sqrt(((double)p_inv - 0.5) / 2.0)) + 1.0;
        int64_t i_inv = (int64_t)i_double;
        int64_t i = 4 * nside - i_inv;
        int64_t j = p_inv - 2 * i_inv * (i_inv - 1);
        
        z = -1.0 + (double)(i_inv * i_inv) / (3.0 * nside * nside);
        phi = (M_PI / (2.0 * i_inv)) * ((double)j - 0.5);
    }
    
    // Convert to RA and Dec (degrees)
    *ra = phi * (180.0 / M_PI);
    *dec = asin(z) * (180.0 / M_PI);
    
    // Normalize RA to [0, 360)
    while (*ra < 0.0) *ra += 360.0;
    while (*ra >= 360.0) *ra -= 360.0;
}

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Usage: %s <index> <level> <type_0_for_nested_1_for_ring>\n", argv[0]);
        printf("Example: %s 134053741 12 0\n", argv[0]);
        return 1;
    }
    
    int64_t index = strtoll(argv[1], NULL, 10);
    int level = atoi(argv[2]);
    int is_ring = atoi(argv[3]);
    
    if (level < 0 || level > 29) {
        fprintf(stderr, "Error: HEALPix level must be between 0 and 29\n");
        return 1;
    }
    
    int64_t nside = 1LL << level;
    int64_t ring_idx = index;
    
    if (!is_ring) {
        ring_idx = nest2ring(nside, index);
        if (ring_idx == -1) {
            fprintf(stderr, "Error: Invalid NESTED index %lld for level %d\n", (long long)index, level);
            return 1;
        }
    }
    
    double ra, dec;
    ring2coords(nside, ring_idx, &ra, &dec);
    
    printf("HEALPix Level %d %s Index: %lld\n", level, is_ring ? "RING" : "NESTED", (long long)index);
    printf("Decoded Coordinates:\n");
    printf("  RA:  %12.6f°\n", ra);
    printf("  Dec: %12.6f°\n", dec);
    return 0;
}
