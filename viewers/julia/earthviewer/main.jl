#!/usr/bin/env julia
# Julia port of C/earth_viewer.c: a two-layer kd-tree (cities.metatree over
# city_tile_*.kdtree HEALPix-cell files, produced by geonames2kd +
# build_city_metatree) rendered as points on Earth's sphere, with an orbit
# camera and proximity-gated name labels. See geo.jl/sphere.jl/names.jl/
# tiles.jl/dots.jl/camera.jl for the pieces; this file wires them together.
using Raylib
using Raylib.Binding
using LinearAlgebra

include(joinpath(@__DIR__, "..", "kdmmap.jl"))
using .KdMmap

include(joinpath(@__DIR__, "geo.jl"))
include(joinpath(@__DIR__, "sphere.jl"))
include(joinpath(@__DIR__, "names.jl"))
include(joinpath(@__DIR__, "dots.jl"))
include(joinpath(@__DIR__, "tiles.jl"))
include(joinpath(@__DIR__, "camera.jl"))

const EARTH_TEXTURE_PATH = "earth_daymap.jpg"

rl_enable_backface_culling() = Binding.rlEnableBackfaceCulling()
rl_disable_backface_culling() = Binding.rlDisableBackfaceCulling()

function main()
    if !isfile("cities.metatree")
        println("Error: cities.metatree not found in the current directory.")
        println("Build it first (see README-cities.md):")
        println("  ./geonames2kd cities1000.txt")
        println("  ./build_city_metatree . cities")
        exit(1)
    end

    println("Loading city meta-index...")
    local world
    try
        world = load_world(".")
    catch e
        println("Error: ", e)
        exit(1)
    end
    println("Meta-index ready: $(manifest_count(world)) tiles indexed.")

    names = load_names("cities.names")

    Binding.SetConfigFlags(Int(Raylib.FLAG_WINDOW_RESIZABLE) | Int(Raylib.FLAG_VSYNC_HINT))
    Raylib.InitWindow(1280, 720, "Earth Cities Viewer")

    renderer = new_renderer() # needs a GL context, so after InitWindow

    # LoadTexture needs a GL context too. Textured Earth is optional -
    # fall back to a plain sphere if the file isn't there or fails to load.
    have_earth_texture = false
    earth_meshes = Raylib.RayMesh[]
    earth_material = nothing

    if isfile(EARTH_TEXTURE_PATH)
        texture = Binding.LoadTexture(EARTH_TEXTURE_PATH)
        if texture.id != 0
            # LoadTexture defaults to GL_NEAREST with no mipmaps - fine
            # for a flat-color sphere, but blocky/aliased up close for a
            # real texture. Mipmaps + trilinear filtering fix both.
            texture_ref = Ref(texture)
            Binding.GenTextureMipmaps(texture_ref)
            texture = texture_ref[]
            Binding.SetTextureFilter(texture, Int(Raylib.TEXTURE_FILTER_TRILINEAR))

            meshes = build_earth_meshes(EARTH_RADIUS_KM, EARTH_SPHERE_RINGS, EARTH_SPHERE_SLICES)
            material = Binding.LoadMaterialDefault()
            material_ref = Ref(material)
            Binding.SetMaterialTexture(material_ref, Int(Raylib.MATERIAL_MAP_ALBEDO), texture)
            material = material_ref[]

            println("Loaded Earth texture: $EARTH_TEXTURE_PATH ($(length(meshes)) mesh band$(length(meshes) == 1 ? "" : "s"))")

            earth_meshes = meshes
            earth_material = material
            have_earth_texture = true
        else
            println("Warning: found $EARTH_TEXTURE_PATH but couldn't load it as a texture; using a plain sphere.")
        end
    else
        println("No $EARTH_TEXTURE_PATH found; using a plain sphere (see README-cities.md to add a real texture).")
    end

    # Start over Cody, Wyoming (44.52634 N, 109.05653 W, per cities1000.txt's
    # own entry for it) rather than an arbitrary point. EV_LON/EV_LAT/EV_ALT
    # override the starting camera position; EV_SCREENSHOT/
    # EV_SCREENSHOT_FRAME capture and quit - mirrors
    # test_earth_viewer_visual.sh's env-var-driven smoke-test convention,
    # since there's no interactive test harness for a GUI app.
    oc = OrbitCamera(-109.05653, 44.52634, INITIAL_ALTITUDE_KM)
    haskey(ENV, "EV_LON") && (oc.lon = parse(Float64, ENV["EV_LON"]))
    haskey(ENV, "EV_LAT") && (oc.lat = parse(Float64, ENV["EV_LAT"]))
    haskey(ENV, "EV_ALT") && (oc.altitude = parse(Float32, ENV["EV_ALT"]))
    screenshot_path = get(ENV, "EV_SCREENSHOT", "")
    screenshot_frame = parse(Int, get(ENV, "EV_SCREENSHOT_FRAME", "30"))
    frame_count = 0

    camera = Raylib.RayCamera3D(
        Raylib.rayvector(0.0, 0.0, 0.0),
        Raylib.rayvector(0.0, 0.0, 0.0),
        Raylib.rayvector(0.0, 1.0, 0.0),
        60.0f0,
        Raylib.CAMERA_PERSPECTIVE,
    )

    Binding.SetTargetFPS(60)
    # Real km throughout: near=3km, far=600,000km (comfortably past the
    # worst case of max altitude 150,000km looking at the far horizon).
    # near_clip/far_clip double as both the *culling* frustum this viewer
    # computes for itself (extract_frustum_planes below) and, via
    # rlSetClipPlanes, raylib's own internal rendering far clip - keeping
    # both consistent matters now that far_clip is well past raylib's old
    # fixed ~1000-world-unit default.
    near_clip, far_clip = 3.0, 600_000.0
    Binding.rlSetClipPlanes(near_clip, far_clip)

    while !Binding.WindowShouldClose()
        frame_count += 1
        delta_time = Binding.GetFrameTime()
        update_orbit_camera!(oc, camera, delta_time)

        current_width = Binding.GetScreenWidth()
        current_height = Binding.GetScreenHeight()
        aspect = Float32(current_width) / Float32(current_height)

        mat_view = Binding.MatrixLookAt(camera.position, camera.target, camera.up)
        mat_proj = Binding.MatrixPerspective(deg2rad(camera.fovy), aspect, Float32(near_clip), Float32(far_clip))
        mat_view_proj = Binding.MatrixMultiply(mat_view, mat_proj)
        frustum = extract_frustum_planes(mat_view_proj)
        horizon_test = compute_horizon_test(camera.position)

        cam_forward = normalize(camera.target - camera.position)
        renderer.billboard_right = normalize(cross(cam_forward, camera.up))
        renderer.billboard_up = normalize(cross(renderer.billboard_right, cam_forward))

        reset_frame!(renderer)

        # Walking the meta-tree is pure CPU work (mmap reads + writing
        # into the renderer's pre-allocated mesh buffers) - it needs no
        # drawing context, so it happens before BeginDrawing.
        walk_meta_tree!(world, renderer, Int64(0), frustum, horizon_test, camera.position, oc.altitude, names)

        # Labels are plain 2D screen text, drawn after EndMode3D via
        # GetWorldToScreen - every position here already passed the same
        # horizon+frustum test as its dot, so it's reliably in front of
        # the camera.
        label_screens = [(Binding.GetWorldToScreen(l.world_pos, camera), Binding.MeasureText(l.name, 14), l.name) for l in renderer.labels]

        Binding.BeginDrawing()
        Binding.ClearBackground(Raylib.raycolor(5, 5, 15, 255))

        Binding.BeginMode3D(camera)
        # Backface culling stays on for the Earth mesh itself - see
        # sphere.jl's gen_earth_sphere_mesh_band comment on triangle
        # winding.
        rl_enable_backface_culling()
        if have_earth_texture
            for m in earth_meshes
                Binding.DrawMesh(m, earth_material, Binding.MatrixIdentity())
            end
        else
            Binding.DrawSphere(Raylib.rayvector(0.0, 0.0, 0.0), EARTH_RADIUS_KM, Raylib.raycolor(25, 60, 95, 255))
            Binding.DrawSphereWires(Raylib.rayvector(0.0, 0.0, 0.0), EARTH_RADIUS_KM * 1.001f0, 18, 36, Raylib.raycolor(255, 255, 255, 40))
        end

        # City-dot billboards are camera-facing quads, not a wound mesh -
        # backface culling has to stay off for them regardless of the
        # Earth mesh's winding.
        rl_disable_backface_culling()
        flush_and_draw!(renderer)
        rl_enable_backface_culling()
        Binding.EndMode3D()

        for (sp, tw, name) in label_screens
            Binding.DrawRectangle(Int(round(sp[1])) - 2, Int(round(sp[2])) - 2, tw + 4, 16, Raylib.raycolor(0, 0, 0, 140))
            Binding.DrawText(name, Int(round(sp[1])), Int(round(sp[2])), 14, Raylib.RAYWHITE)
        end

        Binding.DrawFPS(10, 10)
        Binding.DrawText("Points drawn this frame: $(renderer.points_drawn)", 10, 35, 18, Raylib.GREEN)
        Binding.DrawText(
            "Camera: lon=$(round(oc.lon, digits=2)) lat=$(round(oc.lat, digits=2)) altitude=$(round(Int, oc.altitude)) km",
            10, 58, 16, Raylib.RAYWHITE,
        )
        Binding.DrawText("Tiles mmap'd so far: $(world.tiles_loaded) / $(manifest_count(world))", 10, 80, 16, Raylib.RAYWHITE)
        Binding.DrawText("Controls: drag with left mouse to orbit, scroll/W-S to zoom", 10, current_height - 30, 14, Raylib.SKYBLUE)

        Binding.EndDrawing()

        if !isempty(screenshot_path) && frame_count == screenshot_frame
            Binding.TakeScreenshot(screenshot_path)
            break
        end
    end

    if have_earth_texture
        for m in earth_meshes
            Binding.UnloadMesh(m)
        end
        Binding.UnloadMaterial(earth_material) # also unloads earth_material's texture
    end
    for batch in renderer.batches
        Binding.UnloadMesh(batch.mesh)
    end
    Binding.UnloadMaterial(renderer.dot_material)

    Raylib.CloseWindow()
    println("Viewer closed successfully.")
end

main()
