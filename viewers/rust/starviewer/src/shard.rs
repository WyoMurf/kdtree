use raylib::prelude::*;
use std::io;

use kdmmap::{LodRecord, MappedLod, MappedNodes3I64};

use crate::frustum::{aabb_outside_frustum, Plane};
use crate::render::{
    node_star_pos, Renderer, FRAME_POINT_BUDGET, MAX_SHARD_LOADS_PER_FRAME, SCALE_FACTOR,
};

/// One mmap'd .kdtree shard, plus (if present and valid) the mmap'd
/// .kdtree.lod sidecar of per-node subtree bounds/counts produced by
/// kd2lod. Shards are mmap'd lazily, the first time the meta-tree walk
/// actually visits one - opening/fstat/mmap-ing ~36,900 shard files is
/// disk-I/O-bound and was measured to take minutes even at high
/// parallelism, long enough for the window manager to call the process
/// "not responding".
struct Shard {
    mapped: MappedNodes3I64,
    lod: Option<MappedLod>, // None if no valid sidecar was found
}

/// The meta-tree over every shard file's own bounding box (built by
/// build_metatree, annotated by kd2lod exactly like any other .kdtree
/// file), plus the lazily-loaded shards themselves.
pub struct World {
    meta_tree: MappedNodes3I64,
    meta_lod: MappedLod,
    manifest_paths: Vec<String>,
    shards: Vec<Option<Shard>>,

    pub shards_loaded_count: u64,
    pub stars_discovered: u64,
    shard_loads_this_frame: i32,
}

impl World {
    /// Mmaps catalog.metatree + catalog.metatree.lod, and reads
    /// catalog.manifest, from the given directory. Unlike a per-shard
    /// .lod sidecar (optional - see ensure_shard_loaded's brute-force
    /// fallback), the meta-tree's own .lod is required: without it
    /// there's no way to cull whole shards by bounding box at all.
    pub fn load(dir: &str) -> io::Result<Self> {
        let meta_tree = MappedNodes3I64::open(format!("{dir}/catalog.metatree"))?;
        let meta_lod = MappedLod::open(
            format!("{dir}/catalog.metatree.lod"),
            meta_tree.source_size(),
            meta_tree.nodes().len() as u64,
        )
        .map_err(|e| {
            io::Error::new(
                e.kind(),
                format!("{e}\nRun: kd2lod {dir}/catalog.metatree {dir}/catalog.metatree.lod"),
            )
        })?;

        let manifest_paths = kdmmap::load_manifest(format!("{dir}/catalog.manifest"))?;
        if manifest_paths.len() != meta_tree.nodes().len() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!(
                    "catalog.manifest has {} entries, expected {} (doesn't match catalog.metatree)",
                    manifest_paths.len(),
                    meta_tree.nodes().len()
                ),
            ));
        }

        let shard_count = manifest_paths.len();
        Ok(Self {
            meta_tree,
            meta_lod,
            manifest_paths,
            shards: (0..shard_count).map(|_| None).collect(),
            shards_loaded_count: 0,
            stars_discovered: 0,
            shard_loads_this_frame: 0,
        })
    }

    pub fn manifest_count(&self) -> usize {
        self.manifest_paths.len()
    }

    pub fn reset_frame(&mut self) {
        self.shard_loads_this_frame = 0;
    }

    /// Lazily mmaps the shard at this manifest index, if it hasn't been
    /// tried yet, subject to the per-frame load cap. Returns None if the
    /// shard isn't loaded (whether because loading failed, or because the
    /// attempt is deferred to a later frame to stay within the cap) -
    /// callers treat None exactly like "nothing to draw here yet".
    ///
    /// Slots start as "not yet attempted" (`Vec<Option<Shard>>` = all
    /// `None`); unlike the Go/C ports, this doesn't distinguish "tried and
    /// failed" from "never tried" per slot, so a shard whose open fails
    /// once will be retried on a later visit - harmless (a failing open is
    /// cheap and rare), and simpler than tracking a separate attempted flag.
    fn ensure_shard_loaded(&mut self, manifest_idx: u64) -> Option<&Shard> {
        let idx = manifest_idx as usize;
        if idx >= self.shards.len() {
            return None;
        }
        if self.shards[idx].is_some() {
            return self.shards[idx].as_ref();
        }
        if self.shard_loads_this_frame >= MAX_SHARD_LOADS_PER_FRAME {
            return None; // retry next frame
        }
        self.shard_loads_this_frame += 1;

        let path = &self.manifest_paths[idx];
        let mapped = MappedNodes3I64::open(path).ok()?;

        // Look for a sidecar .kdtree.lod file annotated by kd2lod. Unlike
        // the meta-tree's own .lod, a missing/stale one here just means
        // this shard falls back to brute-force rendering
        // (draw_shard_brute_force), not a fatal error.
        let lod = MappedLod::open(
            format!("{path}.lod"),
            mapped.source_size(),
            mapped.nodes().len() as u64,
        )
        .ok();

        self.shards_loaded_count += 1;
        self.stars_discovered += match &lod {
            Some(l) => l.records()[0].count as u64,
            None => mapped.nodes().len() as u64,
        };

        self.shards[idx] = Some(Shard { mapped, lod });
        self.shards[idx].as_ref()
    }

    /// Walks the meta-tree exactly like cull_and_collect walks a shard's
    /// star-tree, one level up: a meta-tree node's own data is a shard's
    /// bounding box rather than a star's position. "Expanding" a node
    /// lazily loads that shard (if not already cached) and walks its real
    /// stars via cull_and_collect; "collapsing" draws one representative
    /// point standing in for every shard hidden in that subtree. Note
    /// rec.count here is shards hidden, not stars hidden (kd2lod counts
    /// tree nodes generically); treating it as a brightness proxy is an
    /// approximation, since finding the true star count would mean
    /// opening every shard, defeating the point of staying lazy.
    pub fn cull_and_collect_meta(
        &mut self,
        r: &mut Renderer,
        idx: i64,
        fr: &[Plane; 6],
        cam_pos: Vector3,
        angle_threshold: f32,
    ) {
        if idx < 0 {
            return;
        }
        if r.points_drawn >= FRAME_POINT_BUDGET {
            r.budget_hit = true;
            return;
        }

        let rec = self.meta_lod.records()[idx as usize];
        let (bmin, bmax) = box_from_lod(&rec);
        if aabb_outside_frustum(fr, bmin, bmax) {
            return;
        }
        let angular_size = angular_size_of(bmin, bmax, cam_pos);

        let meta_node = self.meta_tree.nodes()[idx as usize];

        if angular_size < angle_threshold {
            let shard_center = node_star_pos(&meta_node);
            let dist = cam_pos.distance_to(shard_center);
            r.draw_star_point(shard_center, dist, rec.count);
            r.nodes_collapsed += 1;
            return;
        }

        let manifest_idx = meta_node.source_id - 1;
        if let Some(shard) = self.ensure_shard_loaded(manifest_idx) {
            if shard.lod.is_some() {
                cull_and_collect(r, shard, 0, fr, cam_pos, angle_threshold);
            } else {
                draw_shard_brute_force(r, shard, cam_pos);
            }
        }
        r.nodes_expanded += 1;

        self.cull_and_collect_meta(r, meta_node.left_child, fr, cam_pos, angle_threshold);
        self.cull_and_collect_meta(r, meta_node.right_child, fr, cam_pos, angle_threshold);
    }
}

