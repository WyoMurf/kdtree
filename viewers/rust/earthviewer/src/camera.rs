use raylib::prelude::*;

use crate::geo::{lon_lat_to_cartesian, EARTH_RADIUS_KM};

pub const MIN_ALTITUDE_KM: f32 = 20.0;
pub const MAX_ALTITUDE_KM: f32 = 150_000.0;
pub const INITIAL_ALTITUDE_KM: f32 = 20_000.0; // ~3.1 Earth radii out: a "respectful distance"

/// (lon, lat, altitude) rather than viewer.c's free-fly scheme, since
/// "start at a respectful distance, move closer" only makes sense with a
/// genuine distance-from-center concept.
pub struct OrbitCamera {
    pub lon: f64,
    pub lat: f64,
    pub altitude: f32,
}

impl OrbitCamera {
    pub fn update(&mut self, rl: &RaylibHandle, camera: &mut Camera3D, delta_time: f32) {
        if rl.is_mouse_button_down(MouseButton::MOUSE_BUTTON_LEFT) {
            let d = rl.get_mouse_delta();
            // Drag sensitivity scales with altitude so panning feels
            // proportional at both a full-globe view and a close-up one.
            let deg_per_pixel = 0.02 * (self.altitude as f64 / EARTH_RADIUS_KM as f64 + 0.05);
            self.lon -= d.x as f64 * deg_per_pixel;
            self.lat += d.y as f64 * deg_per_pixel;
            self.lat = self.lat.clamp(-89.0, 89.0);
            if self.lon > 180.0 {
                self.lon -= 360.0;
            }
            if self.lon < -180.0 {
                self.lon += 360.0;
            }
        }

        let mut zoom = 1.0 - rl.get_mouse_wheel_move() * 0.1;
        if rl.is_key_down(KeyboardKey::KEY_W) || rl.is_key_down(KeyboardKey::KEY_UP) {
            zoom *= 1.0 - 0.8 * delta_time;
        }
        if rl.is_key_down(KeyboardKey::KEY_S) || rl.is_key_down(KeyboardKey::KEY_DOWN) {
            zoom *= 1.0 + 0.8 * delta_time;
        }
        self.altitude = (self.altitude * zoom).clamp(MIN_ALTITUDE_KM, MAX_ALTITUDE_KM);

        let surface_point = lon_lat_to_cartesian(self.lon, self.lat, EARTH_RADIUS_KM);
        let outward = surface_point.normalized();
        camera.position = surface_point + outward * self.altitude;
        camera.target = Vector3::new(0.0, 0.0, 0.0);

        // "Up" is the local north tangent direction, not a blindly-fixed
        // world axis - projecting world-Y onto the tangent plane at the
        // camera's sub-point keeps the view from flipping/degenerating as
        // you orbit around, which a fixed (0,1,0) up would do near the
        // poles. Only undefined exactly at the poles, which the +/-89
        // degree clamp above avoids.
        let world_up = Vector3::new(0.0, 1.0, 0.0);
        let north = world_up - outward * world_up.dot(outward);
        camera.up = north.normalized();
    }
}
