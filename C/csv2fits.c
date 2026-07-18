#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <zlib.h>
#include <fitsio.h>

#define MAX_FIELDS 256
#define LINE_BUFFER_SIZE (256 * 1024)
#define WRITE_BUFFER_SIZE 10000

typedef struct {
    int source_id;
    int ra;
    int dec;
    int parallax;
    int parallax_over_error;
    int ruwe;
    int phot_g_mean_mag;
    int phot_bp_mean_mag;
    int phot_rp_mean_mag;
    int nu_eff_used_in_astrometry;
    int pseudocolour;
    int ecl_lat;
    int astrometric_params_solved;
} GaiaColIndices;

typedef struct {
    int source_id;
    int teff_gspphot;
    int logg_gspphot;
    int mh_gspphot;
    int spectraltype_esphs;
} AstroColIndices;

typedef struct {
    long long source_id;
    double ra;
    double dec;
    double parallax;
    double parallax_over_error;
    double ruwe;
    double phot_g_mean_mag;
    double phot_bp_mean_mag;
    double phot_rp_mean_mag;
    double nu_eff;
    double pseudocolour;
    double ecl_lat;
    int astrometric_params_solved;
} GaiaRow;

typedef struct {
    long long source_id;
    double teff;
    double logg;
    double mh;
    char spectraltype[12];
} AstroRow;

double parse_double(const char *s) {
    if (!s || *s == '\0' || strcmp(s, "null") == 0 || strcmp(s, "NOT_AVAILABLE") == 0) {
        return NAN;
    }
    return strtod(s, NULL);
}

long long parse_long_long(const char *s) {
    if (!s || *s == '\0' || strcmp(s, "null") == 0 || strcmp(s, "NOT_AVAILABLE") == 0) {
        return 0;
    }
    return strtoll(s, NULL, 10);
}

int parse_csv_line(char *line, char **fields, int max_fields) {
    int count = 0;
    char *p = line;
    int in_quotes = 0;
    fields[count++] = p;
    while (*p && count < max_fields) {
        if (*p == '"') {
            in_quotes = !in_quotes;
        } else if (*p == ',' && !in_quotes) {
            *p = '\0';
            fields[count++] = p + 1;
        }
        p++;
    }
    // Remove trailing newline/carriage return characters
    if (count > 0) {
        char *last = fields[count-1];
        size_t len = strlen(last);
        while (len > 0 && (last[len-1] == '\n' || last[len-1] == '\r')) {
            last[len-1] = '\0';
            len--;
        }
    }
    return count;
}

char *gz_gets_uncommented(gzFile file, char *buf, int size) {
    while (gzgets(file, buf, size)) {
        if (buf[0] != '#') {
            return buf;
        }
    }
    return NULL;
}

const char *get_field(char **fields, int field_count, int index) {
    if (index >= 0 && index < field_count) {
        return fields[index];
    }
    return NULL;
}

void find_gaia_indices(char **fields, int field_count, GaiaColIndices *idx) {
    idx->source_id = -1;
    idx->ra = -1;
    idx->dec = -1;
    idx->parallax = -1;
    idx->parallax_over_error = -1;
    idx->ruwe = -1;
    idx->phot_g_mean_mag = -1;
    idx->phot_bp_mean_mag = -1;
    idx->phot_rp_mean_mag = -1;
    idx->nu_eff_used_in_astrometry = -1;
    idx->pseudocolour = -1;
    idx->ecl_lat = -1;
    idx->astrometric_params_solved = -1;
    
    for (int i = 0; i < field_count; i++) {
        if (strcmp(fields[i], "source_id") == 0) idx->source_id = i;
        else if (strcmp(fields[i], "ra") == 0) idx->ra = i;
        else if (strcmp(fields[i], "dec") == 0) idx->dec = i;
        else if (strcmp(fields[i], "parallax") == 0) idx->parallax = i;
        else if (strcmp(fields[i], "parallax_over_error") == 0) idx->parallax_over_error = i;
        else if (strcmp(fields[i], "ruwe") == 0) idx->ruwe = i;
        else if (strcmp(fields[i], "phot_g_mean_mag") == 0) idx->phot_g_mean_mag = i;
        else if (strcmp(fields[i], "phot_bp_mean_mag") == 0) idx->phot_bp_mean_mag = i;
        else if (strcmp(fields[i], "phot_rp_mean_mag") == 0) idx->phot_rp_mean_mag = i;
        else if (strcmp(fields[i], "nu_eff_used_in_astrometry") == 0) idx->nu_eff_used_in_astrometry = i;
        else if (strcmp(fields[i], "pseudocolour") == 0) idx->pseudocolour = i;
        else if (strcmp(fields[i], "ecl_lat") == 0) idx->ecl_lat = i;
        else if (strcmp(fields[i], "astrometric_params_solved") == 0) idx->astrometric_params_solved = i;
    }
}

