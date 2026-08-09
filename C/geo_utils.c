#include <math.h>

#include "geo_utils.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define TO_RAD (M_PI / 180.0)

double haversine_distance(double lat1, double lon1, double lat2, double lon2, double radius) {
    double phi1 = lat1 * TO_RAD;
    double phi2 = lat2 * TO_RAD;
    double dphi = (lat2 - lat1) * TO_RAD;
    double dlambda = (lon2 - lon1) * TO_RAD;

    double a = sin(dphi / 2.0) * sin(dphi / 2.0) +
                cos(phi1) * cos(phi2) * sin(dlambda / 2.0) * sin(dlambda / 2.0);

    double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));

    return radius * c;
}

/*
 * Vincenty inverse formula on an oblate spheroid. Known limitation: this
 * can fail to fully converge for nearly-antipodal points, which is why the
 * iteration is capped -- when the cap is hit, the distance is still
 * computed and returned from the last iteration's sigma/cos2SigmaM rather
 * than erroring out or looping forever.
 */
double vincenty_distance(double lat1, double lon1, double lat2, double lon2, double semi_major_axis, double flattening) {
    double a = semi_major_axis;
    double f = flattening;
    double b = a * (1.0 - f);

    double lat1_rad = lat1 * TO_RAD;
    double lon1_rad = lon1 * TO_RAD;
    double lat2_rad = lat2 * TO_RAD;
    double lon2_rad = lon2 * TO_RAD;

    double L = lon2_rad - lon1_rad;
    double U1 = atan((1.0 - f) * tan(lat1_rad));
    double U2 = atan((1.0 - f) * tan(lat2_rad));
    double sinU1 = sin(U1), cosU1 = cos(U1);
    double sinU2 = sin(U2), cosU2 = cos(U2);

    double lambda = L;
    double sinSigma = 0.0, cosSigma = 1.0, sigma = 0.0;
    double sinAlpha = 0.0, cosSqAlpha = 1.0, cos2SigmaM = 1.0;

    int i;
    for (i = 0; i < 200; i++) {
        double sinLambda = sin(lambda);
        double cosLambda = cos(lambda);

        sinSigma = sqrt((cosU2 * sinLambda) * (cosU2 * sinLambda) +
                         (cosU1 * sinU2 - sinU1 * cosU2 * cosLambda) *
                         (cosU1 * sinU2 - sinU1 * cosU2 * cosLambda));

        if (sinSigma == 0.0) {
            /* Coincident points. */
            return 0.0;
        }

        cosSigma = sinU1 * sinU2 + cosU1 * cosU2 * cosLambda;
        sigma = atan2(sinSigma, cosSigma);

        sinAlpha = cosU1 * cosU2 * sinLambda / sinSigma;
        cosSqAlpha = 1.0 - sinAlpha * sinAlpha;

        if (cosSqAlpha != 0.0) {
            cos2SigmaM = cosSigma - 2.0 * sinU1 * sinU2 / cosSqAlpha;
        } else {
            /* Equatorial line special case. */
            cos2SigmaM = 0.0;
        }

        double C = f / 16.0 * cosSqAlpha * (4.0 + f * (4.0 - 3.0 * cosSqAlpha));
        double lambda_prev = lambda;
        lambda = L + (1.0 - C) * f * sinAlpha *
                 (sigma + C * sinSigma * (cos2SigmaM + C * cosSigma * (-1.0 + 2.0 * cos2SigmaM * cos2SigmaM)));

        if (fabs(lambda - lambda_prev) < 1e-12) {
            break;
        }
    }

    double uSq = cosSqAlpha * (a * a - b * b) / (b * b);
    double A = 1.0 + uSq / 16384.0 * (4096.0 + uSq * (-768.0 + uSq * (320.0 - 175.0 * uSq)));
    double B = uSq / 1024.0 * (256.0 + uSq * (-128.0 + uSq * (74.0 - 47.0 * uSq)));
    double deltaSigma = B * sinSigma * (cos2SigmaM + B / 4.0 *
                         (cosSigma * (-1.0 + 2.0 * cos2SigmaM * cos2SigmaM) -
                          B / 6.0 * cos2SigmaM * (-3.0 + 4.0 * sinSigma * sinSigma) *
                          (-3.0 + 4.0 * cos2SigmaM * cos2SigmaM)));

    return b * A * (sigma - deltaSigma);
}

double dms32_to_degrees(int sign, int32_t deg, int32_t min, double sec) {
    return sign * ((double)deg + (double)min / 60.0 + sec / 3600.0);
}

void degrees_to_dms32(double degrees, int *sign, int32_t *deg, int32_t *min, double *sec) {
    *sign = (degrees < 0) ? -1 : 1;
    double a = fabs(degrees);
    *deg = (int32_t)floor(a);
    double rem_min = (a - (double)*deg) * 60.0;
    *min = (int32_t)floor(rem_min);
    *sec = (rem_min - (double)*min) * 60.0;
}

double dms64_to_degrees(int sign, int64_t deg, int64_t min, double sec) {
    return sign * ((double)deg + (double)min / 60.0 + sec / 3600.0);
}

void degrees_to_dms64(double degrees, int *sign, int64_t *deg, int64_t *min, double *sec) {
    *sign = (degrees < 0) ? -1 : 1;
    double a = fabs(degrees);
    *deg = (int64_t)floor(a);
    double rem_min = (a - (double)*deg) * 60.0;
    *min = (int64_t)floor(rem_min);
    *sec = (rem_min - (double)*min) * 60.0;
}

double dms128_to_degrees(int sign, __int128 deg, __int128 min, double sec) {
    return sign * ((double)deg + (double)min / 60.0 + sec / 3600.0);
}

void degrees_to_dms128(double degrees, int *sign, __int128 *deg, __int128 *min, double *sec) {
    *sign = (degrees < 0) ? -1 : 1;
    double a = fabs(degrees);
    *deg = (__int128)floor(a);
    double rem_min = (a - (double)*deg) * 60.0;
    *min = (__int128)floor(rem_min);
    *sec = (rem_min - (double)*min) * 60.0;
}

double dmsf64_to_degrees(int sign, double deg, double min, double sec) {
    return sign * (deg + min / 60.0 + sec / 3600.0);
}

void degrees_to_dmsf64(double degrees, int *sign, double *deg, double *min, double *sec) {
    *sign = (degrees < 0) ? -1 : 1;
    double a = fabs(degrees);
    *deg = floor(a);
    double rem_min = (a - *deg) * 60.0;
    *min = floor(rem_min);
    *sec = (rem_min - *min) * 60.0;
}
