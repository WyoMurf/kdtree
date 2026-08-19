use raylib::prelude::*;

/// A frustum half-space: ax + by + cz + d >= 0 is "inside". Verbatim from
/// viewer.c (earth_viewer.c later copied the same math from here).
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

/// The positive-vertex (p-vertex) box/frustum test: for each plane, pick
/// the AABB corner most aligned with the plane's normal. If even that
/// corner is on the negative side, the whole box is outside this plane
/// (and thus the frustum).
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
