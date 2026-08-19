# A frustum half-space: ax + by + cz + d >= 0 is "inside". Verbatim from
# viewer.c (earth_viewer.c later copied the same math from here).
struct Plane
    a::Float32
    b::Float32
    c::Float32
    d::Float32
end

# Gribb-Hartmann plane extraction from a combined view*projection matrix.
# See earthviewer/geo.jl's extract_frustum_planes for the empirical
# derivation of this SMatrix column layout (confirmed via MatrixTranslate):
# columns 1..4 are (m0,m4,m8,m12), (m1,m5,m9,m13), (m2,m6,m10,m14),
# (m3,m7,m11,m15) in raylib's own field naming, so each plane is simply
# the last column plus/minus one of the first three.
function extract_frustum_planes(m)
    c1, c2, c3, c4 = m[:, 1], m[:, 2], m[:, 3], m[:, 4]
    mkplane(v) = Plane(v[1], v[2], v[3], v[4])
    return (
        mkplane(c4 + c1), # left
        mkplane(c4 - c1), # right
        mkplane(c4 + c2), # bottom
        mkplane(c4 - c2), # top
        mkplane(c4 + c3), # near
        mkplane(c4 - c3), # far
    )
end

# The positive-vertex (p-vertex) box/frustum test: for each plane, pick
# the AABB corner most aligned with the plane's normal. If even that
# corner is on the negative side, the whole box is outside this plane
# (and thus the frustum).
function aabb_outside_frustum(fr, bmin, bmax)
    for pl in fr
        px = pl.a >= 0.0f0 ? bmax[1] : bmin[1]
        py = pl.b >= 0.0f0 ? bmax[2] : bmin[2]
        pz = pl.c >= 0.0f0 ? bmax[3] : bmin[3]
        pl.a * px + pl.b * py + pl.c * pz + pl.d < 0.0f0 && return true
    end
    return false
end
