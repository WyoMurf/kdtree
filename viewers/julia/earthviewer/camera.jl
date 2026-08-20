const MIN_ALTITUDE_KM = 20.0f0
const MAX_ALTITUDE_KM = 150_000.0f0
const INITIAL_ALTITUDE_KM = 20_000.0f0 # ~3.1 Earth radii out: a "respectful distance"

# (lon, lat, altitude) rather than viewer.c's free-fly scheme, since
# "start at a respectful distance, move closer" only makes sense with a
# genuine distance-from-center concept.
mutable struct OrbitCamera
    lon::Float64
    lat::Float64
    altitude::Float32
end

function update_orbit_camera!(oc::OrbitCamera, camera::Raylib.RayCamera3D, delta_time)
    if Binding.IsMouseButtonDown(Int(Raylib.MOUSE_BUTTON_LEFT))
        d = Binding.GetMouseDelta()
        # Drag sensitivity scales with altitude so panning feels
        # proportional at both a full-globe view and a close-up one.
        deg_per_pixel = 0.02 * (Float64(oc.altitude) / Float64(EARTH_RADIUS_KM) + 0.05)
        oc.lon -= Float64(d[1]) * deg_per_pixel
        oc.lat += Float64(d[2]) * deg_per_pixel
        oc.lat = clamp(oc.lat, -89.0, 89.0)
        oc.lon > 180.0 && (oc.lon -= 360.0)
        oc.lon < -180.0 && (oc.lon += 360.0)
    end

    zoom = 1.0f0 - Binding.GetMouseWheelMove() * 0.1f0
    if Binding.IsKeyDown(Int(Raylib.KEY_W)) || Binding.IsKeyDown(Int(Raylib.KEY_UP))
        zoom *= 1.0f0 - 0.8f0 * delta_time
    end
    if Binding.IsKeyDown(Int(Raylib.KEY_S)) || Binding.IsKeyDown(Int(Raylib.KEY_DOWN))
        zoom *= 1.0f0 + 0.8f0 * delta_time
    end
    oc.altitude = clamp(oc.altitude * zoom, MIN_ALTITUDE_KM, MAX_ALTITUDE_KM)

    surface_point = lon_lat_to_cartesian(oc.lon, oc.lat, EARTH_RADIUS_KM)
    outward = normalize(surface_point)
    # oc.altitude and surface_point are both real km (see geo.jl) - no
    # scaling conversion needed between them.
    camera.position = surface_point + outward * oc.altitude
    camera.target = Raylib.rayvector(0.0, 0.0, 0.0)

    # "Up" is the local north tangent direction, not a blindly-fixed
    # world axis - projecting world-Y onto the tangent plane at the
    # camera's sub-point keeps the view from flipping/degenerating as you
    # orbit around, which a fixed (0,1,0) up would do near the poles.
    # Only undefined exactly at the poles, which the +/-89 degree clamp
    # above avoids.
    world_up = Raylib.rayvector(0.0, 1.0, 0.0)
    north = world_up - outward * dot(world_up, outward)
    camera.up = normalize(north)
end
