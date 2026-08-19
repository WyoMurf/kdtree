#!/usr/bin/env julia
# Spike: confirms the Raylib.jl toolchain builds and runs cleanly here, and
# exercises the handful of raylib APIs the real viewer ports will need:
# RayCamera3D, rlgl immediate-mode quads, and screenshotting via env vars
# (mirrors test_earth_viewer_visual.sh's EV_SCREENSHOT convention).
#
# Raylib.jl auto-generates its bindings from raylib's own raylib_api.xml/
# raymath_api.xml, which does NOT include rlgl.h (a separate module) - so
# rlBegin/rlVertex3f/rlColor4ub/etc. aren't wrapped by the package. Julia's
# ccall can call any exported symbol in a loaded shared library directly
# with no pre-declared binding needed, so this spike declares its own thin
# wrappers for exactly the rlgl calls the real viewer ports use.
using Raylib
using Raylib_jll

const RL_QUADS = Cint(7)

rl_begin(mode::Integer) = ccall((:rlBegin, Raylib_jll.libraylib), Cvoid, (Cint,), mode)
rl_end() = ccall((:rlEnd, Raylib_jll.libraylib), Cvoid, ())
rl_vertex3f(x, y, z) = ccall((:rlVertex3f, Raylib_jll.libraylib), Cvoid, (Cfloat, Cfloat, Cfloat), x, y, z)
rl_color4ub(r, g, b, a) = ccall((:rlColor4ub, Raylib_jll.libraylib), Cvoid, (Cuchar, Cuchar, Cuchar, Cuchar), r, g, b, a)
rl_disable_backface_culling() = ccall((:rlDisableBackfaceCulling, Raylib_jll.libraylib), Cvoid, ())
rl_enable_backface_culling() = ccall((:rlEnableBackfaceCulling, Raylib_jll.libraylib), Cvoid, ())
rl_push_matrix() = ccall((:rlPushMatrix, Raylib_jll.libraylib), Cvoid, ())
rl_pop_matrix() = ccall((:rlPopMatrix, Raylib_jll.libraylib), Cvoid, ())
rl_rotatef(angle, x, y, z) = ccall((:rlRotatef, Raylib_jll.libraylib), Cvoid, (Cfloat, Cfloat, Cfloat, Cfloat), angle, x, y, z)

function main()
    Raylib.SetConfigFlags(Int(Raylib.FLAG_WINDOW_RESIZABLE) | Int(Raylib.FLAG_VSYNC_HINT))
    Raylib.InitWindow(1280, 720, "Raylib.jl spike: cube + rlgl quad")

    camera = Raylib.RayCamera3D(
        Raylib.rayvector(6.0, 6.0, 6.0),
        Raylib.rayvector(0.0, 0.0, 0.0),
        Raylib.rayvector(0.0, 1.0, 0.0),
        60.0,
        Raylib.CAMERA_PERSPECTIVE,
    )

    Raylib.SetTargetFPS(60)

    screenshot_path = get(ENV, "SPIKE_SCREENSHOT", "")
    screenshot_frame = parse(Int, get(ENV, "SPIKE_SCREENSHOT_FRAME", "30"))
    frame = 0
    angle = 0.0f0

    while !Raylib.WindowShouldClose()
        frame += 1
        angle += Raylib.GetFrameTime() * 45.0f0

        Raylib.BeginDrawing()
        Raylib.ClearBackground(Raylib.BLACK)

        Raylib.BeginMode3D(camera)

        rl_push_matrix()
        rl_rotatef(angle, 0.0, 1.0, 0.0)
        Raylib.DrawCube(Raylib.rayvector(0.0, 0.0, 0.0), 2.0, 2.0, 2.0, Raylib.RED)
        Raylib.DrawCubeWires(Raylib.rayvector(0.0, 0.0, 0.0), 2.0, 2.0, 2.0, Raylib.RAYWHITE)
        rl_pop_matrix()

        # rlgl immediate-mode billboard quad, same shape as DrawStarPoint/
        # DrawCityPoint in the real viewers: a camera-facing quad built
        # from raw rlBegin/rlVertex3f/rlColor4ub calls rather than a
        # helper function.
        rl_disable_backface_culling()
        rl_begin(RL_QUADS)
        rl_color4ub(80, 200, 255, 255)
        rl_vertex3f(-3.0, -1.0, 0.0)
        rl_vertex3f(-1.0, -1.0, 0.0)
        rl_vertex3f(-1.0, 1.0, 0.0)
        rl_vertex3f(-3.0, 1.0, 0.0)
        rl_end()
        rl_enable_backface_culling()

        Raylib.EndMode3D()

        Raylib.DrawFPS(10, 10)
        Raylib.DrawText("Raylib.jl spike: cube + rlgl quad", 10, 35, 18, Raylib.RAYWHITE)
        Raylib.EndDrawing()

        if !isempty(screenshot_path) && frame == screenshot_frame
            Raylib.TakeScreenshot(screenshot_path)
            break
        end
    end

    Raylib.CloseWindow()
    println("Spike closed successfully.")
end

main()
