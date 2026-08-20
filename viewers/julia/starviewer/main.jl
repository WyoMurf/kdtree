#!/usr/bin/env julia
# Julia port of C/viewer.c: a two-level kd-tree (catalog.metatree over
# per-shard .kdtree files, produced by fits2kd + build_metatree) of Gaia
# DR3 star positions, flown through with a free-fly camera and rendered
# with frustum-culled, angular-size LOD collapsing. See frustum.jl/
# render.jl/shard.jl/camera.jl for the pieces; this file wires them
# together.
using Raylib
using Raylib.Binding
using LinearAlgebra

include(joinpath(@__DIR__, "..", "kdmmap.jl"))
using .KdMmap

include(joinpath(@__DIR__, "mem.jl"))
include(joinpath(@__DIR__, "frustum.jl"))
include(joinpath(@__DIR__, "render.jl"))
include(joinpath(@__DIR__, "shard.jl"))
include(joinpath(@__DIR__, "camera.jl"))

rl_enable_backface_culling() = Binding.rlEnableBackfaceCulling()
rl_disable_backface_culling() = Binding.rlDisableBackfaceCulling()

function main()
    # catalog.metatree/.metatree.lod/.manifest are produced by
    # build_metatree + kd2lod - a kd-tree over every shard file's own
    # bounding box. The viewer never eagerly opens every shard at
    # startup; it walks this meta-tree per frame and lazily mmaps only
    # the shards whose bounding box actually survives frustum culling.
    if !isfile("catalog.metatree")
        println("Error: catalog.metatree not found in the current directory.")
        println("Build it first (run these from the directory containing your .kdtree shard files):")
        println("  ./build_metatree . catalog")
        println("  ./kd2lod catalog.metatree catalog.metatree.lod")
        exit(1)
    end

    println("Loading meta-index from current directory...")
    local world
    try
        world = load_world(".")
    catch e
        println("Error: ", e)
        exit(1)
    end
    println("Meta-index ready: $(manifest_count(world)) shard files indexed (mmap'd lazily as you fly).")

    println("Launching Raylib 3D Viewer...")
    Binding.SetConfigFlags(Int(Raylib.FLAG_WINDOW_RESIZABLE) | Int(Raylib.FLAG_VSYNC_HINT))
    Raylib.InitWindow(1280, 720, "Gaia 3D Star Catalog Visualizer")

    renderer = new_renderer() # needs a GL context, so after InitWindow

    # Setup camera at the Sun/Earth, looking toward an arbitrary nearby
    # direction. There's no eagerly-computed "average star position" to
    # aim at - the meta-tree covers the whole catalog lazily, and any
    # direction is virtually guaranteed to have solar-neighborhood shards
    # nearby; fly/look around to reveal whatever's actually there.
    camera = Raylib.RayCamera3D(
        Raylib.rayvector(0.0, 0.0, 0.0), # centered at Sun/Earth
        Raylib.rayvector(0.0, 0.0, 20.0),
        Raylib.rayvector(0.0, 1.0, 0.0),
        60.0f0,
        Raylib.CAMERA_PERSPECTIVE,
    )
    speed = Ref(20.0f0) # real parsecs per second

    # SV_CAM_*/SV_TARGET_* override the starting camera position/target,
    # given in real parsecs; SV_SCREENSHOT/SV_SCREENSHOT_FRAME capture and
    # quit - the same env-var-driven smoke-test convention earth_viewer.c
    # established (EV_LON/EV_LAT/EV_ALT/EV_SCREENSHOT), adapted to this
    # viewer's free-fly camera instead of an orbit camera. There's no
    # interactive test harness for a GUI app, so this is the automatable
    # substitute.
    haskey(ENV, "SV_CAM_X") && (camera.position = Raylib.rayvector(parse(Float64, ENV["SV_CAM_X"]), camera.position[2], camera.position[3]))
    haskey(ENV, "SV_CAM_Y") && (camera.position = Raylib.rayvector(camera.position[1], parse(Float64, ENV["SV_CAM_Y"]), camera.position[3]))
    haskey(ENV, "SV_CAM_Z") && (camera.position = Raylib.rayvector(camera.position[1], camera.position[2], parse(Float64, ENV["SV_CAM_Z"])))
    haskey(ENV, "SV_TARGET_X") && (camera.target = Raylib.rayvector(parse(Float64, ENV["SV_TARGET_X"]), camera.target[2], camera.target[3]))
    haskey(ENV, "SV_TARGET_Y") && (camera.target = Raylib.rayvector(camera.target[1], parse(Float64, ENV["SV_TARGET_Y"]), camera.target[3]))
    haskey(ENV, "SV_TARGET_Z") && (camera.target = Raylib.rayvector(camera.target[1], camera.target[2], parse(Float64, ENV["SV_TARGET_Z"])))
    screenshot_path = get(ENV, "SV_SCREENSHOT", "")
    screenshot_frame = parse(Int, get(ENV, "SV_SCREENSHOT_FRAME", "30"))
    frame_count = 0

    Binding.SetTargetFPS(60)
    # Real parsecs throughout: near=0.1pc, far=50,000pc (the catalog's
    # full extent). near_clip/far_clip double as both the *culling*
    # frustum this viewer computes for itself (extract_frustum_planes
    # below) and, via rlSetClipPlanes, raylib's own internal rendering
    # far clip - keeping both consistent matters now that far_clip is
    # well past raylib's old fixed ~1000-world-unit default.
    near_clip, far_clip = 0.1, 50_000.0
    Binding.rlSetClipPlanes(near_clip, far_clip)

    lod_pixel_target = Ref(2.0f0)

    while !Binding.WindowShouldClose()
        frame_count += 1
        delta_time = Binding.GetFrameTime()

        update_free_camera!(camera, speed, delta_time)

        current_width = Binding.GetScreenWidth()
        current_height = Binding.GetScreenHeight()
        aspect = Float32(current_width) / Float32(current_height)

        # Manual LOD tuning: '[' = more detail/slower, ']' = coarser/faster
        Binding.IsKeyDown(Int(Raylib.KEY_LEFT_BRACKET)) && (lod_pixel_target[] *= 1.0f0 - 1.5f0 * delta_time)
        Binding.IsKeyDown(Int(Raylib.KEY_RIGHT_BRACKET)) && (lod_pixel_target[] *= 1.0f0 + 1.5f0 * delta_time)

        # Gentle auto-adaptation toward a comfortable frame time.
        if delta_time > 1.0f0 / 30.0f0
            lod_pixel_target[] *= 1.01f0
        elseif delta_time < 1.0f0 / 55.0f0
            lod_pixel_target[] *= 0.998f0
        end
        lod_pixel_target[] = clamp(lod_pixel_target[], LOD_PIXEL_TARGET_MIN, LOD_PIXEL_TARGET_MAX)

        # Build this frame's view frustum for subtree culling.
        mat_view = Binding.MatrixLookAt(camera.position, camera.target, camera.up)
        mat_proj = Binding.MatrixPerspective(deg2rad(camera.fovy), aspect, Float32(near_clip), Float32(far_clip))
        mat_view_proj = Binding.MatrixMultiply(mat_view, mat_proj)
        frustum = extract_frustum_planes(mat_view_proj)

        # Subtrees collapse once they'd subtend fewer than this many pixels.
        rad_per_pixel = deg2rad(camera.fovy) / Float32(current_height)
        angle_threshold = rad_per_pixel * lod_pixel_target[]

        # Recomputed once per frame (not per star) so every star's
        # billboard faces the camera regardless of view direction.
        cam_forward = normalize(camera.target - camera.position)
        renderer.billboard_right = normalize(cross(cam_forward, camera.up))
        renderer.billboard_up = normalize(cross(renderer.billboard_right, cam_forward))

        reset_frame!(renderer)
        reset_frame!(world)

        Binding.BeginDrawing()
        Binding.ClearBackground(Raylib.BLACK)

        Binding.BeginMode3D(camera)
        # Draw axis reference for spatial awareness (X=Red, Y=Green, Z=Blue).
        Binding.DrawLine3D(Raylib.rayvector(0.0, 0.0, 0.0), Raylib.rayvector(100.0, 0.0, 0.0), Raylib.RED)
        Binding.DrawLine3D(Raylib.rayvector(0.0, 0.0, 0.0), Raylib.rayvector(0.0, 100.0, 0.0), Raylib.GREEN)
        Binding.DrawLine3D(Raylib.rayvector(0.0, 0.0, 0.0), Raylib.rayvector(0.0, 0.0, 100.0), Raylib.BLUE)

        # Walk the meta-tree of shards: cull whole subtrees of shards
        # against the view frustum, collapse distant ones to a single
        # representative point, and lazily mmap + walk only the shards
        # that actually matter this frame. Stars are billboarded quads,
        # so backface culling is disabled for this batch - a quad built
        # from the camera's own right/up vectors always faces the camera.
        rl_disable_backface_culling()
        cull_and_collect_meta!(world, renderer, Int64(0), frustum, camera.position, angle_threshold)
        flush_and_draw!(renderer)
        rl_enable_backface_culling()
        Binding.EndMode3D()

        # If the hard point budget was hit this frame, some subtree's
        # bounding box was too large relative to distance for
        # angular-size culling to collapse it fast enough - jump the LOD
        # threshold up sharply rather than waiting on the gentle
        # auto-adapt above, so it recovers in a frame or two instead of
        # staying pegged at the budget cap.
        renderer.budget_hit && (lod_pixel_target[] *= 1.5f0)

        Binding.DrawFPS(10, 10)
        budget_suffix = renderer.budget_hit ? " [BUDGET CAP HIT]" : ""
        stats_color = renderer.budget_hit ? Raylib.ORANGE : Raylib.GREEN
        Binding.DrawText(
            "Points Drawn This Frame: $(renderer.points_drawn)$budget_suffix (tree nodes: $(renderer.nodes_expanded) expanded / $(renderer.nodes_collapsed) collapsed)",
            10, 35, 18, stats_color,
        )
        Binding.DrawText(
            "Shards mmap'd so far: $(world.shards_loaded_count) / $(manifest_count(world)) indexed | Stars discovered: ~$(world.stars_discovered)",
            10, 58, 16, Raylib.RAYWHITE,
        )
        cp = camera.position
        ct = camera.target
        Binding.DrawText("Cam Position: ($(round(cp[1],digits=1)), $(round(cp[2],digits=1)), $(round(cp[3],digits=1))) pc", 10, 80, 16, Raylib.RAYWHITE)
        Binding.DrawText("Looking at Loaded Sector: ($(round(ct[1],digits=1)), $(round(ct[2],digits=1)), $(round(ct[3],digits=1))) pc", 10, 100, 16, Raylib.RAYWHITE)
        Binding.DrawText("Flight Speed: $(round(speed[],digits=1)) pc/s", 10, 120, 16, Raylib.YELLOW)
        Binding.DrawText("LOD Target: $(round(lod_pixel_target[],digits=2)) px/subtree", 10, 140, 16, Raylib.YELLOW)

        Binding.DrawText("Controls:", 10, current_height - 110, 16, Raylib.SKYBLUE)
        Binding.DrawText("W / S / A / D / Q / E : Fly Forward/Backward/Left/Right/Up/Down", 10, current_height - 90, 14, Raylib.RAYWHITE)
        Binding.DrawText("Mouse Left/Right Click & Drag: Orbit / Look Around", 10, current_height - 70, 14, Raylib.RAYWHITE)
        Binding.DrawText("UP / DOWN Arrow Keys : Adjust flight speed (exponentially)", 10, current_height - 50, 14, Raylib.RAYWHITE)
        Binding.DrawText("[ / ] : More detail (slower) / Coarser detail (faster)", 10, current_height - 30, 14, Raylib.RAYWHITE)

        Binding.EndDrawing()

        if !isempty(screenshot_path) && frame_count == screenshot_frame
            Binding.TakeScreenshot(screenshot_path)
            break
        end
    end

    Raylib.CloseWindow()
    println("Viewer closed successfully.")
end

main()
