#include <stdio.h>
#include <stdint.h>
#include <math.h>

#include "geo_utils.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TOL_DMS 1e-9

static int failures = 0;

static void check(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAILED: %s\n", msg);
        failures++;
    } else {
        printf("ok: %s\n", msg);
    }
}

static void test_dms32(double x) {
    int sign;
    int32_t deg, min;
    double sec;
    degrees_to_dms32(x, &sign, &deg, &min, &sec);
    double back = dms32_to_degrees(sign, deg, min, sec);
    char msg[128];
    snprintf(msg, sizeof(msg), "dms32 round-trip for %.6f (got %.9f)", x, back);
    check(fabs(back - x) < TOL_DMS, msg);
}

static void test_dms64(double x) {
    int sign;
    int64_t deg, min;
    double sec;
    degrees_to_dms64(x, &sign, &deg, &min, &sec);
    double back = dms64_to_degrees(sign, deg, min, sec);
    char msg[128];
    snprintf(msg, sizeof(msg), "dms64 round-trip for %.6f (got %.9f)", x, back);
    check(fabs(back - x) < TOL_DMS, msg);
}

static void test_dms128(double x) {
    int sign;
    __int128 deg, min;
    double sec;
    degrees_to_dms128(x, &sign, &deg, &min, &sec);
    double back = dms128_to_degrees(sign, deg, min, sec);
    char msg[128];
    snprintf(msg, sizeof(msg), "dms128 round-trip for %.6f (got %.9f)", x, back);
    check(fabs(back - x) < TOL_DMS, msg);
}

static void test_dmsf64(double x) {
    int sign;
    double deg, min;
    double sec;
    degrees_to_dmsf64(x, &sign, &deg, &min, &sec);
    double back = dmsf64_to_degrees(sign, deg, min, sec);
    char msg[128];
    snprintf(msg, sizeof(msg), "dmsf64 round-trip for %.6f (got %.9f)", x, back);
    check(fabs(back - x) < TOL_DMS, msg);
}