void find_astro_indices(char **fields, int field_count, AstroColIndices *idx) {
    idx->source_id = -1;
    idx->teff_gspphot = -1;
    idx->logg_gspphot = -1;
    idx->mh_gspphot = -1;
    idx->spectraltype_esphs = -1;
    
    for (int i = 0; i < field_count; i++) {
        if (strcmp(fields[i], "source_id") == 0) idx->source_id = i;
        else if (strcmp(fields[i], "teff_gspphot") == 0) idx->teff_gspphot = i;
        else if (strcmp(fields[i], "logg_gspphot") == 0) idx->logg_gspphot = i;
        else if (strcmp(fields[i], "mh_gspphot") == 0) idx->mh_gspphot = i;
        else if (strcmp(fields[i], "spectraltype_esphs") == 0) idx->spectraltype_esphs = i;
    }
}

int read_headers_gaia(gzFile file, char *buf, int size, GaiaColIndices *idx) {
    char *line = gz_gets_uncommented(file, buf, size);
    if (!line) return 0;
    
    char *fields[MAX_FIELDS];
    int field_count = parse_csv_line(line, fields, MAX_FIELDS);
    find_gaia_indices(fields, field_count, idx);
    return 1;
}

int read_headers_astro(gzFile file, char *buf, int size, AstroColIndices *idx) {
    char *line = gz_gets_uncommented(file, buf, size);
    if (!line) return 0;
    
    char *fields[MAX_FIELDS];
    int field_count = parse_csv_line(line, fields, MAX_FIELDS);
    find_astro_indices(fields, field_count, idx);
    return 1;
}

int read_next_gaia(gzFile file, char *buf, int size, GaiaColIndices *idx, GaiaRow *row) {
    char *line = gz_gets_uncommented(file, buf, size);
    if (!line) return 0;
    
    char *fields[MAX_FIELDS];
    int field_count = parse_csv_line(line, fields, MAX_FIELDS);
    
    row->source_id = parse_long_long(get_field(fields, field_count, idx->source_id));
    row->ra = parse_double(get_field(fields, field_count, idx->ra));
    row->dec = parse_double(get_field(fields, field_count, idx->dec));
    row->parallax = parse_double(get_field(fields, field_count, idx->parallax));
    row->parallax_over_error = parse_double(get_field(fields, field_count, idx->parallax_over_error));
    row->ruwe = parse_double(get_field(fields, field_count, idx->ruwe));
    row->phot_g_mean_mag = parse_double(get_field(fields, field_count, idx->phot_g_mean_mag));
    row->phot_bp_mean_mag = parse_double(get_field(fields, field_count, idx->phot_bp_mean_mag));
    row->phot_rp_mean_mag = parse_double(get_field(fields, field_count, idx->phot_rp_mean_mag));
    row->nu_eff = parse_double(get_field(fields, field_count, idx->nu_eff_used_in_astrometry));
    row->pseudocolour = parse_double(get_field(fields, field_count, idx->pseudocolour));
    row->ecl_lat = parse_double(get_field(fields, field_count, idx->ecl_lat));
    
    const char *sol = get_field(fields, field_count, idx->astrometric_params_solved);
    row->astrometric_params_solved = sol ? (int)parse_long_long(sol) : 31;
    
    return 1;
}

