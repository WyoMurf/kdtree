use raylib::prelude::*;

use crate::mem::mem_alloc;

/// City dots are drawn as camera-facing billboard quads, batched into a
/// handful of dynamic Mesh objects and uploaded/drawn with one draw_mesh
/// call per batch, instead of one rlBegin(RL_QUADS)/rlVertex3f call per dot
/// - ~88k individual immediate-mode vertex calls per frame capped the C
/// viewer at ~23 FPS at typical viewing distance (confirmed by disabling
/// dot rendering alone and watching FPS jump to a smooth 60). raylib's
/// Mesh.indices is still a hard 16-bit-per-mesh limit, so quads are split
/// across several fixed-capacity batch meshes the same way the Earth
/// sphere is split into latitude bands - the index buffer (which vertices
/// form which triangles) never changes and is uploaded once at init; only
/// the position buffer is re-uploaded per frame. Color/UV are dropped
/// entirely: every dot is the same flat amber, reproduced by the shared
/// Material's diffuse tint, and an untextured mesh needs no UVs.
pub const DOT_BATCH_MAX_QUADS: i32 = 16384; // 4 verts/quad * 16384 = 65536 = raylib's Mesh.indices (u16) limit, exactly
pub const MAX_VISIBLE_DOTS_PER_FRAME: i32 = 300_000;
pub const MAX_LABELS_PER_FRAME: usize = 4000;

struct DotBatch {
    mesh: Mesh,
    quad_count: i32,
}

pub struct PendingLabel {
    pub world_pos: Vector3,
    pub name: String,
}

/// Holds all per-frame render state: the billboard basis vectors, the dot
/// batches, and this frame's pending labels.
pub struct Renderer {
    pub billboard_right: Vector3,
    pub billboard_up: Vector3,
    pub points_drawn: i32,

    batches: Vec<DotBatch>,
    batches_used: usize,
    dot_material: WeakMaterial,

    pub labels: Vec<PendingLabel>,
}

fn init_dot_batch() -> Mesh {
    let max_verts = DOT_BATCH_MAX_QUADS * 4;
    let max_tris = DOT_BATCH_MAX_QUADS * 2;

    unsafe {
        let vertices = mem_alloc::<f32>(3 * max_verts as usize);
        let indices = mem_alloc::<u16>(3 * max_tris as usize);

        for q in 0..DOT_BATCH_MAX_QUADS {
            let v0 = (q * 4) as u16;
            let base = (q * 6) as isize;
            *indices.offset(base) = v0;
            *indices.offset(base + 1) = v0 + 1;
            *indices.offset(base + 2) = v0 + 2;
            *indices.offset(base + 3) = v0;
            *indices.offset(base + 4) = v0 + 2;
            *indices.offset(base + 5) = v0 + 3;
        }

        let raw = raylib::ffi::Mesh {
            vertexCount: max_verts,
            triangleCount: max_tris, // capacity; per-frame draws lower this to quad_count*2
            vertices,
            texcoords: std::ptr::null_mut(),
            texcoords2: std::ptr::null_mut(),
            normals: std::ptr::null_mut(),
            tangents: std::ptr::null_mut(),
            colors: std::ptr::null_mut(),
            indices,
            animVertices: std::ptr::null_mut(),
            animNormals: std::ptr::null_mut(),
            boneIds: std::ptr::null_mut(),
            boneWeights: std::ptr::null_mut(),
            boneMatrices: std::ptr::null_mut(),
            boneCount: 0,
            vaoId: 0,
            vboId: std::ptr::null_mut(),
        };
        let mut mesh = Mesh::from_raw(raw);
        mesh.upload(true); // dynamic: positions are re-uploaded every frame
        mesh
    }
}

