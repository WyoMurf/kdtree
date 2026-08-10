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

/*
 * Interleaves the bits of two 32-bit integers into a 64-bit Morton (Z-order
 * curve) code -- the standard way to build a HEALPix NESTED pixel index out
 * of a face's local (i, j) grid coordinates.
 */
static uint64_t interleave_bits(uint32_t x, uint32_t y) {
    uint64_t res = 0;
    for (int i = 0; i < 32; i++) {
        res |= (((uint64_t)(x & (1U << i))) << i) | (((uint64_t)(y & (1U << i))) << (i + 1));
    }
    return res;
}

uint64_t healpix_nested_index(double ra_or_lon_deg, double dec_or_lat_deg, int level) {
    /* Normalize to [0, 360) so callers can pass geographic longitude
     * ([-180, 180)) directly, same as right ascension ([0, 360), already a
     * no-op here). */
    double lon = fmod(ra_or_lon_deg, 360.0);
    if (lon < 0.0) lon += 360.0;

    double phi = lon * TO_RAD;
    double z = sin(dec_or_lat_deg * TO_RAD);
    double za = fabs(z);

    /* tt is phi scaled so a full turn spans [0, 4) -- one unit per base-face
     * column, matching the jrll/jpll face layout used by the inverse
     * (healpix_nested_to_ring) below. */
    double tt = fmod(phi, 2.0 * M_PI);
    if (tt < 0.0) tt += 2.0 * M_PI;
    tt *= 2.0 / M_PI;

    int64_t nside = 1LL << level;
    int64_t face_num;
    uint32_t ix, iy;

    if (za <= 2.0 / 3.0) {
        /* Equatorial belt. */
        double temp1 = nside * (0.5 + tt);
        double temp2 = nside * (z * 0.75);
        int64_t jp = (int64_t)floor(temp1 - temp2); /* ascending edge line index */
        int64_t jm = (int64_t)floor(temp1 + temp2); /* descending edge line index */
        int64_t ifp = jp / nside;
        int64_t ifm = jm / nside;
        face_num = (ifp == ifm) ? (ifp | 4) : ((ifp < ifm) ? ifp : (ifm + 8));
        ix = (uint32_t)(jm & (nside - 1));
        iy = (uint32_t)(nside - (jp & (nside - 1)) - 1);
    } else {
        /* Polar caps. */
        int ntt = (int)tt;
        if (ntt >= 4) ntt = 3;
        double tp = tt - ntt;
        double tmp = nside * sqrt(3.0 * (1.0 - za));

        int64_t jp = (int64_t)(tp * tmp);       /* increasing edge line index */
        int64_t jm = (int64_t)((1.0 - tp) * tmp); /* decreasing edge line index */
        if (jp >= nside) jp = nside - 1;
        if (jm >= nside) jm = nside - 1;

        if (z >= 0.0) {
            face_num = ntt;
            ix = (uint32_t)(nside - jm - 1);
            iy = (uint32_t)(nside - jp - 1);
        } else {
            face_num = ntt + 8;
            ix = (uint32_t)jp;
            iy = (uint32_t)jm;
        }
    }

    uint64_t face_pixels = (uint64_t)nside * (uint64_t)nside;
    uint64_t morton = interleave_bits(ix, iy);

    return (uint64_t)face_num * face_pixels + morton;
}

/*
 * De-interleaves a 64-bit Morton code back into its original two 32-bit x/y
 * components -- the inverse of interleave_bits(), used when converting a
 * HEALPix NESTED pixel index back to a face's local (i, j) grid coordinates.
 */
static void deinterleave_bits(uint64_t ip, uint32_t *ix, uint32_t *iy) {
    uint64_t x = 0, y = 0;
    for (int i = 0; i < 32; i++) {
        x |= ((ip >> (2 * i)) & 1) << i;
        y |= ((ip >> (2 * i + 1)) & 1) << i;
    }
    *ix = (uint32_t)x;
    *iy = (uint32_t)y;
}

/*
 * Converts a HEALPix NESTED index to the equivalent 0-based RING-scheme
 * index, for the given nside. Returns -1 if nested_index is out of range
 * (>= 12*nside^2).
 */
