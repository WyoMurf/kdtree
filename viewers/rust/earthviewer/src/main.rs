// Rust port of C/earth_viewer.c: a two-layer kd-tree (cities.metatree over
// city_tile_*.kdtree HEALPix-cell files, produced by geonames2kd +
// build_city_metatree) rendered as points on Earth's sphere, with an orbit
// camera and proximity-gated name labels. See geo.rs/sphere.rs/names.rs/
// tiles.rs/dots.rs/camera.rs for the pieces; this file wires them together.
mod camera;
mod dots;
mod geo;
mod mem;
mod names;
mod sphere;
mod tiles;

use raylib::prelude::*;
use std::env;
use std::path::Path;

use camera::{OrbitCamera, INITIAL_ALTITUDE_KM};
use dots::Renderer;
use geo::{compute_horizon_test, extract_frustum_planes, EARTH_RADIUS_KM};
use tiles::World;

const EARTH_TEXTURE_PATH: &str = "earth_daymap.jpg";

fn main() {
    if !Path::new("cities.metatree").exists() {
        println!("Error: cities.metatree not found in the current directory.");
        println!("Build it first (see README-cities.md):");
        println!("  ./geonames2kd cities1000.txt");
        println!("  ./build_city_metatree . cities");
        std::process::exit(1);
    }

    println!("Loading city meta-index...");
    let mut world = match World::load(".") {
        Ok(w) => w,
        Err(e) => {
            println!("Error: {e}");
            std::process::exit(1);
        }
    };
    println!(
        "Meta-index ready: {} tiles indexed.",
        world.manifest_count()
    );

    let names = names::load_names("cities.names");

    let (mut rl, thread) = raylib::init()
        .size(1280, 720)
        .resizable()
        .vsync()
        .title("Earth Cities Viewer")
        .build();

    let mut renderer = Renderer::new(&rl, &thread); // needs a GL context, so after init

    // LoadTexture needs a GL context too. Textured Earth is optional -
    // fall back to a plain sphere if the file isn't there or fails to load.
    let mut have_earth_texture = false;
    let mut earth_meshes: Vec<Mesh> = Vec::new();
    let mut earth_material: Option<WeakMaterial> = None;
    let mut earth_texture: Option<Texture2D> = None;

    if Path::new(EARTH_TEXTURE_PATH).exists() {
        match rl.load_texture(&thread, EARTH_TEXTURE_PATH) {
            Ok(mut texture) => {
                // load_texture defaults to GL_NEAREST with no mipmaps -
                // fine for a flat-color sphere, but blocky/aliased up
                // close for a real texture. Mipmaps + trilinear filtering
                // fix both.
                texture.gen_texture_mipmaps();
                texture.set_texture_filter(&thread, TextureFilter::TEXTURE_FILTER_TRILINEAR);

                let meshes = sphere::build_earth_meshes(
                    EARTH_RADIUS_KM,
                    sphere::EARTH_SPHERE_RINGS,
                    sphere::EARTH_SPHERE_SLICES,
                );
                let mut material = rl.load_material_default(&thread);
                material.set_material_texture(ffi::MaterialMapIndex::MATERIAL_MAP_ALBEDO, &texture);

                println!(
                    "Loaded Earth texture: {EARTH_TEXTURE_PATH} ({} mesh band{})",
                    meshes.len(),
                    if meshes.len() == 1 { "" } else { "s" }
                );

                earth_meshes = meshes;
                earth_material = Some(material);
                earth_texture = Some(texture);
                have_earth_texture = true;
            }
            Err(_) => {
                println!(
                    "Warning: found {EARTH_TEXTURE_PATH} but couldn't load it as a texture; using a plain sphere."
                );
            }
        }
    } else {
        println!("No {EARTH_TEXTURE_PATH} found; using a plain sphere (see README-cities.md to add a real texture).");
    }

    // Start over Cody, Wyoming (44.52634 N, 109.05653 W, per cities1000.txt's
    // own entry for it) rather than an arbitrary point. EV_LON/EV_LAT/EV_ALT
    // override the starting camera position; EV_SCREENSHOT/
    // EV_SCREENSHOT_FRAME capture and quit - mirrors
    // test_earth_viewer_visual.sh's env-var-driven smoke-test convention,
    // since there's no interactive test harness for a GUI app.
    let mut oc = OrbitCamera {
        lon: -109.05653,
        lat: 44.52634,
        altitude: INITIAL_ALTITUDE_KM,
    };
    if let Ok(v) = env::var("EV_LON") {
        if let Ok(f) = v.parse() {
            oc.lon = f;
        }
    }
    if let Ok(v) = env::var("EV_LAT") {
        if let Ok(f) = v.parse() {
            oc.lat = f;
        }
    }
    if let Ok(v) = env::var("EV_ALT") {
        if let Ok(f) = v.parse::<f32>() {
            oc.altitude = f;
        }
    }
    let screenshot_path = env::var("EV_SCREENSHOT").ok();
    let screenshot_frame: i32 = env::var("EV_SCREENSHOT_FRAME")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(30);
    let mut frame_count = 0;

    let mut camera = Camera3D::perspective(
        Vector3::new(0.0, 0.0, 0.0),
        Vector3::new(0.0, 0.0, 0.0),
        Vector3::new(0.0, 1.0, 0.0),
        60.0,
    );

    rl.set_target_fps(60);
    const NEAR_CLIP: f64 = 1.0;
    const FAR_CLIP: f64 = 300_000.0;
    unsafe {
        ffi::rlSetClipPlanes(NEAR_CLIP, FAR_CLIP);
    }

    while !rl.window_should_close() {
        frame_count += 1;
        let delta_time = rl.get_frame_time();
        oc.update(&rl, &mut camera, delta_time);

        let current_width = rl.get_screen_width();
        let current_height = rl.get_screen_height();
        let aspect = current_width as f32 / current_height as f32;

        let mat_view = Matrix::look_at(camera.position, camera.target, camera.up);
        let mat_proj = Matrix::perspective(
            camera.fovy.to_radians(),
            aspect,
            NEAR_CLIP as f32,
            FAR_CLIP as f32,
        );
        let mat_view_proj = mat_view * mat_proj;
        let frustum = extract_frustum_planes(mat_view_proj);
        let horizon_test = compute_horizon_test(camera.position);

        let cam_forward = (camera.target - camera.position).normalized();
        renderer.billboard_right = cam_forward.cross(camera.up).normalized();
        renderer.billboard_up = renderer.billboard_right.cross(cam_forward).normalized();

        renderer.reset_frame();

        // Walking the meta-tree is pure CPU work (mmap reads + writing
        // into the renderer's pre-allocated mesh buffers) - it needs no
        // drawing context, so it happens before begin_drawing borrows `rl`
        // mutably. That leaves `rl` free for get_world_to_screen/
        // measure_text just below, without a borrow conflict against the
        // active RaylibDrawHandle.
        world.walk_meta_tree(
            &mut renderer,
            0,
            &frustum,
            &horizon_test,
            camera.position,
            oc.altitude,
            &names,
        );

        // Labels are plain 2D screen text, drawn after mode3D ends via
        // get_world_to_screen - every position here already passed the
        // same horizon+frustum test as its dot, so it's reliably in front
        // of the camera. Screen positions/widths are computed here (while
        // `rl` is still only borrowed immutably) and drawn later.
        let label_screens: Vec<(Vector2, i32, String)> = renderer
            .labels
            .iter()
            .map(|label| {
                let sp = rl.get_world_to_screen(label.world_pos, camera);
                let tw = rl.measure_text(&label.name, 14);
                (sp, tw, label.name.clone())
            })
            .collect();

        let mut d = rl.begin_drawing(&thread);
        d.clear_background(Color::new(5, 5, 15, 255));

        {
            let mut d3 = d.begin_mode3D(camera);
            // Backface culling stays on for the Earth mesh itself - see
            // sphere.rs's gen_earth_sphere_mesh_band comment on triangle
            // winding.
            unsafe {
                ffi::rlEnableBackfaceCulling();
            }
            if have_earth_texture {
                for m in &earth_meshes {
                    d3.draw_mesh(m, earth_material.clone().unwrap(), Matrix::identity());
                }
            } else {
                d3.draw_sphere(
                    Vector3::new(0.0, 0.0, 0.0),
                    EARTH_RADIUS_KM,
                    Color::new(25, 60, 95, 255),
                );
                d3.draw_sphere_wires(
                    Vector3::new(0.0, 0.0, 0.0),
                    EARTH_RADIUS_KM * 1.001,
                    18,
                    36,
                    Color::new(255, 255, 255, 40),
                );
            }

            // City-dot billboards are camera-facing quads, not a wound
            // mesh - backface culling has to stay off for them regardless
            // of the Earth mesh's winding.
            unsafe {
                ffi::rlDisableBackfaceCulling();
            }
            renderer.flush_and_draw(&mut d3);
            unsafe {
                ffi::rlEnableBackfaceCulling();
            }
        }

        for (sp, tw, name) in &label_screens {
            d.draw_rectangle(
                sp.x as i32 - 2,
                sp.y as i32 - 2,
                tw + 4,
                16,
                Color::new(0, 0, 0, 140),
            );
            d.draw_text(name, sp.x as i32, sp.y as i32, 14, Color::RAYWHITE);
        }

        d.draw_fps(10, 10);
        d.draw_text(
            &format!("Points drawn this frame: {}", renderer.points_drawn),
            10,
            35,
            18,
            Color::GREEN,
        );
        d.draw_text(
            &format!(
                "Camera: lon={:.2} lat={:.2} altitude={:.0} km",
                oc.lon, oc.lat, oc.altitude
            ),
            10,
            58,
            16,
            Color::RAYWHITE,
        );
        d.draw_text(
            &format!(
                "Tiles mmap'd so far: {} / {}",
                world.tiles_loaded,
                world.manifest_count()
            ),
            10,
            80,
            16,
            Color::RAYWHITE,
        );
        d.draw_text(
            "Controls: drag with left mouse to orbit, scroll/W-S to zoom",
            10,
            current_height - 30,
            14,
            Color::SKYBLUE,
        );

        drop(d);

        // Screenshot is taken relative to the process's working directory
        // regardless of what path is given - mirrors
        // test_earth_viewer_visual.sh's convention.
        if let Some(path) = &screenshot_path {
            if frame_count == screenshot_frame {
                rl.take_screenshot(&thread, path);
                break;
            }
        }
    }

    let _ = earth_texture; // keeps the GPU texture alive until here; drops (UnloadTexture) now
    println!("Viewer closed successfully.");
}
