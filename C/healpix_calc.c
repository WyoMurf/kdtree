#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// Interleave the bits of two 32-bit integers (Z-order curve / Morton code)
uint64_t interleave_bits(uint32_t x, uint32_t y) {
    uint64_t res = 0;
    for (int i = 0; i < 32; i++) {
        res |= (((uint64_t)(x & (1U << i))) << i) | (((uint64_t)(y & (1U << i))) << (i + 1));
    }
    return res;
}

// Convert RA, Dec (in degrees) to HEALPix Level k NESTED index
uint64_t ra_dec_to_healpix(double ra, double dec, int level) {
    double phi = ra * (M_PI / 180.0);
    double z = sin(dec * (M_PI / 180.0));
    
    uint64_t nside = 1ULL << level;
    uint64_t face_pixels = nside * nside;
    
    double xc, yc;
    if (fabs(z) <= 2.0 / 3.0) {
        xc = phi;
        yc = 1.5 * z;
    } else {
        // Polar caps
        double sgn = (z >= 0.0) ? 1.0 : -1.0;
        double sigma = sqrt(3.0 * (1.0 - fabs(z)));
        yc = sgn * (2.0 - sigma);
        
        // Find which of the 4 polar facets we are in
        int facet = (int)(phi / (M_PI / 2.0));
        if (facet < 0) facet = 0;
        if (facet > 3) facet = 3;
        double phi_c = (facet + 0.5) * (M_PI / 2.0);
        xc = phi_c + (phi - phi_c) * sigma;
    }
    
    // Project to oblique grid coordinates (scaled by pi/2)
    double pa = xc / (M_PI / 2.0);
    double pb = yc / (M_PI / 2.0);
    
    double u = pa + pb / 2.0;
    double v = pa - pb / 2.0;
    
    double ku = floor(u);
    double kv = floor(v);
    
    double u_frac = u - ku;
    double v_frac = v - kv;
    
    // Translate (ku, kv) oblique grid coordinate to Base Face ID (0..11)
    int face = 0;
    int ku_i = (int)ku;
    int kv_i = (int)kv;
    
    if (ku_i >= 0 && kv_i >= 0) {
        if (ku_i < 4 && kv_i < 4) {
            face = (4 - kv_i + ku_i % 4) % 4 + 4; // Equatorial
        } else {
            face = ku_i % 4; // North cap
        }
    } else if (ku_i < 0 && kv_i < 0) {
        int ku_mod = ku_i % 4;
        if (ku_mod < 0) ku_mod += 4;
        face = 8 + ku_mod; // South cap
    }
    
    // Grid coordinates inside the face
    uint32_t i = (uint32_t)(u_frac * nside);
    uint32_t j = (uint32_t)(v_frac * nside);
    if (i >= nside) i = nside - 1;
    if (j >= nside) j = nside - 1;
    
    // Interleave bits for NESTED scheme
    uint64_t morton = interleave_bits(i, j);
    
    return face * face_pixels + morton;
}

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
    
    uint64_t index = ra_dec_to_healpix(ra, dec, level);
    printf("HEALPix Level %d NESTED Index: %llu\n", level, (unsigned long long)index);
    return 0;
}