fn box_from_lod(rec: &LodRecord) -> (Vector3, Vector3) {
    let bmin = Vector3::new(
        (rec.min[0] as f64 / SCALE_FACTOR) as f32,
        (rec.min[1] as f64 / SCALE_FACTOR) as f32,
        (rec.min[2] as f64 / SCALE_FACTOR) as f32,
    );
    let bmax = Vector3::new(
        (rec.max[0] as f64 / SCALE_FACTOR) as f32,
        (rec.max[1] as f64 / SCALE_FACTOR) as f32,
        (rec.max[2] as f64 / SCALE_FACTOR) as f32,
    );
    (bmin, bmax)
}

fn angular_size_of(bmin: Vector3, bmax: Vector3, cam_pos: Vector3) -> f32 {
    let ext = bmax - bmin;
    let diag = (ext.x * ext.x + ext.y * ext.y + ext.z * ext.z).sqrt();
    let center = (bmin + bmax) * 0.5;
    let center_dist = cam_pos.distance_to(center).max(0.001);
    diag / center_dist
}

/// The core LOD walk within a single shard. Every node holds a real star,
/// so "collapsing" just means: draw this node's own star (boosted to
/// represent its whole subtree) and stop, instead of recursing into
/// children to draw them individually.
fn cull_and_collect(
    r: &mut Renderer,
    shard: &Shard,
    idx: i64,
    fr: &[Plane; 6],
    cam_pos: Vector3,
    angle_threshold: f32,
) {
    if idx < 0 {
        return;
    }
    if r.points_drawn >= FRAME_POINT_BUDGET {
        r.budget_hit = true;
        return;
    }

    let lod = shard
        .lod
        .as_ref()
        .expect("cull_and_collect requires a shard with a valid .lod sidecar");
    let rec = lod.records()[idx as usize];
    let (bmin, bmax) = box_from_lod(&rec);
    if aabb_outside_frustum(fr, bmin, bmax) {
        return;
    }
    let angular_size = angular_size_of(bmin, bmax, cam_pos);

    let node = shard.mapped.nodes()[idx as usize];
    let star_pos = node_star_pos(&node);
    let star_dist = cam_pos.distance_to(star_pos);

    if angular_size < angle_threshold {
        r.draw_star_point(star_pos, star_dist, rec.count);
        r.nodes_collapsed += 1;
        return;
    }

    r.draw_star_point(star_pos, star_dist, 1);
    r.nodes_expanded += 1;

    cull_and_collect(r, shard, node.left_child, fr, cam_pos, angle_threshold);
    cull_and_collect(r, shard, node.right_child, fr, cam_pos, angle_threshold);
}

/// Fallback for shards with no (or a stale/mismatched) .lod sidecar: draw
/// every star. Correct but slow - no frustum culling; run kd2lod on the
/// shard's .kdtree file to speed it up.
fn draw_shard_brute_force(r: &mut Renderer, shard: &Shard, cam_pos: Vector3) {
    for node in shard.mapped.nodes() {
        if r.points_drawn >= FRAME_POINT_BUDGET {
            r.budget_hit = true;
            return;
        }
        if node.source_id == 0 {
            continue;
        }
        let star_pos = node_star_pos(node);
        let dist = cam_pos.distance_to(star_pos);
        r.draw_star_point(star_pos, dist, 1);
    }
}
