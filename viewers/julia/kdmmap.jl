# Mmaps the binary .kdtree/.metatree files produced by this project's C
# tools (geonames2kd, build_city_metatree, fits2kd, ...) and overlays them
# with Julia structs matching the C layout exactly (see C/include/kdtree.h).
# Every field in these node structs is already a naturally 8-byte-aligned
# type in C declaration order, and Julia (like Go, unlike Rust) preserves
# struct field order/layout as declared with no reordering, so no manual
# offset math or repr(C)-style annotation is needed - just matching field
# order and using NTuple for the fixed-size C arrays (which Julia stores
# inline, same as a C array member).
module KdMmap

using Mmap

export Node2F64, Node3I64, LodHeader, LodRecord, SOURCE_ID_SENTINEL,
       open_nodes2f64, open_nodes3i64, open_lod, load_manifest

# Marks the trailing "bounds" record every serialized .kdtree/.metatree
# file may carry (see kd_serialize in the C library): a node whose
# source_id is typemax(UInt64) isn't a real item, just whole-tree bounds
# tacked onto the end. Real node counts always exclude it.
const SOURCE_ID_SENTINEL = typemax(UInt64)

# Mirrors kd_2d_f64_mmap_node: a node in a 2D, float64-keyed kd-tree.
# size holds (lonMin, latMin, lonMax, latMax) for city tiles.
struct Node2F64
    source_id::UInt64
    size::NTuple{4,Float64}
    lo_min_bound::Float64
    hi_max_bound::Float64
    other_bound::Float64
    left_child::Int64
    right_child::Int64
end

# Mirrors kd_3d_64_mmap_node: a node in a 3D, int64-keyed kd-tree. size
# holds (xMin, yMin, zMin, xMax, yMax, zMax) in fixed-point units (see
# fits2kd.c's SCALE_FACTOR) for Gaia star shards and the star meta-tree.
struct Node3I64
    source_id::UInt64
    size::NTuple{6,Int64}
    lo_min_bound::Int64
    hi_max_bound::Int64
    other_bound::Int64
    left_child::Int64
    right_child::Int64
end

# LOD sidecar format (produced by kd2lod.c, see C/include/kd2lod.h):
# record i corresponds exactly to node i in the source .kdtree file, so a
# viewer can mmap both and use left_child/right_child from the .kdtree
# file to walk this array directly.
const LOD_MAGIC = UInt32(0x32444F4C) # "LOD2" little-endian
const LOD_VERSION = UInt32(1)

struct LodHeader
    magic::UInt32
    version::UInt32
    source_size::UInt64 # st_size of the source .kdtree file at annotation time
    node_count::UInt64  # number of records that follow (excludes the bounds sentinel)
end

struct LodRecord
    min::NTuple{3,Int64}
    max::NTuple{3,Int64}
    count::UInt32
    _pad::UInt32 # kept for exact layout parity with kd2lod_record
end

# open_nodes mmaps path read-only and overlays it as a Vector{T} of node
# records (T is Node2F64 or Node3I64), excluding the trailing bounds
# sentinel if present. Returns (nodes_view, raw_file_size) - the raw size
# is needed by callers to validate a shard/meta-tree's .lod sidecar
# against it (see open_lod).
function open_nodes(::Type{T}, path::AbstractString) where {T}
    sz = filesize(path)
    sz == 0 && error("$path: empty file")

    node_size = sizeof(T)
    total_count = div(sz, node_size)
    total_count == 0 && error("$path: too short for even one node (likely truncated)")

    io = open(path, "r")
    nodes = Mmap.mmap(io, Vector{T}, total_count)
    close(io) # mapping stays valid after the descriptor is closed

    real_count = total_count
    if nodes[total_count].source_id == SOURCE_ID_SENTINEL
        real_count = total_count - 1
    end
    real_count == 0 && error("$path: contains only the bounds sentinel, no real nodes")

    return view(nodes, 1:real_count), UInt64(sz)
end

open_nodes2f64(path::AbstractString) = open_nodes(Node2F64, path)
open_nodes3i64(path::AbstractString) = open_nodes(Node3I64, path)

# open_lod mmaps path and validates its header against the source .kdtree/
# .metatree file it must exactly match (same size, same node count) -
# otherwise it's stale (source was rebuilt without re-running kd2lod) and
# must be rejected rather than trusted. Mirrors the validation
# LoadOneShard/LoadMetaTree do in viewer.c.
function open_lod(path::AbstractString, expected_source_size::UInt64, expected_node_count::UInt64)
    sz = filesize(path)
    header_size = sizeof(LodHeader)
    sz < header_size && error("$path: too short for a kd2lod header")

    io = open(path, "r")
    hdr = Mmap.mmap(io, Vector{LodHeader}, 1)[1]

    record_size = sizeof(LodRecord)
    expected_size = header_size + hdr.node_count * record_size

    if hdr.magic != LOD_MAGIC || hdr.version != LOD_VERSION ||
       hdr.source_size != expected_source_size || hdr.node_count != expected_node_count ||
       sz != expected_size
        close(io)
        error("$path: stale or invalid (doesn't match its source file)")
    end

    seek(io, header_size)
    records = Mmap.mmap(io, Vector{LodRecord}, hdr.node_count)
    close(io)
    return records
end

# Reads a newline-delimited list of file paths, in the same format the C
# tools (build_metatree/build_city_metatree) write: line i names the
# shard/tile file for manifest index i.
load_manifest(path::AbstractString) = readlines(path)

end # module