impl Renderer {
    pub fn new(rl: &RaylibHandle, thread: &RaylibThread) -> Self {
        let mut dot_material = rl.load_material_default(thread);
        *dot_material.maps_mut()[ffi::MaterialMapIndex::MATERIAL_MAP_ALBEDO as usize].color_mut() =
            Color::new(255, 210, 90, 255); // warm amber marker dots

        let dot_batch_count =
            (MAX_VISIBLE_DOTS_PER_FRAME + DOT_BATCH_MAX_QUADS - 1) / DOT_BATCH_MAX_QUADS;
        let batches = (0..dot_batch_count)
            .map(|_| DotBatch {
                mesh: init_dot_batch(),
                quad_count: 0,
            })
            .collect();

        Self {
            billboard_right: Vector3::new(1.0, 0.0, 0.0),
            billboard_up: Vector3::new(0.0, 1.0, 0.0),
            points_drawn: 0,
            batches,
            batches_used: 0,
            dot_material,
            labels: Vec::new(),
        }
    }

    /// A fixed world-space size looks fine from far away (shrinks
    /// correctly with perspective) but is far too large up close: real
    /// neighboring towns in a densely-settled region can be closer
    /// together than a generously-sized fixed marker, so their billboards
    /// overlap and merge into a solid blob instead of showing as distinct
    /// dots. Sizing by a roughly-constant angular radius (world size =
    /// angle * distance) keeps markers a sensible, mostly
    /// distance-independent size on screen instead, clamped so it neither
    /// vanishes at planetary range nor balloons absurdly at very low
    /// altitude.
    pub fn city_marker_world_size(dist_km: f32, log10_pop: f32) -> f32 {
        let pop_boost = 1.0 + 0.15 * log10_pop;
        let angular_radius = 0.0009 * pop_boost;
        (angular_radius * dist_km).clamp(0.3, 150.0)
    }

    /// Writes one camera-facing billboard quad directly into the current
    /// batch's vertex buffer (billboard_right/up are recomputed once per
    /// frame, not per dot, in main.rs's per-frame setup).
    pub fn draw_city_point(&mut self, pos: Vector3, size_km: f32) {
        let global_quad_idx = self.points_drawn;
        let batch_idx = (global_quad_idx / DOT_BATCH_MAX_QUADS) as usize;
        if batch_idx >= self.batches.len() {
            return; // hit MAX_VISIBLE_DOTS_PER_FRAME; should never happen at this dataset's scale
        }
        let local_quad_idx = (global_quad_idx % DOT_BATCH_MAX_QUADS) as usize;

        let right = self.billboard_right * size_km;
        let up = self.billboard_up * size_km;
        let p0 = pos - right - up; // bottom-left
        let p1 = pos + right - up; // bottom-right
        let p2 = pos + right + up; // top-right
        let p3 = pos - right + up; // top-left

        let batch = &mut self.batches[batch_idx];
        let verts = batch.mesh.vertices_mut();
        let base = local_quad_idx * 4;
        verts[base] = p0;
        verts[base + 1] = p1;
        verts[base + 2] = p2;
        verts[base + 3] = p3;
        batch.quad_count = local_quad_idx as i32 + 1;
        if batch_idx + 1 > self.batches_used {
            self.batches_used = batch_idx + 1;
        }

        self.points_drawn += 1;
    }

    /// Uploads and draws every batch touched this frame. Only the filled
    /// prefix of each batch's vertex buffer is uploaded (quad_count*4
    /// verts), matching earth_viewer.c's partial UpdateMeshBuffer call.
    pub fn flush_and_draw(&mut self, d3: &mut impl RaylibDraw3D) {
        for batch in &mut self.batches[..self.batches_used] {
            if batch.quad_count == 0 {
                continue;
            }
            let vert_count = (batch.quad_count * 4) as usize;
            let byte_len = vert_count * std::mem::size_of::<Vector3>();
            let bytes = unsafe {
                std::slice::from_raw_parts(
                    batch.mesh.vertices_mut().as_ptr() as *const u8,
                    byte_len,
                )
            };
            unsafe {
                batch.mesh.update_buffer::<Vector3>(0, bytes, 0); // index 0 = position buffer
            }
            batch.mesh.triangleCount = batch.quad_count * 2;
            d3.draw_mesh(&batch.mesh, self.dot_material.clone(), Matrix::identity());
        }
    }

    pub fn reset_frame(&mut self) {
        self.points_drawn = 0;
        self.labels.clear();
        for batch in &mut self.batches[..self.batches_used] {
            batch.quad_count = 0;
        }
        self.batches_used = 0;
    }
}
