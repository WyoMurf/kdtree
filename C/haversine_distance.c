#include <math.h>

#define EARTH_RADIUS_KM 6371.0
#define TO_RAD (M_PI / 180.0)

double haversine_distance(double lat1, double lon1, double lat2, double lon2) {
    // Convert degrees to radians
    double lat1_rad = lat1 * TO_RAD;
    double lat2_rad = lat2 * TO_RAD;
    double d_lat = (lat2 - lat1) * TO_RAD;
    double d_lon = (lon2 - lon1) * TO_RAD;

    // Haversine formula
    double a = sin(d_lat / 2.0) * sin(d_lat / 2.0) +
               cos(lat1_rad) * cos(lat2_rad) *
               sin(d_lon / 2.0) * sin(d_lon / 2.0);

   double c = 2.0 * atan2(sqrt(a), sqrt(1.0 - a));

   // Distance
   return EARTH_RADIUS_KM * c;
}
