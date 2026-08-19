// Spike: confirms the raylib-rs toolchain builds and runs cleanly here,
// and exercises the handful of raylib APIs the real viewer ports will need:
// Camera3D, rlgl immediate-mode quads, and screenshotting via env vars
// (mirrors test_earth_viewer_visual.sh's EV_SCREENSHOT convention).
use raylib::prelude::*;
use std::env;

fn main() {
    let (mut rl, thread) = raylib::init()
        .size(1280, 720)
        .resizable()
        .vsync()
        .title("raylib-rs spike: cube + rlgl quad")
        .build();

    let camera = Camera3D::perspective(
        Vector3::new(6.0, 6.0, 6.0),
        Vector3::new(0.0, 0.0, 0.0),
        Vector3::new(0.0, 1.0, 0.0),
        60.0,
    );

    rl.set_target_fps(60);

    let screenshot_path = env::var("SPIKE_SCREENSHOT").ok();
    let screenshot_frame: i32 = env::var("SPIKE_SCREENSHOT_FRAME")
        .ok()
        .and_then(|v| v.parse().ok())
        .unwrap_or(30);
    let mut frame = 0;
    let mut angle: f32 = 0.0;

    while !rl.window_should_close() {
        frame += 1;
        angle += rl.get_frame_time() * 45.0;

        let mut d = rl.begin_drawing(&thread);
        d.clear_background(Color::BLACK);

        {
            let mut d3 = d.begin_mode3D(camera);

            unsafe {
                raylib::ffi::rlPushMatrix();
                raylib::ffi::rlRotatef(angle, 0.0, 1.0, 0.0);
            }
            d3.draw_cube(Vector3::new(0.0, 0.0, 0.0), 2.0, 2.0, 2.0, Color::RED);
            d3.draw_cube_wires(Vector3::new(0.0, 0.0, 0.0), 2.0, 2.0, 2.0, Color::RAYWHITE);
            unsafe {
                raylib::ffi::rlPopMatrix();
            }

            // rlgl immediate-mode billboard quad, same shape as
            // DrawStarPoint/DrawCityPoint in the real viewers: a
            // camera-facing quad built from raw rlBegin/rlVertex3f/
            // rlColor4ub calls rather than a helper function.
            unsafe {
                raylib::ffi::rlDisableBackfaceCulling();
                raylib::ffi::rlBegin(raylib::ffi::RL_QUADS as i32);
                raylib::ffi::rlColor4ub(80, 200, 255, 255);
                raylib::ffi::rlVertex3f(-3.0, -1.0, 0.0);
                raylib::ffi::rlVertex3f(-1.0, -1.0, 0.0);
                raylib::ffi::rlVertex3f(-1.0, 1.0, 0.0);
                raylib::ffi::rlVertex3f(-3.0, 1.0, 0.0);
                raylib::ffi::rlEnd();
                raylib::ffi::rlEnableBackfaceCulling();
            }
        }

        d.draw_fps(10, 10);
        d.draw_text(
            "raylib-rs spike: cube + rlgl quad",
            10,
            35,
            18,
            Color::RAYWHITE,
        );
        drop(d);

        if let Some(path) = &screenshot_path {
            if frame == screenshot_frame {
                rl.take_screenshot(&thread, path);
                break;
            }
        }
    }

    println!("Spike closed successfully.");
}
