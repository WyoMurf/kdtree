#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <fitsio.h>
#include "kdtree.h"

// 1e9 scale factor fits within 64-bit integer range up to ~9.2 billion parsecs,
// which is well beyond the ~14 billion parsec diameter of the observable universe.
#define SCALE_FACTOR 1000000000.0 

typedef struct {
    int64_t source_id;
    int64_t x, y, z;
    int64_t radius; // Physical stellar radius in scaled coordinate space (1 Rsun = 22.56 units)
    double parallax; // Kept to segment stars by parallax ranges
} StarCoord;

int item_func(kd_generic arg, kd_generic *val, kd_3d_64_box size) {
    StarCoord **stars = (StarCoord **)arg;
    StarCoord *star = *stars;
    
    if (star->source_id == 0) { // sentinel
        return 0;
    }
    
    *val = (kd_generic)(intptr_t)star->source_id; // Using item as ID
    
    // Store actual 3D bounding box of the star: [x - R, y - R, z - R, x + R, y + R, z + R]
    size[0] = star->x - star->radius;
    size[1] = star->y - star->radius;
    size[2] = star->z - star->radius;
    size[3] = star->x + star->radius;
    size[4] = star->y + star->radius;
    size[5] = star->z + star->radius;
    
    (*stars)++;
    return KD_OK;
}

typedef struct {
    double min_plx;
    double max_plx;
} ParallaxRange;

#define NUM_SEGMENTS 10
const ParallaxRange PARALLAX_RANGES[NUM_SEGMENTS] = {
    {0.0, 0.5},
    {0.5, 1.0},
    {1.0, 1.5},
    {1.5, 2.0},
    {2.0, 3.0},
    {3.0, 4.0},
    {4.0, 6.0},
    {6.0, 10.0},
    {10.0, 20.0},
    {20.0, 1000000.0} // Covers any remaining high parallax
};