int main(void) {
    printf("== DMS round-trip tests ==\n");
    double angles[] = { -0.25, 0.0, 15.5, -15.5, 179.999, -179.999, 90.0, -90.0, 0.001, -0.001 };
    size_t n = sizeof(angles) / sizeof(angles[0]);
    for (size_t i = 0; i < n; i++) {
        test_dms32(angles[i]);
        test_dms64(angles[i]);
        test_dms128(angles[i]);
        test_dmsf64(angles[i]);
    }

    printf("== Haversine sanity check ==\n");
    {
        double R = EARTH_RADIUS_KM;
        double d = haversine_distance(0.0, 0.0, 90.0, 0.0, R);
        double expected = R * M_PI / 2.0;
        printf("haversine(0,0 -> 90,0) = %.9f, expected %.9f\n", d, expected);
        check(fabs(d - expected) < 1e-9, "haversine quarter-great-circle distance");
    }

    printf("== Vincenty sanity check (equator, WGS-84) ==\n");
    {
        double delta = 10.0;
        double d = vincenty_distance(0.0, 0.0, 0.0, delta, EARTH_SEMI_MAJOR_AXIS_M, EARTH_FLATTENING);
        double expected = EARTH_SEMI_MAJOR_AXIS_M * (delta * M_PI / 180.0);
        printf("vincenty(0,0 -> 0,10) = %.9f, expected %.9f\n", d, expected);
        check(fabs(d - expected) < 1e-6, "vincenty equatorial exact circle distance");
    }

    printf("== Vincenty near-antipodal graceful degradation ==\n");
    {
        double d = vincenty_distance(0.0, 0.0, 0.001, 179.999, EARTH_SEMI_MAJOR_AXIS_M, EARTH_FLATTENING);
        printf("vincenty(0,0 -> 0.001,179.999) = %.9f\n", d);
        check(isfinite(d), "vincenty near-antipodal returns finite value");
    }

    printf("== HEALPix nested index (forward) ==\n");
    {
        /* Covers both the polar-cap and equatorial-belt code paths, at a few
         * different levels. Reference values are independently verified
         * against astropy_healpix's lonlat_to_healpix (NESTED scheme), not
         * self-derived -- see the forward/inverse round-trip test below for
         * why that distinction matters.
         *
         * (0.0, 0.0) is deliberately not used here: it sits exactly on a
         * corner shared by 4 base faces, where even astropy_healpix's own
         * result is a floating-point tie-break that differs from its result
         * at every immediately adjacent point. (0.001, 0.0) is the nearby,
         * unambiguous stand-in. */
        struct { double ra, dec; int level; uint64_t want; } fwd[] = {
            { 217.4290, -62.6795, 12, 170359233ULL }, /* equatorial belt, high level */
            { -109.05653, 44.52634, 3, 156ULL },      /* north cap, negative lon */
            { 45.0, 10.0, 3, 3ULL },                  /* north cap */
            { 0.001, 0.0, 3, 282ULL },                /* equatorial belt, near origin */
            { 200.0, -20.0, 5, 6228ULL },              /* equatorial belt, higher level */
        };
        for (size_t i = 0; i < sizeof(fwd) / sizeof(fwd[0]); i++) {
            uint64_t got = healpix_nested_index(fwd[i].ra, fwd[i].dec, fwd[i].level);
            char msg[128];
            snprintf(msg, sizeof(msg), "healpix_nested_index(%.5f, %.5f, %d) == %llu (got %llu)",
                     fwd[i].ra, fwd[i].dec, fwd[i].level, (unsigned long long)fwd[i].want, (unsigned long long)got);
            check(got == fwd[i].want, msg);
        }

        uint64_t a = healpix_nested_index(-109.05653, 44.52634, 3);
        uint64_t b = healpix_nested_index(250.94347, 44.52634, 3);
        check(a == b, "healpix_nested_index: -109.05653 and its +360 equivalent land in the same cell");
    }

    printf("== HEALPix index to coordinates (inverse) ==\n");
    {
        /* Expected values are the exact center-of-pixel coordinates for each
         * index, independently verified against astropy_healpix's
         * healpix_to_lonlat (NESTED scheme) -- covers north cap, equatorial
         * belt, and south cap, plus the first/last valid index at a level.
         *
         * (An earlier version of this table used values self-derived from
         * this file's own pre-fix nest2ring/ring2coords implementation,
         * which had a real bug in both polar-cap branches that canceled out
         * against itself in that circular check. Cross-checking against an
         * independent reference implementation is what caught it -- see the
         * forward/inverse round-trip test below.) */
        struct { int level; uint64_t idx; double want_ra, want_dec; const char *note; } inv[] = {
            { 12, 134053741ULL, 274.273681640625000, 37.005237186492252, "equatorial belt, high level" },
            { 3, 330ULL, 73.125000000000000, -19.471220634490692, "equatorial belt" },
            { 3, 282ULL, 5.625000000000000, 0.000000000000000, "equatorial belt" },
            { 3, 256ULL, 0.000000000000000, -35.685334712652057, "south cap, ra wraps to 0" },
            { 3, 0ULL, 45.000000000000000, 4.780191847199159, "first valid index" },
            { 3, 767ULL, 315.000000000000000, -4.780191847199159, "last valid index (12*8^2-1)" },
        };
        for (size_t i = 0; i < sizeof(inv) / sizeof(inv[0]); i++) {
            double ra, dec;
            int status = healpix_nested_index_to_coords(inv[i].level, inv[i].idx, &ra, &dec);
            char msg[160];
            snprintf(msg, sizeof(msg), "healpix_nested_index_to_coords(level=%d, idx=%llu) [%s]: ra=%.9f dec=%.9f",
                     inv[i].level, (unsigned long long)inv[i].idx, inv[i].note, ra, dec);
            check(status == 0 && fabs(ra - inv[i].want_ra) < 1e-9 && fabs(dec - inv[i].want_dec) < 1e-9, msg);
        }

        /* Cross-check the RING-scheme entry point directly against a known
         * RING index (100 at level 3, independently verified against
         * astropy_healpix's RING-scheme healpix_to_lonlat), independent of
         * the NESTED path above. */
        double ra, dec;
        int status = healpix_ring_index_to_coords(3, 100, &ra, &dec);
        check(status == 0 && fabs(ra - 212.142857142857139) < 1e-9 && fabs(dec - 48.141207794360284) < 1e-9,
              "healpix_ring_index_to_coords(level=3, ring=100) matches known value");

        /* Out-of-range indices (>= 12*nside^2 = 768 at level 3) must fail cleanly, not
         * silently return garbage coordinates. */
        status = healpix_nested_index_to_coords(3, 768, &ra, &dec);
        check(status != 0, "healpix_nested_index_to_coords rejects an out-of-range index");
        status = healpix_ring_index_to_coords(3, 768, &ra, &dec);
        check(status != 0, "healpix_ring_index_to_coords rejects an out-of-range index");
    }

    printf("== HEALPix forward/inverse round-trip ==\n");
    {
        /* forward(lon, lat) -> idx -> inverse(idx) must land in the SAME cell as
         * forward(inverse(idx)) -- i.e. re-encoding the decoded center must
         * recover the exact same index, even though the decoded center is not
         * necessarily equal to the original (lon, lat) that produced idx. */
        double lon = -109.05653, lat = 44.52634;
        int level = 3;
        uint64_t idx = healpix_nested_index(lon, lat, level);
        double center_lon, center_lat;
        int status = healpix_nested_index_to_coords(level, idx, &center_lon, &center_lat);
        uint64_t idx_again = healpix_nested_index(center_lon, center_lat, level);
        check(status == 0 && idx_again == idx, "healpix forward->inverse->forward round-trips to the same cell");
    }

    if (failures > 0) {
        fprintf(stderr, "%d test(s) failed.\n", failures);
        return 1;
    }

    printf("All tests passed.\n");
    return 0;
}
