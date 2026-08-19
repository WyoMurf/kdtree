//! Mmaps the binary .kdtree/.metatree files produced by this project's C
//! tools (geonames2kd, build_city_metatree, fits2kd, ...) and overlays them
//! with Rust structs matching the C layout exactly (see C/include/kdtree.h).
//! Every field in these node structs is already a naturally 8-byte-aligned
//! type in C declaration order, so C's `#pragma pack(1)` has no actual
//! effect on them - `#[repr(C)]` (no reordering, no implicit padding beyond
//! what C would insert, and none is needed here) reproduces the same byte
//! layout with no manual offset math needed. `#[repr(C)]` is required
//! (rather than the Go port's plain structs) because Rust's default
//! `repr(Rust)` layout is free to reorder fields for size optimization.

use memmap2::Mmap;
use std::fs::File;
use std::io;
use std::mem::size_of;
use std::path::Path;

/// Marks the trailing "bounds" record every serialized .kdtree/.metatree
/// file may carry (see kd_serialize in the C library): a node whose
/// source_id is u64::MAX isn't a real item, just whole-tree bounds tacked
/// onto the end. Real node counts always exclude it.
pub const SOURCE_ID_SENTINEL: u64 = u64::MAX;

/// Mirrors kd_2d_f64_mmap_node: a node in a 2D, float64-keyed kd-tree.
/// `size` holds [lonMin, latMin, lonMax, latMax] for city tiles.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Node2F64 {
    pub source_id: u64,
    pub size: [f64; 4],
    pub lo_min_bound: f64,
    pub hi_max_bound: f64,
    pub other_bound: f64,
    pub left_child: i64,
    pub right_child: i64,
}

/// Mirrors kd_3d_64_mmap_node: a node in a 3D, int64-keyed kd-tree. `size`
/// holds [xMin, yMin, zMin, xMax, yMax, zMax] in fixed-point units (see
/// fits2kd.c's SCALE_FACTOR) for Gaia star shards and the star meta-tree.
#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct Node3I64 {
    pub source_id: u64,
    pub size: [i64; 6],
    pub lo_min_bound: i64,
    pub hi_max_bound: i64,
    pub other_bound: i64,
    pub left_child: i64,
    pub right_child: i64,
}

