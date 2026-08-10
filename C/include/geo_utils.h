#ifndef GEO_UTILS_H
#define GEO_UTILS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * General-purpose geo/angle math utilities. These are independent of the
 * kd-tree implementation itself -- they're provided alongside it for
 * convenience when working with geographic coordinates (e.g. star catalogs,
 * terrestrial coordinates) that end up stored in a kd-tree.
 *
 * All latitude/longitude angle inputs and outputs are in degrees unless
 * otherwise noted.
 */

/* WGS-84 Earth constants. Pass these to vincenty_distance() (or use
 * EARTH_RADIUS_KM with haversine_distance()) when modeling Earth
 * specifically -- the majority of callers will want exactly these values. */
#define EARTH_RADIUS_KM 6371.0
#define EARTH_SEMI_MAJOR_AXIS_M 6378137.0
#define EARTH_FLATTENING (1.0 / 298.257223563)

/*
 * Fast, approximate great-circle distance between two lat/lon points on a
 * perfect sphere of the given radius, using the haversine formula. Units of
 * the return value match the units of `radius` (e.g. pass EARTH_RADIUS_KM
 * for kilometers on Earth modeled as a sphere).
 */
double haversine_distance(double lat1, double lon1, double lat2, double lon2, double radius);

/*
 * Slow, iterative, exact geodesic distance between two lat/lon points on an
 * oblate spheroid, via the Vincenty inverse formula. `semi_major_axis` and
 * `flattening` describe the spheroid; the return value is in the same units
 * as `semi_major_axis` (e.g. pass EARTH_SEMI_MAJOR_AXIS_M and
 * EARTH_FLATTENING for meters on the WGS-84 Earth ellipsoid -- these are the
 * values to use in the majority of cases where the model is Earth).
 *
 * Known limitation: the underlying iteration can fail to fully converge for
 * nearly-antipodal points. An iteration cap prevents hanging in that case;
 * when the cap is hit, the best available estimate is returned rather than
 * an error.
 */
double vincenty_distance(double lat1, double lon1, double lat2, double lon2, double semi_major_axis, double flattening);

/*
 * Converts an equatorial-style (ra, dec) or geographic (lon, lat) pair, in
 * degrees, into a HEALPix NESTED-scheme pixel index at the given resolution
 * `level` (nside = 2^level; 12*nside^2 cells total over the whole sphere --
 * level 3 is 768 cells, a common "roughly a thousand tiles" choice; valid
 * levels are 0..29). The two angle arguments are mathematically
 * interchangeable -- this is the same equatorial-coordinate projection
 * either way -- so pass right ascension/declination for astronomical data,
 * or longitude/latitude for terrestrial data. The first angle is
 * normalized internally, so it may be given in either the conventional
 * [0, 360) astronomical range or the conventional [-180, 180) geographic
 * range; callers don't need to pre-normalize longitude before calling.
 */
uint64_t healpix_nested_index(double ra_or_lon_deg, double dec_or_lat_deg, int level);

/*
 * Degrees/minutes/seconds <-> decimal degrees conversions, one pair per
 * bit-width flavor (C has no generics). Degrees and minutes are always
 * non-negative magnitudes; the separate `sign` parameter (+1 or -1) carries
 * the sign, which correctly represents angles between -1 and 0 degrees
 * (e.g. -0 deg 15 min) that a signed-degrees-only field cannot. Seconds is
 * always `double` since sub-arcsecond precision matters, while sub-integer
 * degrees/minutes do not.
 */
double dms32_to_degrees(int sign, int32_t deg, int32_t min, double sec);
void   degrees_to_dms32(double degrees, int *sign, int32_t *deg, int32_t *min, double *sec);

double dms64_to_degrees(int sign, int64_t deg, int64_t min, double sec);
void   degrees_to_dms64(double degrees, int *sign, int64_t *deg, int64_t *min, double *sec);

double dms128_to_degrees(int sign, __int128 deg, __int128 min, double sec);
void   degrees_to_dms128(double degrees, int *sign, __int128 *deg, __int128 *min, double *sec);

double dmsf64_to_degrees(int sign, double deg, double min, double sec);
void   degrees_to_dmsf64(double degrees, int *sign, double *deg, double *min, double *sec);

#ifdef __cplusplus
}
#endif

#endif /* GEO_UTILS_H */
