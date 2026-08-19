# This Julia binding (Raylib.jl, via Raylib_jll) is pinned to raylib 4.0,
# which predates rlSetClipPlanes - there is no runtime API to change the
# far clip distance in this build at all, and it's baked in at roughly
# 1000 world units (confirmed empirically: geometry beyond ~1000 units
# from the camera renders as visibly clipped/fragmented, and fully
# vanishes past that). Real km can't be used as world units directly
# (the default camera altitude alone is 20,000 km), so every position fed
# to raylib is scaled down by WORLD_UNIT_KM - chosen so that even the
# worst case (max altitude 150,000 km, looking at the horizon on the far
# side of the globe) stays comfortably under the ~1000-unit ceiling.
# oc.altitude (camera.jl) and the label-threshold math (tiles.jl) stay in
# real, unscaled km throughout - only the actual Vector3 positions handed
# to raylib (this constant, and the altitude-to-position step in
# update_orbit_camera!) go through this conversion.
const WORLD_UNIT_KM = 300.0f0
const EARTH_RADIUS_KM = 6371.0f0 / WORLD_UNIT_KM

# Verbatim from earth_viewer.c's LonLatToCartesian. Longitude is negated:
# raylib/OpenGL's right-handed, Y-up world means +sin(lon) for Z maps
# increasing (eastward) longitude to the geometrically wrong side once
# actually viewed through a standard look-at camera - the whole globe came
# out mirrored east-west. Negating flips it back without touching the
# north/south mapping (driven by Y alone) at all.
function lon_lat_to_cartesian(lon_deg::Real, lat_deg::Real, radius_km::Real)
    lon_rad = deg2rad(-Float64(lon_deg))
    lat_rad = deg2rad(Float64(lat_deg))
    cl = cos(lat_rad)
    return Raylib.rayvector(
        Float64(radius_km) * cl * cos(lon_rad),
        Float64(radius_km) * sin(lat_rad),
        Float64(radius_km) * cl * sin(lon_rad),
    )
end

# Hoists the camera-only part of the sphere-horizon visibility test out to
# once per frame (see ComputeHorizonTest/IsAboveHorizonFast in
# earth_viewer.c): for a camera outside a sphere of radius R centered at
# the origin, a surface point is visible iff the angle between its
# direction from center and the camera's direction from center is within
# the tangent-line angle to the horizon.
struct HorizonTest
    cam_dir::Raylib.RayVector3
    cos_threshold::Float32
    all_inside::Bool
end

function compute_horizon_test(cam_pos)
    d = norm(cam_pos)
    if d <= EARTH_RADIUS_KM
        return HorizonTest(Raylib.rayvector(0.0, 0.0, 0.0), 0.0f0, true) # shouldn't happen in practice
    end
    return HorizonTest(cam_pos * (1.0f0 / d), EARTH_RADIUS_KM / d, false)
end

# Assumes point_on_sphere already has magnitude EARTH_RADIUS_KM exactly
# (true for every point lon_lat_to_cartesian(_, _, EARTH_RADIUS_KM)
# produces), so normalizing it is just a scale by a compile-time-known
# constant, not a sqrt.
function is_above_horizon_fast(point_on_sphere, ht::HorizonTest)
    ht.all_inside && return true
    point_dir = point_on_sphere * (1.0f0 / EARTH_RADIUS_KM)
    return dot(point_dir, ht.cam_dir) >= ht.cos_threshold
end

# A frustum half-space: ax + by + cz + d >= 0 is "inside".
struct Plane
    a::Float32
    b::Float32
    c::Float32
    d::Float32
end

# Gribb-Hartmann plane extraction from a combined view*projection matrix.
# Raylib.jl represents Matrix as an SMatrix{4,4} reinterpreting raylib's
# raw m0..m15 fields; empirically (confirmed via MatrixTranslate) column j
# of that SMatrix is (m[4(j-1)], m[4(j-1)+1], m[4(j-1)+2], m[4(j-1)+3]) in
# raylib's own naming, i.e. columns 1..4 are (m0,m4,m8,m12),
# (m1,m5,m9,m13), (m2,m6,m10,m14), (m3,m7,m11,m15) respectively - so each
# plane is simply the last column plus/minus one of the first three.
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

function point_in_frustum(fr, p)
    for pl in fr
        pl.a * p[1] + pl.b * p[2] + pl.c * p[3] + pl.d < 0.0f0 && return false
    end
    return true
end

# The positive-vertex (p-vertex) box/frustum test: for each plane, pick
# the AABB corner most aligned with the plane's normal. If even that
# corner is on the negative side, the whole box is outside this plane
# (and thus the frustum). Tests whether the box itself can overlap the
# frustum volume, not whether any sampled point lies inside it.
function aabb_outside_frustum(fr, bmin, bmax)
    for pl in fr
        px = pl.a >= 0.0f0 ? bmax[1] : bmin[1]
        py = pl.b >= 0.0f0 ? bmax[2] : bmin[2]
        pz = pl.c >= 0.0f0 ? bmax[3] : bmin[3]
        pl.a * px + pl.b * py + pl.c * pz + pl.d < 0.0f0 && return true
    end
    return false
end

# Samples a grid across a HEALPix cell's lon/lat bounding box (not just
# its 4 corners - a box much larger than a narrow close-up frustum can
# have every sampled corner outside the frustum while its true extent
# still slices through it), builds the Cartesian AABB of those samples,
# and tests that box against the frustum. This only gates *loading* a
# tile, not final rendering - every point drawn still gets its own
# precise per-point horizon+frustum test in draw_tile_points!.
function cell_visible(lon_min, lat_min, lon_max, lat_max, fr, ht::HorizonTest)
    bmin = Raylib.rayvector(1.0e9, 1.0e9, 1.0e9)
    bmax = Raylib.rayvector(-1.0e9, -1.0e9, -1.0e9)
    any_above_horizon = false
    steps = 6
    for i in 0:steps
        lon = lon_min + (lon_max - lon_min) * i / steps
        for j in 0:steps
            lat = lat_min + (lat_max - lat_min) * j / steps
            p = lon_lat_to_cartesian(lon, lat, EARTH_RADIUS_KM)
            bmin = Raylib.rayvector(min(bmin[1], p[1]), min(bmin[2], p[2]), min(bmin[3], p[3]))
            bmax = Raylib.rayvector(max(bmax[1], p[1]), max(bmax[2], p[2]), max(bmax[3], p[3]))
            is_above_horizon_fast(p, ht) && (any_above_horizon = true)
        end
    end
    !any_above_horizon && return false
    return !aabb_outside_frustum(fr, bmin, bmax)
end
