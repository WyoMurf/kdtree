// Rust port of C/viewer.c: a two-level kd-tree (catalog.metatree over
// per-shard .kdtree files, produced by fits2kd + build_metatree) of Gaia
// DR3 star positions, flown through with a free-fly camera and rendered
// with frustum-culled, angular-size LOD collapsing. See frustum.rs/
// render.rs/shard.rs/camera.rs for the pieces; this file wires them
// together.
mod camera;
mod frustum;
mod render;
mod shard;

use raylib::prelude::*;
use std::env;
use std::path::Path;

use frustum::extract_frustum_planes;
use render::{Renderer, LOD_PIXEL_TARGET_MAX, LOD_PIXEL_TARGET_MIN};
use shard::World;

fn main() {
    // catalog.metatree/.metatree.lod/.manifest are produced by
    // build_metatree + kd2lod - a kd-tree over every shard file's own
    // bounding box. The viewer never eagerly opens every shard at
    // startup; it walks this meta-tree per frame and lazily mmaps only
    // the shards whose bounding box actually survives frustum culling.
    if !Path::new("catalog.metatree").exists() {
        println!("Error: catalog.metatree not found in the current directory.");
        println!(
            "Build it first (run these from the directory containing your .kdtree shard files):"
        );
        println!("  ./build_metatree . catalog");
        println!("  ./kd2lod catalog.metatree catalog.metatree.lod");
        std::process::exit(1);
    }

    println!("Loading meta-index from current directory...");
    let mut world = match World::load(".") {
        Ok(w) => w,
        Err(e) => {
            println!("Error: {e}");
            std::process::exit(1);
        }
    };
    println!(
        "Meta-index ready: {} shard files indexed (mmap'd lazily as you fly).",
        world.manifest_count()
    );

    println!("Launching Raylib 3D Viewer...");
    let (mut rl, thread) = raylib::init()
        .size(1280, 720)
        .resizable()
        .vsync()
        .title("Gaia 3D Star Catalog Visualizer")
        .build();

    let mut renderer = Renderer::new(&mut rl, &thread); // needs a GL context, so after init

    // Setup camera at the Sun/Earth, looking toward an arbitrary nearby
    // direction. There's no eagerly-computed "average star position" to
    // aim at - the meta-tree covers the whole catalog lazily, and any
    // direction is virtually guaranteed to have solar-neighborhood shards
    // nearby; fly/look around to reveal whatever's actually there.
    let mut camera = Camera3D::perspective(
        Vector3::new(0.0, 0.0, 0.0), // centered at Sun/Earth
        Vector3::new(0.0, 0.0, 20.0),
        Vector3::new(0.0, 1.0, 0.0),
        60.0,
    );
    let mut speed: f32 = 20.0; // parsecs per second

    // SV_CAM_*/SV_TARGET_* override the starting camera position/target;
    // SV_SCREENSHOT/SV_SCREENSHOT_FRAME capture and quit - the same
    // env-var-driven smoke-test convention earth_viewer.c established
    // (EV_LON/EV_LAT/EV_ALT/EV_SCREENSHOT), adapted to this viewer's
    // free-fly camera instead of an orbit camera. There's no interactive
    // test harness for a GUI app, so this is the automatable substitute.
    if let Ok(v) = env::var("SV_CAM_X") {
        if let Ok(f) = v.parse() {
            camera.position.x = f;
        }
    }
    if let Ok(v) = env::var("SV_CAM_Y") {
        if let Ok(f) = v.parse() {
            camera.position.y = f;
        }
    }
    if let Ok(v) = env::var("SV_CAM_Z") {
        if let Ok(f) = v.parse() {
            camera.position.z = f;
        }
    }
    if let Ok(v) = env::var("SV_TARGET_X") {
        if let Ok(f) = v.parse() {
            camera.target.x = f;
        }
    }
    if let Ok(v) = env::var("SV_TARGET_Y") {
        if let Ok(f) = v.parse() {
            camera.target.y = f;
        }
    }
    if let Ok(v) = env::var("SV_TARGET_Z") {
        if let Ok(f) = v.parse() {
            camera.target.z = f;
        }
    }
    let screenshot_path = env::var("SV_SCREENSHOT").ok();
    let screenshot_frame: i32 = env::var("SV_SCREENSHOT_FRAME")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(30);
    let mut frame_count = 0;

    rl.set_target_fps(60);

    // Custom near/far clip planes so distant stars up to 50,000 pc are visible.
    const NEAR_CLIP: f64 = 0.1;
    const FAR_CLIP: f64 = 50_000.0;
    unsafe {
        ffi::rlSetClipPlanes(NEAR_CLIP, FAR_CLIP);
    }

    let mut lod_pixel_target: f32 = 2.0;

    while !rl.window_should_close() {
        frame_count += 1;
        let delta_time = rl.get_frame_time();

        camera::update_free_camera(&rl, &mut camera, &mut speed, delta_time);

        let current_width = rl.get_screen_width();
        let current_height = rl.get_screen_height();
        let aspect = current_width as f32 / current_height as f32;

        // Manual LOD tuning: '[' = more detail/slower, ']' = coarser/faster
        if rl.is_key_down(KeyboardKey::KEY_LEFT_BRACKET) {
            lod_pixel_target *= 1.0 - 1.5 * delta_time;
        }
        if rl.is_key_down(KeyboardKey::KEY_RIGHT_BRACKET) {
            lod_pixel_target *= 1.0 + 1.5 * delta_time;
        }

        // Gentle auto-adaptation toward a comfortable frame time.
        if delta_time > 1.0 / 30.0 {
            lod_pixel_target *= 1.01;
        } else if delta_time < 1.0 / 55.0 {
            lod_pixel_target *= 0.998;
        }
        lod_pixel_target = lod_pixel_target.clamp(LOD_PIXEL_TARGET_MIN, LOD_PIXEL_TARGET_MAX);

        // Build this frame's view frustum for subtree culling.
        let mat_view = Matrix::look_at(camera.position, camera.target, camera.up);
        let mat_proj = Matrix::perspective(
            camera.fovy.to_radians(),
            aspect,
            NEAR_CLIP as f32,
            FAR_CLIP as f32,
        );
        let mat_view_proj = mat_view * mat_proj;
        let frustum = extract_frustum_planes(mat_view_proj);

        // Subtrees collapse once they'd subtend fewer than this many pixels.
        let rad_per_pixel = (camera.fovy.to_radians()) / current_height as f32;
        let angle_threshold = rad_per_pixel * lod_pixel_target;

        // Recomputed once per frame (not per star) so every star's
        // billboard faces the camera regardless of view direction.
        let cam_forward = (camera.target - camera.position).normalized();
        renderer.billboard_right = cam_forward.cross(camera.up).normalized();
        renderer.billboard_up = renderer.billboard_right.cross(cam_forward).normalized();

        renderer.reset_frame();
        world.reset_frame();

        let mut d = rl.begin_drawing(&thread);
        d.clear_background(Color::BLACK);

        {
            let mut d3 = d.begin_mode3D(camera);
            // Draw axis reference for spatial awareness (X=Red, Y=Green, Z=Blue).
            d3.draw_line_3D(
                Vector3::new(0.0, 0.0, 0.0),
                Vector3::new(100.0, 0.0, 0.0),
                Color::RED,
            );
            d3.draw_line_3D(
                Vector3::new(0.0, 0.0, 0.0),
                Vector3::new(0.0, 100.0, 0.0),
                Color::GREEN,
            );
            d3.draw_line_3D(
                Vector3::new(0.0, 0.0, 0.0),
                Vector3::new(0.0, 0.0, 100.0),
                Color::BLUE,
            );

            // Walk the meta-tree of shards: cull whole subtrees of shards
            // against the view frustum, collapse distant ones to a single
            // representative point, and lazily mmap + walk only the
            // shards that actually matter this frame. Stars are
            // billboarded quads, so backface culling is disabled for this
            // batch - a quad built from the camera's own right/up vectors
            // always faces the camera.
            unsafe {
                ffi::rlDisableBackfaceCulling();
                ffi::rlSetTexture(renderer.star_texture.id);
                ffi::rlBegin(ffi::RL_QUADS as i32);
            }
            world.cull_and_collect_meta(
                &mut renderer,
                0,
                &frustum,
                camera.position,
                angle_threshold,
            );
            unsafe {
                ffi::rlEnd();
                ffi::rlSetTexture(ffi::rlGetTextureIdDefault());
                ffi::rlEnableBackfaceCulling();
            }
        }

        // If the hard point budget was hit this frame, some subtree's
        // bounding box was too large relative to distance for
        // angular-size culling to collapse it fast enough - jump the LOD
        // threshold up sharply rather than waiting on the gentle
        // auto-adapt above, so it recovers in a frame or two instead of
        // staying pegged at the budget cap.
        if renderer.budget_hit {
            lod_pixel_target *= 1.5;
        }

        d.draw_fps(10, 10);
        let (budget_suffix, stats_color) = if renderer.budget_hit {
            (" [BUDGET CAP HIT]", Color::ORANGE)
        } else {
            ("", Color::GREEN)
        };
        d.draw_text(
            &format!(
                "Points Drawn This Frame: {}{budget_suffix} (tree nodes: {} expanded / {} collapsed)",
                renderer.points_drawn, renderer.nodes_expanded, renderer.nodes_collapsed
            ),
            10,
            35,
            18,
            stats_color,
        );
        d.draw_text(
            &format!(
                "Shards mmap'd so far: {} / {} indexed | Stars discovered: ~{}",
                world.shards_loaded_count,
                world.manifest_count(),
                world.stars_discovered
            ),
            10,
            58,
            16,
            Color::RAYWHITE,
        );
        d.draw_text(
            &format!(
                "Cam Position: ({:.1}, {:.1}, {:.1}) pc",
                camera.position.x, camera.position.y, camera.position.z
            ),
            10,
            80,
            16,
            Color::RAYWHITE,
        );
        d.draw_text(
            &format!(
                "Looking at Loaded Sector: ({:.1}, {:.1}, {:.1}) pc",
                camera.target.x, camera.target.y, camera.target.z
            ),
            10,
            100,
            16,
            Color::RAYWHITE,
        );
        d.draw_text(
            &format!("Flight Speed: {speed:.1} pc/s"),
            10,
            120,
            16,
            Color::YELLOW,
        );
        d.draw_text(
            &format!("LOD Target: {lod_pixel_target:.2} px/subtree"),
            10,
            140,
            16,
            Color::YELLOW,
        );

        d.draw_text("Controls:", 10, current_height - 110, 16, Color::SKYBLUE);
        d.draw_text(
            "W / S / A / D / Q / E : Fly Forward/Backward/Left/Right/Up/Down",
            10,
            current_height - 90,
            14,
            Color::RAYWHITE,
        );
        d.draw_text(
            "Mouse Left/Right Click & Drag: Orbit / Look Around",
            10,
            current_height - 70,
            14,
            Color::RAYWHITE,
        );
        d.draw_text(
            "UP / DOWN Arrow Keys : Adjust flight speed (exponentially)",
            10,
            current_height - 50,
            14,
            Color::RAYWHITE,
        );
        d.draw_text(
            "[ / ] : More detail (slower) / Coarser detail (faster)",
            10,
            current_height - 30,
            14,
            Color::RAYWHITE,
        );

        drop(d);

        if let Some(path) = &screenshot_path {
            if frame_count == screenshot_frame {
                rl.take_screenshot(&thread, path);
                break;
            }
        }
    }

    println!("Viewer closed successfully.");
}