static int64_t healpix_nested_to_ring(int64_t nside, int64_t nested_index) {
    int64_t npix = 12 * nside * nside;
    if (nested_index < 0 || nested_index >= npix) return -1;

    int64_t nside2 = nside * nside;
    int64_t face_num = nested_index / nside2;
    uint64_t ipf = (uint64_t)(nested_index % nside2);

    uint32_t ix, iy;
    deinterleave_bits(ipf, &ix, &iy);

    static const int jrll[] = { 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4 };
    static const int jpll[] = { 1, 3, 5, 7, 0, 2, 4, 6, 1, 3, 5, 7 };

    int64_t jr = (int64_t)jrll[face_num] * nside - (int64_t)ix - (int64_t)iy - 1;

    int64_t nr, n_before, kshift;
    if (jr < nside) { /* North polar cap */
        nr = jr;
        n_before = 2 * nr * (nr - 1);
        kshift = 0;
    } else if (jr > 3 * nside) { /* South polar cap */
        nr = 4 * nside - jr;
        n_before = npix - 2 * (nr + 1) * nr;
        kshift = 0;
    } else { /* Equatorial belt */
        nr = nside;
        n_before = 2 * nside * (nside - 1) + (jr - nside) * 4 * nside;
        kshift = (jr - nside) & 1;
    }

    int64_t jp = ((int64_t)jpll[face_num] * nr + (int64_t)ix - (int64_t)iy + 1 + kshift) / 2;
    if (jp > 4 * nr) jp -= 4 * nr;
    if (jp < 1) jp += 4 * nr;

    return n_before + jp - 1;
}

int healpix_ring_index_to_coords(int level, uint64_t ring_index, double *ra_or_lon_deg, double *dec_or_lat_deg) {
    int64_t nside = 1LL << level;
    int64_t npix = 12 * nside * nside;
    int64_t p_ring = (int64_t)ring_index;
    if (p_ring < 0 || p_ring >= npix) return -1;

    int64_t ncap = 2 * nside * (nside - 1);
    double z, phi;
    int64_t ip = p_ring; /* 0-indexed, matches the canonical pix2ang_ring pseudocode's "ip" */

    if (ip < ncap) { /* North polar cap */
        double hip = (double)(ip + 1) / 2.0;
        double fihip = floor(hip);
        int64_t irn = (int64_t)(floor(sqrt(hip - sqrt(fihip)))) + 1;
        int64_t iphi = (ip + 1) - 2 * irn * (irn - 1);
        z = 1.0 - (double)(irn * irn) / (3.0 * (double)nside * (double)nside);
        phi = ((double)iphi - 0.5) * M_PI / (2.0 * (double)irn);
    } else if (ip < npix - ncap) { /* Equatorial belt */
        int64_t ip1 = ip - ncap;
        int64_t irn = ip1 / (4 * nside) + nside;
        int64_t iphi = ip1 % (4 * nside) + 1;
        double fodd = 0.5 * (double)(1 + (int)((irn + nside) % 2));
        z = (double)(2 * nside - irn) * 2.0 / (3.0 * (double)nside);
        phi = ((double)iphi - fodd) * M_PI / (2.0 * (double)nside);
    } else { /* South polar cap */
        int64_t ip1 = npix - ip;
        double hip = (double)ip1 / 2.0;
        double fihip = floor(hip);
        int64_t irs = (int64_t)(floor(sqrt(hip - sqrt(fihip)))) + 1;
        int64_t iphi = 4 * irs + 1 - (ip1 - 2 * irs * (irs - 1));
        z = -1.0 + (double)(irs * irs) / (3.0 * (double)nside * (double)nside);
        phi = ((double)iphi - 0.5) * M_PI / (2.0 * (double)irs);
    }

    double ra = phi * (180.0 / M_PI);
    while (ra < 0.0) ra += 360.0;
    while (ra >= 360.0) ra -= 360.0;

    *ra_or_lon_deg = ra;
    *dec_or_lat_deg = asin(z) * (180.0 / M_PI);
    return 0;
}

int healpix_nested_index_to_coords(int level, uint64_t nested_index, double *ra_or_lon_deg, double *dec_or_lat_deg) {
    int64_t nside = 1LL << level;
    int64_t ring_index = healpix_nested_to_ring(nside, (int64_t)nested_index);
    if (ring_index < 0) return -1;
    return healpix_ring_index_to_coords(level, (uint64_t)ring_index, ra_or_lon_deg, dec_or_lat_deg);
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
