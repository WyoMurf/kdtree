use raylib::prelude::*;

pub const EARTH_RADIUS_KM: f32 = 6371.0;

/// Verbatim from earth_viewer.c's function of the same name. Longitude is
/// negated: raylib/OpenGL's right-handed, Y-up world means +sin(lon) for Z
/// maps increasing (eastward) longitude to the geometrically wrong side
/// once actually viewed through a standard look-at camera - the whole
/// globe came out mirrored east-west. Negating flips it back without
/// touching the north/south mapping (driven by Y alone) at all.
pub fn lon_lat_to_cartesian(lon_deg: f64, lat_deg: f64, radius_km: f32) -> Vector3 {
    let lon_rad = (-lon_deg).to_radians();
    let lat_rad = lat_deg.to_radians();
    let cl = lat_rad.cos();
    Vector3::new(
        (radius_km as f64 * cl * lon_rad.cos()) as f32,
        (radius_km as f64 * lat_rad.sin()) as f32,
        (radius_km as f64 * cl * lon_rad.sin()) as f32,
    )
}

/// Hoists the camera-only part of the sphere-horizon visibility test out to
/// once per frame (see ComputeHorizonTest/IsAboveHorizonFast in
/// earth_viewer.c): for a camera outside a sphere of radius R centered at
/// the origin, a surface point is visible iff the angle between its
/// direction from center and the camera's direction from center is within
/// the tangent-line angle to the horizon.
pub struct HorizonTest {
    cam_dir: Vector3,
    cos_threshold: f32,
    all_inside: bool,
}

pub fn compute_horizon_test(cam_pos: Vector3) -> HorizonTest {
    let d = cam_pos.length();
    if d <= EARTH_RADIUS_KM {
        return HorizonTest {
            cam_dir: Vector3::new(0.0, 0.0, 0.0),
            cos_threshold: 0.0,
            all_inside: true, // shouldn't happen in practice
        };
    }
    HorizonTest {
        cam_dir: cam_pos * (1.0 / d),
        cos_threshold: EARTH_RADIUS_KM / d,
        all_inside: false,
    }
}

/// Assumes point_on_sphere already has magnitude EARTH_RADIUS_KM exactly
/// (true for every point lon_lat_to_cartesian(_, _, EARTH_RADIUS_KM)
/// produces), so normalizing it is just a scale by a compile-time-known
/// constant, not a sqrt.
pub fn is_above_horizon_fast(point_on_sphere: Vector3, ht: &HorizonTest) -> bool {
    if ht.all_inside {
        return true;
    }
    let point_dir = point_on_sphere * (1.0 / EARTH_RADIUS_KM);
    point_dir.dot(ht.cam_dir) >= ht.cos_threshold
}

/// A frustum half-space: ax + by + cz + d >= 0 is "inside".
#[derive(Clone, Copy)]
pub struct Plane {
    pub a: f32,
    pub b: f32,
    pub c: f32,
    pub d: f32,
}

/// Gribb-Hartmann plane extraction from a combined view*projection matrix.
/// raylib's Matrix stores rows as (m0,m4,m8,m12), (m1,m5,m9,m13),
/// (m2,m6,m10,m14), (m3,m7,m11,m15); planes are (row3 +/- rowN).
pub fn extract_frustum_planes(m: Matrix) -> [Plane; 6] {
    [
        Plane {
            a: m.m3 + m.m0,
            b: m.m7 + m.m4,
            c: m.m11 + m.m8,
            d: m.m15 + m.m12,
        }, // left
        Plane {
            a: m.m3 - m.m0,
            b: m.m7 - m.m4,
            c: m.m11 - m.m8,
            d: m.m15 - m.m12,
        }, // right
        Plane {
            a: m.m3 + m.m1,
            b: m.m7 + m.m5,
            c: m.m11 + m.m9,
            d: m.m15 + m.m13,
        }, // bottom
        Plane {
            a: m.m3 - m.m1,
            b: m.m7 - m.m5,
            c: m.m11 - m.m9,
            d: m.m15 - m.m13,
        }, // top
        Plane {
            a: m.m3 + m.m2,
            b: m.m7 + m.m6,
            c: m.m11 + m.m10,
            d: m.m15 + m.m14,
        }, // near
        Plane {
            a: m.m3 - m.m2,
            b: m.m7 - m.m6,
            c: m.m11 - m.m10,
            d: m.m15 - m.m14,
        }, // far
    ]
}

pub fn point_in_frustum(fr: &[Plane; 6], p: Vector3) -> bool {
    fr.iter()
        .all(|pl| pl.a * p.x + pl.b * p.y + pl.c * p.z + pl.d >= 0.0)
}

/// The positive-vertex (p-vertex) box/frustum test: for each plane, pick
/// the AABB corner most aligned with the plane's normal. If even that
/// corner is on the negative side, the whole box is outside this plane
/// (and thus the frustum). Tests whether the box itself can overlap the
/// frustum volume, not whether any sampled point lies inside it.
pub fn aabb_outside_frustum(fr: &[Plane; 6], bmin: Vector3, bmax: Vector3) -> bool {
    for pl in fr {
        let px = if pl.a >= 0.0 { bmax.x } else { bmin.x };
        let py = if pl.b >= 0.0 { bmax.y } else { bmin.y };
        let pz = if pl.c >= 0.0 { bmax.z } else { bmin.z };
        if pl.a * px + pl.b * py + pl.c * pz + pl.d < 0.0 {
            return true;
        }
    }
    false
}

/// Samples a grid across a HEALPix cell's lon/lat bounding box (not just
/// its 4 corners - a box much larger than a narrow close-up frustum can
/// have every sampled corner outside the frustum while its true extent
/// still slices through it), builds the Cartesian AABB of those samples,
/// and tests that box against the frustum. This only gates *loading* a
/// tile, not final rendering - every point drawn still gets its own
/// precise per-point horizon+frustum test in draw_tile_points.
pub fn cell_visible(
    lon_min: f64,
    lat_min: f64,
    lon_max: f64,
    lat_max: f64,
    fr: &[Plane; 6],
    ht: &HorizonTest,
) -> bool {
    let mut bmin = Vector3::new(1e9, 1e9, 1e9);
    let mut bmax = Vector3::new(-1e9, -1e9, -1e9);
    let mut any_above_horizon = false;
    const STEPS: i32 = 6;
    for i in 0..=STEPS {
        let lon = lon_min + (lon_max - lon_min) * i as f64 / STEPS as f64;
        for j in 0..=STEPS {
            let lat = lat_min + (lat_max - lat_min) * j as f64 / STEPS as f64;
            let p = lon_lat_to_cartesian(lon, lat, EARTH_RADIUS_KM);
            bmin.x = bmin.x.min(p.x);
            bmax.x = bmax.x.max(p.x);
            bmin.y = bmin.y.min(p.y);
            bmax.y = bmax.y.max(p.y);
            bmin.z = bmin.z.min(p.z);
            bmax.z = bmax.z.max(p.z);
            if is_above_horizon_fast(p, ht) {
                any_above_horizon = true;
            }
        }
    }
    if !any_above_horizon {
        return false;
    }
    !aabb_outside_frustum(fr, bmin, bmax)
}
