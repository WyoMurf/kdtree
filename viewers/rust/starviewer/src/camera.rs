use raylib::prelude::*;

/// A custom camera flying control scheme: WASD/QE to move, click-drag with
/// either mouse button to look around, up/down arrows to adjust flight
/// speed exponentially.
pub fn update_free_camera(
    rl: &RaylibHandle,
    camera: &mut Camera3D,
    speed: &mut f32,
    delta_time: f32,
) {
    if rl.is_key_down(KeyboardKey::KEY_UP) {
        *speed *= 1.05;
    }
    if rl.is_key_down(KeyboardKey::KEY_DOWN) {
        *speed /= 1.05;
    }
    *speed = speed.clamp(0.1, 1000.0);

    let distance = (camera.target - camera.position).length();
    let forward = (camera.target - camera.position) * (1.0 / distance);
    let right = forward.cross(camera.up).normalized();

    let mut move_dir = Vector3::new(0.0, 0.0, 0.0);
    if rl.is_key_down(KeyboardKey::KEY_W) {
        move_dir = move_dir + forward;
    }
    if rl.is_key_down(KeyboardKey::KEY_S) {
        move_dir = move_dir - forward;
    }
    if rl.is_key_down(KeyboardKey::KEY_D) {
        move_dir = move_dir + right;
    }
    if rl.is_key_down(KeyboardKey::KEY_A) {
        move_dir = move_dir - right;
    }
    if rl.is_key_down(KeyboardKey::KEY_E) {
        move_dir = move_dir + camera.up;
    }
    if rl.is_key_down(KeyboardKey::KEY_Q) {
        move_dir = move_dir - camera.up;
    }

    if move_dir.length() > 0.0 {
        let displacement = move_dir.normalized() * (*speed * delta_time);
        camera.position = camera.position + displacement;
        camera.target = camera.target + displacement;
    }

    if rl.is_mouse_button_down(MouseButton::MOUSE_BUTTON_LEFT)
        || rl.is_mouse_button_down(MouseButton::MOUSE_BUTTON_RIGHT)
    {
        let mouse_delta = rl.get_mouse_delta();
        if mouse_delta.x != 0.0 || mouse_delta.y != 0.0 {
            const SENSITIVITY: f32 = 0.003;
            let angle_x = -mouse_delta.x * SENSITIVITY;
            let angle_y = -mouse_delta.y * SENSITIVITY;

            // raylib-rs's Vector3 has no rotate-by-axis-angle helper
            // directly, but Vector3::transform_with(Matrix) matches
            // raylib's own Vector3Transform exactly, so building a
            // rotation matrix via Matrix::rotate and transforming through
            // it reuses the library's own rotation math instead of
            // hand-rolling Rodrigues' formula.
            let mut target_offset = camera.target - camera.position;
            target_offset = target_offset.transform_with(Matrix::rotate(camera.up, angle_x)); // yaw
            target_offset = target_offset.transform_with(Matrix::rotate(right, angle_y)); // pitch

            camera.target = camera.position + target_offset;
        }
    }
}
