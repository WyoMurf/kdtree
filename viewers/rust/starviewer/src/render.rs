use raylib::prelude::*;

use kdmmap::Node3I64;

/// Must match fits2kd.c's fixed-point scale (parsecs -> integer units).
pub const SCALE_FACTOR: f64 = 1_000_000_000.0;

/// Hard safety valve: some subtrees (e.g. a parallax segment spanning
/// thousands of parsecs) have a bounding box large enough that
/// angular-size culling alone doesn't collapse them from certain camera
/// positions, which would otherwise make a single frame's traversal
/// unboundedly expensive. Once this many points are drawn in a frame,
/// every further cull call returns immediately - the frame always
/// finishes quickly, and the auto-adapt in main.rs reacts on the next
/// frame.
pub const FRAME_POINT_BUDGET: u64 = 1_500_000;

/// A second, independent safety valve: opening a burst of never-before-seen
/// shard files in a single frame (e.g. flying fast into unexplored
/// territory) blocks on disk I/O for however long that many opens/mmaps
/// take. Capping new loads per frame bounds worst-case frame time the same
/// way FRAME_POINT_BUDGET does for drawing.
pub const MAX_SHARD_LOADS_PER_FRAME: i32 = 64;

/// LOD aggressiveness bounds: a subtree collapses to one representative
/// point once its angular size (as seen from the camera) drops below this
/// many screen pixels. Smaller = more detail + slower, larger = coarser +
/// faster. Tuned live with '[' / ']' and nudged automatically toward a
/// comfortable frame time (see main.rs's loop).
pub const LOD_PIXEL_TARGET_MIN: f32 = 0.25;
pub const LOD_PIXEL_TARGET_MAX: f32 = 64.0;

/// Holds all per-frame render state: the billboard basis vectors and this
/// frame's draw/cull stats.
pub struct Renderer {
    pub billboard_right: Vector3,
    pub billboard_up: Vector3,

    pub points_drawn: u64,
    pub nodes_expanded: u64,
    pub nodes_collapsed: u64,
    pub budget_hit: bool,

    pub star_texture: Texture2D,
}

impl Renderer {
    pub fn new(rl: &mut RaylibHandle, thread: &RaylibThread) -> Self {
        Self {
            billboard_right: Vector3::new(1.0, 0.0, 0.0),
            billboard_up: Vector3::new(0.0, 1.0, 0.0),
            points_drawn: 0,
            nodes_expanded: 0,
            nodes_collapsed: 0,
            budget_hit: false,
            star_texture: create_star_texture(rl, thread),
        }
    }

    pub fn reset_frame(&mut self) {
        self.points_drawn = 0;
        self.nodes_expanded = 0;
        self.nodes_collapsed = 0;
        self.budget_hit = false;
    }

    /// Draws a camera-facing quad (billboarded via billboard_right/up,
    /// recomputed once per frame in main.rs, not per star) textured with
    /// the star texture's soft radial glow - looks round from every
    /// viewing angle and distance, unlike a fixed-axis world-space line
    /// segment. Must be called between rlBegin(RL_QUADS)/rlEnd() with the
    /// star texture bound.
    pub fn draw_star_point(&mut self, pos: Vector3, dist: f32, hidden_count: u32) {
        let (alpha, size) = star_brightness(dist, hidden_count);

        let right = self.billboard_right * size;
        let up = self.billboard_up * size;
        let p0 = pos - right - up; // bottom-left
        let p1 = pos + right - up; // bottom-right
        let p2 = pos + right + up; // top-right
        let p3 = pos - right + up; // top-left

        unsafe {
            ffi::rlColor4ub(255, 245, 218, alpha); // warm yellow-white glowing stars
            ffi::rlTexCoord2f(0.0, 1.0);
            ffi::rlVertex3f(p0.x, p0.y, p0.z);
            ffi::rlTexCoord2f(1.0, 1.0);
            ffi::rlVertex3f(p1.x, p1.y, p1.z);
            ffi::rlTexCoord2f(1.0, 0.0);
            ffi::rlVertex3f(p2.x, p2.y, p2.z);
            ffi::rlTexCoord2f(0.0, 0.0);
            ffi::rlVertex3f(p3.x, p3.y, p3.z);
        }
        self.points_drawn += 1;
    }
}

/// Builds a small soft-edged circular glow sprite: opaque white core
/// fading to fully transparent at the edge. Tinted per-star via
/// rlColor4ub in draw_star_point, so this only supplies the round
/// shape/falloff, not the color itself.
fn create_star_texture(rl: &mut RaylibHandle, thread: &RaylibThread) -> Texture2D {
    let img =
        Image::gen_image_gradient_radial(64, 64, 0.15, Color::WHITE, Color::new(255, 255, 255, 0));
    let mut tex = rl
        .load_texture_from_image(thread, &img)
        .expect("failed to build star texture");
    tex.gen_texture_mipmaps();
    tex.set_texture_filter(thread, TextureFilter::TEXTURE_FILTER_TRILINEAR); // avoids shimmering as distant stars shrink sub-pixel
    tex
}

/// Returns the exact mathematical center from the 3D bounding box [min, max].
pub fn node_star_pos(n: &Node3I64) -> Vector3 {
    Vector3::new(
        ((n.size[0] + n.size[3]) as f64 / (2.0 * SCALE_FACTOR)) as f32,
        ((n.size[1] + n.size[4]) as f64 / (2.0 * SCALE_FACTOR)) as f32,
        ((n.size[2] + n.size[5]) as f64 / (2.0 * SCALE_FACTOR)) as f32,
    )
}

/// A physically-motivated apparent-brightness mapping: closer =
/// brighter/larger. When a single point stands in for a collapsed subtree
/// of hidden_count unresolved stars, brighten/enlarge it a bit so dense
/// regions still read as dense from far away, instead of looking like one
/// dim star.
fn star_brightness(dist: f32, hidden_count: u32) -> (u8, f32) {
    let (mut a, mut sz);

    if dist < 50.0 {
        a = 255u8;
        sz = 0.20f32;
    } else if dist > 3000.0 {
        a = 45;
        sz = 0.01;
    } else {
        let t = (dist - 50.0) / 2950.0;
        a = (255.0 - t * 210.0) as u8;
        sz = 0.20 - t * 0.19;
    }

    if hidden_count > 1 {
        let boost = 20.0 * (hidden_count as f32).log2();
        let boosted = a as i32 + boost as i32;
        a = boosted.min(255) as u8;
        sz += 0.02 * (hidden_count as f32).log2();
    }

    (a, sz)
}