/// LOD sidecar format (produced by kd2lod.c, see C/include/kd2lod.h):
/// record i corresponds exactly to node i in the source .kdtree file, so a
/// viewer can mmap both and use left_child/right_child from the .kdtree
/// file to walk this array directly.
pub const LOD_MAGIC: u32 = 0x32444F4C; // "LOD2" little-endian
pub const LOD_VERSION: u32 = 1;

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct LodHeader {
    pub magic: u32,
    pub version: u32,
    pub source_size: u64, // st_size of the source .kdtree file at annotation time
    pub node_count: u64,  // number of records that follow (excludes the bounds sentinel)
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct LodRecord {
    pub min: [i64; 3],
    pub max: [i64; 3],
    pub count: u32,
    _pad: u32, // kept for exact layout parity with kd2lod_record
}

fn too_short(path: &Path, msg: &str) -> io::Error {
    io::Error::new(
        io::ErrorKind::InvalidData,
        format!("{}: {}", path.display(), msg),
    )
}

/// An mmap'd .kdtree/.metatree file overlaid as [Node2F64]. Excludes the
/// trailing bounds sentinel, if present.
pub struct MappedNodes2F64 {
    mmap: Mmap,
    real_count: usize,
}

impl MappedNodes2F64 {
    pub fn open(path: impl AsRef<Path>) -> io::Result<Self> {
        let path = path.as_ref();
        let file = File::open(path)?;
        let mmap = unsafe { Mmap::map(&file)? };
        if mmap.is_empty() {
            return Err(too_short(path, "empty file"));
        }

        let node_size = size_of::<Node2F64>();
        let total_count = mmap.len() / node_size;
        if total_count == 0 {
            return Err(too_short(
                path,
                "too short for even one node (likely truncated)",
            ));
        }

        let nodes =
            unsafe { std::slice::from_raw_parts(mmap.as_ptr() as *const Node2F64, total_count) };
        let mut real_count = total_count;
        if nodes[total_count - 1].source_id == SOURCE_ID_SENTINEL {
            real_count = total_count - 1;
        }
        if real_count == 0 {
            return Err(too_short(
                path,
                "contains only the bounds sentinel, no real nodes",
            ));
        }

        Ok(Self { mmap, real_count })
    }

    pub fn nodes(&self) -> &[Node2F64] {
        unsafe {
            std::slice::from_raw_parts(self.mmap.as_ptr() as *const Node2F64, self.real_count)
        }
    }

    pub fn source_size(&self) -> u64 {
        self.mmap.len() as u64
    }
}

/// An mmap'd .kdtree/.metatree file overlaid as [Node3I64]. Excludes the
/// trailing bounds sentinel, if present.
pub struct MappedNodes3I64 {
    mmap: Mmap,
    real_count: usize,
}

impl MappedNodes3I64 {
    pub fn open(path: impl AsRef<Path>) -> io::Result<Self> {
        let path = path.as_ref();
        let file = File::open(path)?;
        let mmap = unsafe { Mmap::map(&file)? };
        if mmap.is_empty() {
            return Err(too_short(path, "empty file"));
        }

        let node_size = size_of::<Node3I64>();
        let total_count = mmap.len() / node_size;
        if total_count == 0 {
            return Err(too_short(
                path,
                "too short for even one node (likely truncated)",
            ));
        }

        let nodes =
            unsafe { std::slice::from_raw_parts(mmap.as_ptr() as *const Node3I64, total_count) };
        let mut real_count = total_count;
        if nodes[total_count - 1].source_id == SOURCE_ID_SENTINEL {
            real_count = total_count - 1;
        }
        if real_count == 0 {
            return Err(too_short(
                path,
                "contains only the bounds sentinel, no real nodes",
            ));
        }

        Ok(Self { mmap, real_count })
    }

    pub fn nodes(&self) -> &[Node3I64] {
        unsafe {
            std::slice::from_raw_parts(self.mmap.as_ptr() as *const Node3I64, self.real_count)
        }
    }

    pub fn source_size(&self) -> u64 {
        self.mmap.len() as u64
    }
}

/// An mmap'd .kdtree.lod/.metatree.lod sidecar file.
pub struct MappedLod {
    mmap: Mmap,
    node_count: usize,
}

impl MappedLod {
    /// Mmaps path and validates its header against the source .kdtree/
    /// .metatree file it must exactly match (same size, same node count) -
    /// otherwise it's stale (source was rebuilt without re-running kd2lod)
    /// and must be rejected rather than trusted. Mirrors the validation
    /// LoadOneShard/LoadMetaTree do in viewer.c.
    pub fn open(
        path: impl AsRef<Path>,
        expected_source_size: u64,
        expected_node_count: u64,
    ) -> io::Result<Self> {
        let path = path.as_ref();
        let file = File::open(path)?;
        let mmap = unsafe { Mmap::map(&file)? };

        let header_size = size_of::<LodHeader>();
        if mmap.len() < header_size {
            return Err(too_short(path, "too short for a kd2lod header"));
        }

        let hdr = unsafe { *(mmap.as_ptr() as *const LodHeader) };
        let record_size = size_of::<LodRecord>() as u64;
        let expected_size = header_size as u64 + hdr.node_count * record_size;

        if hdr.magic != LOD_MAGIC
            || hdr.version != LOD_VERSION
            || hdr.source_size != expected_source_size
            || hdr.node_count != expected_node_count
            || mmap.len() as u64 != expected_size
        {
            return Err(too_short(
                path,
                "stale or invalid (doesn't match its source file)",
            ));
        }

        Ok(Self {
            mmap,
            node_count: hdr.node_count as usize,
        })
    }

    pub fn records(&self) -> &[LodRecord] {
        let header_size = size_of::<LodHeader>();
        unsafe {
            std::slice::from_raw_parts(
                self.mmap.as_ptr().add(header_size) as *const LodRecord,
                self.node_count,
            )
        }
    }
}

/// Reads a newline-delimited list of file paths, in the same format the C
/// tools (build_metatree/build_city_metatree) write: line i names the
/// shard/tile file for manifest index i.
pub fn load_manifest(path: impl AsRef<Path>) -> io::Result<Vec<String>> {
    let data = std::fs::read_to_string(path)?;
    Ok(data
        .lines()
        .map(|l| l.trim_end_matches('\r').to_string())
        .collect())
}
