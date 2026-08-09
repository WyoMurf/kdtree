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

    if (failures > 0) {
        fprintf(stderr, "%d test(s) failed.\n", failures);
        return 1;
    }

    printf("All tests passed.\n");
    return 0;
}
