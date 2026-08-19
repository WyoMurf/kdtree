use raylib::prelude::*;
use std::collections::HashMap;
use std::io;

use kdmmap::MappedNodes2F64;

use crate::dots::{PendingLabel, Renderer, MAX_LABELS_PER_FRAME};
use crate::geo::{
    cell_visible, is_above_horizon_fast, lon_lat_to_cartesian, point_in_frustum, HorizonTest,
    Plane, EARTH_RADIUS_KM,
};
use crate::names::NameEntry;

/// The per-tile mmap cache, same lazy-load-once-and-keep pattern as
/// viewer.c's Shard/EnsureShardLoaded (no eviction - at ~390 tiles totaling
/// maybe a few MB, keeping them all mapped for the process lifetime once
/// touched isn't a concern the way it was for the Gaia viewer's ~33,500
/// star shards).
struct Tile {
    mapped: MappedNodes2F64,
    // Cached lon_lat_to_cartesian(lon, lat, EARTH_RADIUS_KM) per node,
    // computed once on load - fixed city data never changes frame to
    // frame, so there's no reason to redo this trig every single frame.
    positions: Vec<Vector3>,
}

pub struct World {
    meta_tree: MappedNodes2F64,
    manifest_paths: Vec<String>,
    tiles: Vec<Option<Tile>>,
    pub tiles_loaded: usize,
}

impl World {
    pub fn load(dir: &str) -> io::Result<Self> {
        let meta_tree = MappedNodes2F64::open(format!("{dir}/cities.metatree"))?;
        let manifest_paths = kdmmap::load_manifest(format!("{dir}/cities.manifest"))?;
        if manifest_paths.len() != meta_tree.nodes().len() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidData,
                format!(
                    "cities.manifest has {} entries, expected {} (doesn't match cities.metatree)",
                    manifest_paths.len(),
                    meta_tree.nodes().len()
                ),
            ));
        }

        let tile_count = manifest_paths.len();
        Ok(Self {
            meta_tree,
            manifest_paths,
            tiles: (0..tile_count).map(|_| None).collect(),
            tiles_loaded: 0,
        })
    }

    pub fn manifest_count(&self) -> usize {
        self.manifest_paths.len()
    }

    /// Lazily mmaps the tile at this manifest index, if it hasn't been
    /// tried yet.
    fn ensure_tile_loaded(&mut self, manifest_idx: u64) -> Option<&Tile> {
        let idx = manifest_idx as usize;
        if idx >= self.tiles.len() {
            return None;
        }
        if self.tiles[idx].is_none() {
            if let Ok(mapped) = MappedNodes2F64::open(&self.manifest_paths[idx]) {
                let positions = mapped
                    .nodes()
                    .iter()
                    .map(|n| {
                        let lon = (n.size[0] + n.size[2]) / 2.0;
                        let lat = (n.size[1] + n.size[3]) / 2.0;
                        lon_lat_to_cartesian(lon, lat, EARTH_RADIUS_KM)
                    })
                    .collect();
                self.tiles[idx] = Some(Tile { mapped, positions });
                self.tiles_loaded += 1;
            }
        }
        self.tiles[idx].as_ref()
    }

    /// Visits every meta-tree node (each holds exactly one tile's own
    /// bounding box - there's no subtree-aggregated box without a
    /// kd2lod-style annotation, which this viewer deliberately doesn't
    /// build; at ~390 entries a full scan every frame is trivial) and
    /// draws any tile whose box survives cell_visible.
    pub fn walk_meta_tree(
        &mut self,
        r: &mut Renderer,
        idx: i64,
        fr: &[Plane; 6],
        ht: &HorizonTest,
        cam_pos: Vector3,
        altitude_km: f32,
        names: &HashMap<u64, NameEntry>,
    ) {
        if idx < 0 {
            return;
        }
        let node = self.meta_tree.nodes()[idx as usize];

        if cell_visible(
            node.size[0],
            node.size[1],
            node.size[2],
            node.size[3],
            fr,
            ht,
        ) {
            let manifest_idx = node.source_id - 1;
            if let Some(tile) = self.ensure_tile_loaded(manifest_idx) {
                draw_tile_points(tile, fr, ht, cam_pos, altitude_km, names, r);
            }
        }

        self.walk_meta_tree(r, node.left_child, fr, ht, cam_pos, altitude_km, names);
        self.walk_meta_tree(r, node.right_child, fr, ht, cam_pos, altitude_km, names);
    }
}

/// Draws every point in this tile's already-loaded data that passes its
/// own horizon+frustum test, and queues a name label for it if close
/// enough - the tile-level cell_visible check only gates whether the tile
/// was loaded/scanned at all, so any looseness there never leaks into
/// what's actually rendered.
fn draw_tile_points(
    tile: &Tile,
    fr: &[Plane; 6],
    ht: &HorizonTest,
    cam_pos: Vector3,
    altitude_km: f32,
    names: &HashMap<u64, NameEntry>,
    r: &mut Renderer,
) {
    for (i, node) in tile.mapped.nodes().iter().enumerate() {
        if node.source_id == 0 {
            continue;
        }
        let pos = tile.positions[i];
        if !is_above_horizon_fast(pos, ht) {
            continue;
        }
        if !point_in_frustum(fr, pos) {
            continue;
        }

        let name_entry = names.get(&node.source_id);
        let population = name_entry.map_or(0, |e| e.population);
        let popf = if population > 10 {
            population as f32
        } else {
            10.0
        };
        let log10_pop = popf.log10();
        let dist_km = cam_pos.distance_to(pos);
        r.draw_city_point(pos, Renderer::city_marker_world_size(dist_km, log10_pop));

        if let Some(entry) = name_entry {
            if r.labels.len() < MAX_LABELS_PER_FRAME {
                // Steep population curve so labels appear progressively,
                // the way a map app would: tiny villages (~1000 people)
                // only label once you're within ~50km; a capital-sized
                // city labels from several hundred km out.
                let label_threshold_km = 60.0 * log10_pop - 130.0;
                if altitude_km < label_threshold_km {
                    r.labels.push(PendingLabel {
                        world_pos: pos,
                        name: entry.name.clone(),
                    });
                }
            }
        }
    }
}
