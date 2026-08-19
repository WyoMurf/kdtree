# Raylib's row-vector convention (confirmed empirically in
# earthviewer/geo.jl's extract_frustum_planes derivation: translation
# lives in matrix row 4, not column 4) means transforming a vector by a
# raylib Matrix is `transpose(M) * v`, not `M * v` - Raylib.jl's Vector3
# has no rotate-by-axis-angle helper directly, so this reuses the
# library's own MatrixRotate instead of hand-rolling Rodrigues' formula.
function rotate_by_axis_angle(v, axis, angle::Float32)
    m = Binding.MatrixRotate(axis, angle)
    v4 = transpose(m) * vcat(v, 0.0f0) # w=0: a direction, not a point - no translation applied
    return Raylib.rayvector(v4[1], v4[2], v4[3])
end

# A custom camera flying control scheme: WASD/QE to move, click-drag with
# either mouse button to look around, up/down arrows to adjust flight
# speed exponentially. speed is real parsecs/second (matches the HUD
# display in main.jl); camera.position/target are in the scaled
# world-space units every position in this viewer uses (see render.jl's
# WORLD_UNIT_PC comment), so the actual per-frame displacement converts
# speed to that same scale.
function update_free_camera!(camera::Raylib.RayCamera3D, speed::Ref{Float32}, delta_time::Float32)
    if Binding.IsKeyDown(Int(Raylib.KEY_UP))
        speed[] *= 1.05f0
    end
    if Binding.IsKeyDown(Int(Raylib.KEY_DOWN))
        speed[] /= 1.05f0
    end
    speed[] = clamp(speed[], 0.1f0, 1000.0f0)

    distance = norm(camera.target - camera.position)
    forward = (camera.target - camera.position) * (1.0f0 / distance)
    right = normalize(cross(forward, camera.up))

    move = Raylib.rayvector(0.0, 0.0, 0.0)
    Binding.IsKeyDown(Int(Raylib.KEY_W)) && (move = move + forward)
    Binding.IsKeyDown(Int(Raylib.KEY_S)) && (move = move - forward)
    Binding.IsKeyDown(Int(Raylib.KEY_D)) && (move = move + right)
    Binding.IsKeyDown(Int(Raylib.KEY_A)) && (move = move - right)
    Binding.IsKeyDown(Int(Raylib.KEY_E)) && (move = move + camera.up)
    Binding.IsKeyDown(Int(Raylib.KEY_Q)) && (move = move - camera.up)

    if norm(move) > 0.0f0
        displacement = normalize(move) * Float32(speed[] * delta_time / WORLD_UNIT_PC)
        camera.position = camera.position + displacement
        camera.target = camera.target + displacement
    end

    if Binding.IsMouseButtonDown(Int(Raylib.MOUSE_BUTTON_LEFT)) || Binding.IsMouseButtonDown(Int(Raylib.MOUSE_BUTTON_RIGHT))
        mouse_delta = Binding.GetMouseDelta()
        if mouse_delta[1] != 0.0f0 || mouse_delta[2] != 0.0f0
            sensitivity = 0.003f0
            angle_x = -mouse_delta[1] * sensitivity
            angle_y = -mouse_delta[2] * sensitivity

            target_offset = camera.target - camera.position
            target_offset = rotate_by_axis_angle(target_offset, camera.up, angle_x) # yaw
            target_offset = rotate_by_axis_angle(target_offset, right, angle_y)      # pitch

            camera.target = camera.position + target_offset
        end
    end
end