int read_next_astro(gzFile file, char *buf, int size, AstroColIndices *idx, AstroRow *row) {
    char *line = gz_gets_uncommented(file, buf, size);
    if (!line) return 0;
    
    char *fields[MAX_FIELDS];
    int field_count = parse_csv_line(line, fields, MAX_FIELDS);
    
    row->source_id = parse_long_long(get_field(fields, field_count, idx->source_id));
    row->teff = parse_double(get_field(fields, field_count, idx->teff_gspphot));
    row->logg = parse_double(get_field(fields, field_count, idx->logg_gspphot));
    row->mh = parse_double(get_field(fields, field_count, idx->mh_gspphot));
    
    const char *spec = get_field(fields, field_count, idx->spectraltype_esphs);
    if (spec && strcmp(spec, "null") != 0 && strcmp(spec, "NOT_AVAILABLE") != 0 && *spec != '\0') {
        if (spec[0] == '"') {
            size_t len = strlen(spec);
            if (len > 2) {
                strncpy(row->spectraltype, spec + 1, len - 2);
                row->spectraltype[len - 2] = '\0';
            } else {
                strcpy(row->spectraltype, "");
            }
        } else {
            strncpy(row->spectraltype, spec, sizeof(row->spectraltype) - 1);
            row->spectraltype[sizeof(row->spectraltype) - 1] = '\0';
        }
    } else {
        strcpy(row->spectraltype, "");
    }
    
    return 1;
}

// Write Buffers for Block Writing
long long buf_source_id[WRITE_BUFFER_SIZE];
double buf_ra[WRITE_BUFFER_SIZE];
double buf_dec[WRITE_BUFFER_SIZE];
double buf_parallax[WRITE_BUFFER_SIZE];
double buf_phot_g_mean_mag[WRITE_BUFFER_SIZE];
double buf_phot_bp_mean_mag[WRITE_BUFFER_SIZE];
double buf_phot_rp_mean_mag[WRITE_BUFFER_SIZE];
double buf_nu_eff[WRITE_BUFFER_SIZE];
double buf_pseudocolour[WRITE_BUFFER_SIZE];
double buf_ecl_lat[WRITE_BUFFER_SIZE];
int buf_astrometric_params_solved[WRITE_BUFFER_SIZE];
double buf_teff[WRITE_BUFFER_SIZE];
double buf_logg[WRITE_BUFFER_SIZE];
double buf_mh[WRITE_BUFFER_SIZE];
char buf_spectraltype[WRITE_BUFFER_SIZE][12];
int buf_count = 0;
long long total_written_rows = 0;

