#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <fitsio.h>
#include "kdtree.h"

// 1e9 scale factor fits within 64-bit integer range up to ~9.2 billion parsecs,
// which is well beyond the ~14 billion parsec diameter of the observable universe.
#define SCALE_FACTOR 1000000000.0 

typedef struct {
    int64_t source_id;
    int64_t x, y, z;
} StarCoord;

int item_func(kd_generic arg, kd_generic *val, kd_3d_64_box size) {
    StarCoord **stars = (StarCoord **)arg;
    StarCoord *star = *stars;
    
    if (star->source_id == 0) { // sentinel
        return 0;
    }
    

    
    *val = (kd_generic)(intptr_t)star->source_id; // Using item as ID
    size[0] = star->x;
    size[1] = star->y;
    size[2] = star->z;
    size[3] = star->x;
    size[4] = star->y;
    size[5] = star->z;
    
    (*stars)++;
    return KD_OK;
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
        return 1;
    }
    
    int64_t *ids = malloc(num_rows * sizeof(int64_t));
    double *ras = malloc(num_rows * sizeof(double));
    double *decs = malloc(num_rows * sizeof(double));
    double *plxs = malloc(num_rows * sizeof(double));
    
    int anynul;
    fits_read_col(fptr, TLONGLONG, id_col, 1, 1, num_rows, NULL, ids, &anynul, &status);
    fits_read_col(fptr, TDOUBLE, ra_col, 1, 1, num_rows, NULL, ras, &anynul, &status);
    fits_read_col(fptr, TDOUBLE, dec_col, 1, 1, num_rows, NULL, decs, &anynul, &status);
    fits_read_col(fptr, TDOUBLE, parallax_col, 1, 1, num_rows, NULL, plxs, &anynul, &status);
    
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
        
        stars[valid_count].source_id = ids[i];
        stars[valid_count].x = (int64_t)(x * SCALE_FACTOR);
        stars[valid_count].y = (int64_t)(y * SCALE_FACTOR);
        stars[valid_count].z = (int64_t)(z * SCALE_FACTOR);
        valid_count++;
    }
    
    stars[valid_count].source_id = 0; // sentinel
    
    printf("Loaded %ld stars with valid parallaxes.\n", valid_count);
    printf("Building KD-Tree...\n");
    StarCoord *ptr = stars;
    kd_tree tree = kd_3d_64_build(item_func, (kd_generic)&ptr);
    
    printf("Serializing KD-Tree to %s...\n", argv[2]);
    kd_3d_64_serialize(tree, argv[2]);
    
    free(ids); free(ras); free(decs); free(plxs); free(stars);
    printf("Done!\n");
    return 0;
}