void write_bb_file(const char *kdtree_filename, kd_3d_64_box bounds) {
    char bb_filename[512];
    size_t len = strlen(kdtree_filename);
    if (len > 7 && strcmp(kdtree_filename + len - 7, ".kdtree") == 0) {
        memcpy(bb_filename, kdtree_filename, len - 7);
        bb_filename[len - 7] = '\0';
        strcat(bb_filename, ".bb");
    } else {
        strcpy(bb_filename, kdtree_filename);
        strcat(bb_filename, ".bb");
    }

    FILE *f = fopen(bb_filename, "w");
    if (!f) {
        fprintf(stderr, "Error: Could not open bounding box file %s for writing\n", bb_filename);
        return;
    }
    // Write space-separated bounding box values on the first line
    fprintf(f, "%lld %lld %lld %lld %lld %lld\n",
            (long long)bounds[0], (long long)bounds[1], (long long)bounds[2],
            (long long)bounds[3], (long long)bounds[4], (long long)bounds[5]);
    fclose(f);
    printf("Written bounding box file: %s\n", bb_filename);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        printf("Usage: %s <input.fits.gz> <output.kdtree>\n", argv[0]);
        return 1;
    }
    
    fitsfile *fptr;
    int status = 0;
    if (fits_open_table(&fptr, argv[1], READONLY, &status)) {
        fits_report_error(stderr, status);
        return 1;
    }
    
    long num_rows = 0;
    fits_get_num_rows(fptr, &num_rows, &status);
    
    StarCoord *stars = malloc((num_rows + 1) * sizeof(StarCoord));
    
    int id_col, ra_col, dec_col, parallax_col;
    fits_get_colnum(fptr, CASEINSEN, "source_id", &id_col, &status);
    fits_get_colnum(fptr, CASEINSEN, "ra", &ra_col, &status);
    fits_get_colnum(fptr, CASEINSEN, "dec", &dec_col, &status);
    fits_get_colnum(fptr, CASEINSEN, "parallax", &parallax_col, &status);
    
    if (status) {
        fits_report_error(stderr, status);
        free(stars);
        return 1;
    }
    
    // Check and read optional G, BP, and RP photometric magnitude columns
    int g_col, bp_col, rp_col;
    int g_status = 0, bp_status = 0, rp_status = 0;
    fits_get_colnum(fptr, CASEINSEN, "phot_g_mean_mag", &g_col, &g_status);
    fits_get_colnum(fptr, CASEINSEN, "phot_bp_mean_mag", &bp_col, &bp_status);
    fits_get_colnum(fptr, CASEINSEN, "phot_rp_mean_mag", &rp_col, &rp_status);
    
    int64_t *ids = malloc(num_rows * sizeof(int64_t));
    double *ras = malloc(num_rows * sizeof(double));
    double *decs = malloc(num_rows * sizeof(double));
    double *plxs = malloc(num_rows * sizeof(double));
    
    double *g_mags = malloc(num_rows * sizeof(double));
    double *bp_mags = malloc(num_rows * sizeof(double));
    double *rp_mags = malloc(num_rows * sizeof(double));
    
    int anynul;
    fits_read_col(fptr, TLONGLONG, id_col, 1, 1, num_rows, NULL, ids, &anynul, &status);
    fits_read_col(fptr, TDOUBLE, ra_col, 1, 1, num_rows, NULL, ras, &anynul, &status);
    fits_read_col(fptr, TDOUBLE, dec_col, 1, 1, num_rows, NULL, decs, &anynul, &status);
    fits_read_col(fptr, TDOUBLE, parallax_col, 1, 1, num_rows, NULL, plxs, &anynul, &status);
    
    if (g_status == 0) {
        fits_read_col(fptr, TDOUBLE, g_col, 1, 1, num_rows, NULL, g_mags, &anynul, &status);
    } else {
        for(long i=0; i<num_rows; i++) g_mags[i] = NAN;
    }
    
    if (bp_status == 0) {
        fits_read_col(fptr, TDOUBLE, bp_col, 1, 1, num_rows, NULL, bp_mags, &anynul, &status);
    } else {
        for(long i=0; i<num_rows; i++) bp_mags[i] = NAN;
    }
    
    if (rp_status == 0) {
        fits_read_col(fptr, TDOUBLE, rp_col, 1, 1, num_rows, NULL, rp_mags, &anynul, &status);
    } else {
        for(long i=0; i<num_rows; i++) rp_mags[i] = NAN;
    }
    
    fits_close_file(fptr, &status);
    
    long valid_count = 0;
    for(long i=0; i<num_rows; i++) {
        if (plxs[i] <= 0 || isnan(plxs[i])) continue; // Cannot compute realistic distance if p <= 0
        
        double d = 1000.0 / plxs[i]; // Distance in parsecs
        double ra_rad = ras[i] * (M_PI / 180.0);
        double dec_rad = decs[i] * (M_PI / 180.0);
        
        double x = d * cos(dec_rad) * cos(ra_rad);
        double y = d * cos(dec_rad) * sin(ra_rad);
        double z = d * sin(dec_rad);
        
        // Calculate physical radius of the star (R in terms of Solar Radii R_sun)
        double r_solar = 1.0; // Default fallback to 1 Solar Radius
        
        if (!isnan(g_mags[i]) && !isnan(bp_mags[i]) && !isnan(rp_mags[i])) {
            double abs_g = g_mags[i] - 5.0 * log10(d) + 5.0;
            double lum = pow(10.0, (4.67 - abs_g) / 2.5);
            double color = bp_mags[i] - rp_mags[i];
            double teff = pow(10.0, 3.979 - 0.20 * color);
            
            if (teff >= 2000.0 && teff <= 50000.0 && lum > 0.0) {
                double calc_r = sqrt(lum) * pow(5778.0 / teff, 2.0);
                if (calc_r >= 0.01 && calc_r <= 1000.0) {
                    r_solar = calc_r;
                }
            }
        }
        
        // Convert solar radius to scaled coordinate space:
        // R_scaled = R * 22.56 (since 1 Rsun = 22.56 units in our 1e9 parsecs grid)
        int64_t r_scaled = (int64_t)(r_solar * 22.56);
        if (r_scaled < 1) r_scaled = 1; // At least 1 unit bounding box
        
        stars[valid_count].source_id = ids[i];
        stars[valid_count].x = (int64_t)(x * SCALE_FACTOR);
        stars[valid_count].y = (int64_t)(y * SCALE_FACTOR);
        stars[valid_count].z = (int64_t)(z * SCALE_FACTOR);
        stars[valid_count].radius = r_scaled;
        stars[valid_count].parallax = plxs[i];
        valid_count++;
    }
    
    stars[valid_count].source_id = 0; // sentinel
    
    printf("Loaded %ld stars with valid parallaxes.\n", valid_count);
    
    // --- PART 1: Build and Serialize Original Full Tree ---
    printf("Building Full KD-Tree...\n");
    StarCoord *ptr = stars;
    kd_tree tree = kd_3d_64_build(item_func, (kd_generic)&ptr);
    
    printf("Serializing Full KD-Tree to %s...\n", argv[2]);
    kd_3d_64_serialize(tree, argv[2]);
    
    // Write full tree bounding box
    kd_3d_64_box full_bounds;
    if (kd_3d_64_get_bounds(tree, full_bounds) == KD_OK) {
        write_bb_file(argv[2], full_bounds);
    } else {
        printf("Warning: Could not compute bounding box for full tree.\n");
    }
    kd_3d_64_destroy(tree, NULL);
    
    // --- PART 2: Build and Serialize 10 Subtrees by Parallax ---
    printf("Segmenting stars into %d parallax-ranged subtrees...\n", NUM_SEGMENTS);
    for (int s = 0; s < NUM_SEGMENTS; s++) {
        long seg_count = 0;
        for (long i = 0; i < valid_count; i++) {
            if (stars[i].parallax >= PARALLAX_RANGES[s].min_plx && stars[i].parallax < PARALLAX_RANGES[s].max_plx) {
                seg_count++;
            }
        }
        
        if (seg_count > 0) {
            StarCoord *seg_stars = malloc((seg_count + 1) * sizeof(StarCoord));
            long idx = 0;
            for (long i = 0; i < valid_count; i++) {
                if (stars[i].parallax >= PARALLAX_RANGES[s].min_plx && stars[i].parallax < PARALLAX_RANGES[s].max_plx) {
                    seg_stars[idx] = stars[i];
                    idx++;
                }
            }
            seg_stars[idx].source_id = 0; // sentinel
            
            char subtree_filename[512];
            size_t len = strlen(argv[2]);
            if (len > 7 && strcmp(argv[2] + len - 7, ".kdtree") == 0) {
                memcpy(subtree_filename, argv[2], len - 7);
                subtree_filename[len - 7] = '\0';
                sprintf(subtree_filename + len - 7, "-%d.kdtree", s);
            } else {
                sprintf(subtree_filename, "%s-%d.kdtree", argv[2], s);
            }
            
            printf("Building subtree segment %d with %ld stars (%g to %g mas)...\n", 
                   s, seg_count, PARALLAX_RANGES[s].min_plx, PARALLAX_RANGES[s].max_plx);
            
            StarCoord *seg_ptr = seg_stars;
            kd_tree seg_tree = kd_3d_64_build(item_func, (kd_generic)&seg_ptr);
            
            printf("Serializing subtree to %s...\n", subtree_filename);
            kd_3d_64_serialize(seg_tree, subtree_filename);
            
            kd_3d_64_box seg_bounds;
            if (kd_3d_64_get_bounds(seg_tree, seg_bounds) == KD_OK) {
                write_bb_file(subtree_filename, seg_bounds);
            } else {
                printf("Warning: Could not compute bounding box for subtree %d.\n", s);
            }
            
            kd_3d_64_destroy(seg_tree, NULL);
            free(seg_stars);
        } else {
            printf("Subtree segment %d has 0 stars (%g to %g mas), skipping.\n", 
                   s, PARALLAX_RANGES[s].min_plx, PARALLAX_RANGES[s].max_plx);
        }
    }
    
    free(ids); free(ras); free(decs); free(plxs); free(stars);
    free(g_mags); free(bp_mags); free(rp_mags);
    printf("Done!\n");
    return 0;
}