void flush_buffer(fitsfile *fptr, int *status) {
    if (buf_count == 0) return;
    long long first_row = total_written_rows + 1;
    
    fits_write_col(fptr, TLONGLONG, 1, first_row, 1, buf_count, buf_source_id, status);
    fits_write_col(fptr, TDOUBLE,   2, first_row, 1, buf_count, buf_ra, status);
    fits_write_col(fptr, TDOUBLE,   3, first_row, 1, buf_count, buf_dec, status);
    fits_write_col(fptr, TDOUBLE,   4, first_row, 1, buf_count, buf_parallax, status);
    fits_write_col(fptr, TDOUBLE,   5, first_row, 1, buf_count, buf_phot_g_mean_mag, status);
    fits_write_col(fptr, TDOUBLE,   6, first_row, 1, buf_count, buf_phot_bp_mean_mag, status);
    fits_write_col(fptr, TDOUBLE,   7, first_row, 1, buf_count, buf_phot_rp_mean_mag, status);
    fits_write_col(fptr, TDOUBLE,   8, first_row, 1, buf_count, buf_nu_eff, status);
    fits_write_col(fptr, TDOUBLE,   9, first_row, 1, buf_count, buf_pseudocolour, status);
    fits_write_col(fptr, TDOUBLE,  10, first_row, 1, buf_count, buf_ecl_lat, status);
    fits_write_col(fptr, TINT,     11, first_row, 1, buf_count, buf_astrometric_params_solved, status);
    fits_write_col(fptr, TDOUBLE,  12, first_row, 1, buf_count, buf_teff, status);
    fits_write_col(fptr, TDOUBLE,  13, first_row, 1, buf_count, buf_logg, status);
    fits_write_col(fptr, TDOUBLE,  14, first_row, 1, buf_count, buf_mh, status);
    
    char *str_ptrs[WRITE_BUFFER_SIZE];
    for (int i = 0; i < buf_count; i++) {
        str_ptrs[i] = buf_spectraltype[i];
    }
    fits_write_col(fptr, TSTRING, 15, first_row, 1, buf_count, str_ptrs, status);
    
    total_written_rows += buf_count;
    buf_count = 0;
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "Usage: %s <GaiaSource.csv.gz> <AstrophysicalParameters.csv.gz> <output.fits.gz>\n", argv[0]);
        return 1;
    }
    
    const char *gaia_path = argv[1];
    const char *astro_path = argv[2];
    const char *out_path = argv[3];
    
    printf("Opening GaiaSource: %s\n", gaia_path);
    gzFile gaia_file = gzopen(gaia_path, "rb");
    if (!gaia_file) {
        fprintf(stderr, "Error: Could not open %s\n", gaia_path);
        return 1;
    }
    
    printf("Opening AstrophysicalParameters: %s\n", astro_path);
    gzFile astro_file = gzopen(astro_path, "rb");
    if (!astro_file) {
        fprintf(stderr, "Error: Could not open %s\n", astro_path);
        gzclose(gaia_file);
        return 1;
    }
    
    char *gaia_buf = malloc(LINE_BUFFER_SIZE);
    char *astro_buf = malloc(LINE_BUFFER_SIZE);
    
    GaiaColIndices gaia_idx;
    AstroColIndices astro_idx;
    
    if (!read_headers_gaia(gaia_file, gaia_buf, LINE_BUFFER_SIZE, &gaia_idx)) {
        fprintf(stderr, "Error: Could not read GaiaSource headers\n");
        free(gaia_buf);
        free(astro_buf);
        gzclose(gaia_file);
        gzclose(astro_file);
        return 1;
    }
    
    if (!read_headers_astro(astro_file, astro_buf, LINE_BUFFER_SIZE, &astro_idx)) {
        fprintf(stderr, "Error: Could not read AstrophysicalParameters headers\n");
        free(gaia_buf);
        free(astro_buf);
        gzclose(gaia_file);
        gzclose(astro_file);
        return 1;
    }
    
    if (gaia_idx.source_id == -1 || gaia_idx.ra == -1 || gaia_idx.dec == -1 || gaia_idx.parallax == -1) {
        fprintf(stderr, "Error: Mandatory GaiaSource columns missing\n");
        free(gaia_buf);
        free(astro_buf);
        gzclose(gaia_file);
        gzclose(astro_file);
        return 1;
    }
    
    if (astro_idx.source_id == -1) {
        fprintf(stderr, "Error: AstrophysicalParameters source_id column missing\n");
        free(gaia_buf);
        free(astro_buf);
        gzclose(gaia_file);
        gzclose(astro_file);
        return 1;
    }
    
    // Create CFITSIO output file
    fitsfile *fptr;
    int fits_status = 0;
    remove(out_path); // Clear any old file
    
    if (fits_create_file(&fptr, out_path, &fits_status)) {
        fits_report_error(stderr, fits_status);
        free(gaia_buf);
        free(astro_buf);
        gzclose(gaia_file);
        gzclose(astro_file);
        return 1;
    }
    
    int tfields = 15;
    char *ttype[] = {
        "source_id", "ra", "dec", "parallax", 
        "phot_g_mean_mag", "phot_bp_mean_mag", "phot_rp_mean_mag",
        "nu_eff_used_in_astrometry", "pseudocolour", "ecl_lat",
        "astrometric_params_solved", "teff_gspphot", "logg_gspphot",
        "mh_gspphot", "spectraltype_esphs"
    };
    char *tform[] = {
        "1K", "1D", "1D", "1D",
        "1D", "1D", "1D",
        "1D", "1D", "1D",
        "1J", "1D", "1D",
        "1D", "12A"
    };
    char *tunit[] = {
        "", "deg", "deg", "mas",
        "mag", "mag", "mag",
        "um**-1", "um**-1", "deg",
        "", "K", "log(cm.s**-2)",
        "dex", ""
    };
    
    if (fits_create_tbl(fptr, BINARY_TBL, 0, tfields, ttype, tform, tunit, "GaiaStars", &fits_status)) {
        fits_report_error(stderr, fits_status);
        fits_close_file(fptr, &fits_status);
        free(gaia_buf);
        free(astro_buf);
        gzclose(gaia_file);
        gzclose(astro_file);
        return 1;
    }
    
    printf("Joining and filtering stars...\n");
    
    GaiaRow gaia_row;
    AstroRow astro_row;
    
    int has_gaia = read_next_gaia(gaia_file, gaia_buf, LINE_BUFFER_SIZE, &gaia_idx, &gaia_row);
    int has_astro = read_next_astro(astro_file, astro_buf, LINE_BUFFER_SIZE, &astro_idx, &astro_row);
    
    long long total_processed = 0;
    long long total_kept = 0;
    
    while (has_gaia) {
        total_processed++;
        
        // Sync AstrophysicalParameters to GaiaSource using O(N) sort-merge join
        while (has_astro && astro_row.source_id < gaia_row.source_id) {
            has_astro = read_next_astro(astro_file, astro_buf, LINE_BUFFER_SIZE, &astro_idx, &astro_row);
        }
        
        int match = (has_astro && astro_row.source_id == gaia_row.source_id);
        
        // Filter: Positive Parallax, SNR (parallax_over_error) >= 5, and RUWE <= 1.4
        // Consistent with process_chunk.py
        int keep = 1;
        if (isnan(gaia_row.parallax) || gaia_row.parallax <= 0.0) keep = 0;
        if (isnan(gaia_row.ra) || isnan(gaia_row.dec)) keep = 0;
        if (isnan(gaia_row.parallax_over_error) || gaia_row.parallax_over_error < 5.0) keep = 0;
        if (isnan(gaia_row.ruwe) || gaia_row.ruwe > 1.4) keep = 0;
        
        if (keep) {
            // Buffer the row for writing
            buf_source_id[buf_count] = gaia_row.source_id;
            buf_ra[buf_count] = gaia_row.ra;
            buf_dec[buf_count] = gaia_row.dec;
            buf_parallax[buf_count] = gaia_row.parallax;
            buf_phot_g_mean_mag[buf_count] = gaia_row.phot_g_mean_mag;
            buf_phot_bp_mean_mag[buf_count] = gaia_row.phot_bp_mean_mag;
            buf_phot_rp_mean_mag[buf_count] = gaia_row.phot_rp_mean_mag;
            buf_nu_eff[buf_count] = gaia_row.nu_eff;
            buf_pseudocolour[buf_count] = gaia_row.pseudocolour;
            buf_ecl_lat[buf_count] = gaia_row.ecl_lat;
            buf_astrometric_params_solved[buf_count] = gaia_row.astrometric_params_solved;
            
            if (match) {
                buf_teff[buf_count] = astro_row.teff;
                buf_logg[buf_count] = astro_row.logg;
                buf_mh[buf_count] = astro_row.mh;
                strcpy(buf_spectraltype[buf_count], astro_row.spectraltype);
            } else {
                buf_teff[buf_count] = NAN;
                buf_logg[buf_count] = NAN;
                buf_mh[buf_count] = NAN;
                strcpy(buf_spectraltype[buf_count], "");
            }
            
            buf_count++;
            total_kept++;
            
            if (buf_count == WRITE_BUFFER_SIZE) {
                flush_buffer(fptr, &fits_status);
                if (fits_status) {
                    fprintf(stderr, "Error: CFITSIO error during block write\n");
                    fits_report_error(stderr, fits_status);
                    break;
                }
            }
        }
        
        // If we matched, we can advance astro reader
        if (match) {
            has_astro = read_next_astro(astro_file, astro_buf, LINE_BUFFER_SIZE, &astro_idx, &astro_row);
        }
        
        has_gaia = read_next_gaia(gaia_file, gaia_buf, LINE_BUFFER_SIZE, &gaia_idx, &gaia_row);
    }
    
    // Flush remaining buffer
    if (buf_count > 0 && !fits_status) {
        flush_buffer(fptr, &fits_status);
    }
    
    printf("Processed %lld stars, Kept %lld high-quality stars.\n", total_processed, total_kept);
    
    fits_close_file(fptr, &fits_status);
    free(gaia_buf);
    free(astro_buf);
    gzclose(gaia_file);
    gzclose(astro_file);
    
    if (fits_status) {
        fits_report_error(stderr, fits_status);
        return 1;
    }
    
    printf("FITS File successfully written: %s\n", out_path);
    return 0;
}
