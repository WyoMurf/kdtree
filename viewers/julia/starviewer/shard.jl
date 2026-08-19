# One mmap'd .kdtree shard, plus (if present and valid) the mmap'd
# .kdtree.lod sidecar of per-node subtree bounds/counts produced by
# kd2lod. Shards are mmap'd lazily, the first time the meta-tree walk
# actually visits one - opening/fstat/mmap-ing ~36,900 shard files is
# disk-I/O-bound and was measured to take minutes even at high
# parallelism, long enough for the window manager to call the process
# "not responding".
struct Shard
    nodes::SubArray{KdMmap.Node3I64,1}
    lod::Union{Nothing,Vector{KdMmap.LodRecord}} # nothing if no valid sidecar was found
end

# The meta-tree over every shard file's own bounding box (built by
# build_metatree, annotated by kd2lod exactly like any other .kdtree
# file), plus the lazily-loaded shards themselves.
mutable struct World
    meta_tree::SubArray{KdMmap.Node3I64,1}
    meta_lod::Vector{KdMmap.LodRecord}
    manifest_paths::Vector{String}
    shards::Vector{Union{Nothing,Shard}}

    shards_loaded_count::Int
    stars_discovered::Int
    shard_loads_this_frame::Int
end

# Mmaps catalog.metatree + catalog.metatree.lod, and reads
# catalog.manifest, from the given directory. Unlike a per-shard .lod
# sidecar (optional - see ensure_shard_loaded!'s brute-force fallback),
# the meta-tree's own .lod is required: without it there's no way to
# cull whole shards by bounding box at all.
function load_world(dir::AbstractString)
    meta_tree, meta_tree_size = KdMmap.open_nodes3i64(joinpath(dir, "catalog.metatree"))
    meta_lod = try
        KdMmap.open_lod(joinpath(dir, "catalog.metatree.lod"), meta_tree_size, UInt64(length(meta_tree)))
    catch e
        error("$e\nRun: kd2lod $dir/catalog.metatree $dir/catalog.metatree.lod")
    end

    manifest_paths = KdMmap.load_manifest(joinpath(dir, "catalog.manifest"))
    if length(manifest_paths) != length(meta_tree)
        error(
            "catalog.manifest has $(length(manifest_paths)) entries, expected $(length(meta_tree)) " *
            "(doesn't match catalog.metatree)",
        )
    end

    shards = Vector{Union{Nothing,Shard}}(nothing, length(manifest_paths))
    return World(meta_tree, meta_lod, manifest_paths, shards, 0, 0, 0)
end

manifest_count(w::World) = length(w.manifest_paths)
reset_frame!(w::World) = (w.shard_loads_this_frame = 0)

# Lazily mmaps the shard at this manifest index (0-based, matching the C
# node.source_id - 1 convention), if it hasn't been tried yet, subject to
# the per-frame load cap. Returns nothing if the shard isn't loaded
# (whether because loading failed, or because the attempt is deferred to
# a later frame to stay within the cap) - callers treat nothing exactly
# like "nothing to draw here yet".
#
# Slots start as "not yet attempted" (all `nothing`); unlike the Go/Rust
# ports, this doesn't distinguish "tried and failed" from "never tried"
# per slot, so a shard whose open fails once will be retried on a later
# visit - harmless (a failing open is cheap and rare), and simpler than
# tracking a separate attempted flag.
function ensure_shard_loaded!(w::World, manifest_idx::Integer)
    idx = manifest_idx + 1 # Julia is 1-based
    (idx < 1 || idx > length(w.shards)) && return nothing
    w.shards[idx] !== nothing && return w.shards[idx]
    w.shard_loads_this_frame >= MAX_SHARD_LOADS_PER_FRAME && return nothing # retry next frame
    w.shard_loads_this_frame += 1

    path = w.manifest_paths[idx]
    nodes, nodes_size = try
        KdMmap.open_nodes3i64(path)
    catch
        return nothing
    end

    # Look for a sidecar .kdtree.lod file annotated by kd2lod. Unlike the
    # meta-tree's own .lod, a missing/stale one here just means this
    # shard falls back to brute-force rendering (draw_shard_brute_force!),
    # not a fatal error.
    lod = try
        KdMmap.open_lod("$path.lod", nodes_size, UInt64(length(nodes)))
    catch
        nothing
    end

    w.shards_loaded_count += 1
    w.stars_discovered += lod === nothing ? length(nodes) : Int(lod[1].count)

    shard = Shard(nodes, lod)
    w.shards[idx] = shard
    return shard
end

function box_from_lod(rec::KdMmap.LodRecord)
    bmin = Raylib.rayvector(rec.min[1] / NODE_SCALE, rec.min[2] / NODE_SCALE, rec.min[3] / NODE_SCALE)
    bmax = Raylib.rayvector(rec.max[1] / NODE_SCALE, rec.max[2] / NODE_SCALE, rec.max[3] / NODE_SCALE)
    return bmin, bmax
end

function angular_size_of(bmin, bmax, cam_pos)
    ext = bmax - bmin
    diag = sqrt(ext[1]^2 + ext[2]^2 + ext[3]^2)
    center = (bmin + bmax) * 0.5f0
    center_dist = max(norm(cam_pos - center), 0.001f0)
    return diag / center_dist
end

# The core LOD walk within a single shard. Every node holds a real star,
# so "collapsing" just means: draw this node's own star (boosted to
# represent its whole subtree) and stop, instead of recursing into
# children to draw them individually.
function cull_and_collect!(r::Renderer, shard::Shard, idx::Int64, fr, cam_pos, angle_threshold::Float32)
    idx < 0 && return
    if r.points_drawn >= FRAME_POINT_BUDGET
        r.budget_hit = true
        return
    end

    rec = shard.lod[idx+1]
    bmin, bmax = box_from_lod(rec)
    aabb_outside_frustum(fr, bmin, bmax) && return
    angular_size = angular_size_of(bmin, bmax, cam_pos)

    node = shard.nodes[idx+1]
    star_pos = node_star_pos(node)
    star_dist = norm(cam_pos - star_pos)

    if angular_size < angle_threshold
        draw_star_point!(r, star_pos, star_dist, rec.count)
        r.nodes_collapsed += 1
        return
    end

    draw_star_point!(r, star_pos, star_dist, UInt32(1))
    r.nodes_expanded += 1

    cull_and_collect!(r, shard, node.left_child, fr, cam_pos, angle_threshold)
    cull_and_collect!(r, shard, node.right_child, fr, cam_pos, angle_threshold)
end

# Fallback for shards with no (or a stale/mismatched) .lod sidecar: draw
# every star. Correct but slow - no frustum culling; run kd2lod on the
# shard's .kdtree file to speed it up.
function draw_shard_brute_force!(r::Renderer, shard::Shard, cam_pos)
    for node in shard.nodes
        if r.points_drawn >= FRAME_POINT_BUDGET
            r.budget_hit = true
            return
        end
        node.source_id == 0 && continue
        star_pos = node_star_pos(node)
        dist = norm(cam_pos - star_pos)
        draw_star_point!(r, star_pos, dist, UInt32(1))
    end
end

# Walks the meta-tree exactly like cull_and_collect! walks a shard's
# star-tree, one level up: a meta-tree node's own data is a shard's
# bounding box rather than a star's position. "Expanding" a node lazily
# loads that shard (if not already cached) and walks its real stars via
# cull_and_collect!; "collapsing" draws one representative point standing
# in for every shard hidden in that subtree. Note rec.count here is
# shards hidden, not stars hidden (kd2lod counts tree nodes generically);
# treating it as a brightness proxy is an approximation, since finding
# the true star count would mean opening every shard, defeating the
# point of staying lazy.
function cull_and_collect_meta!(w::World, r::Renderer, idx::Int64, fr, cam_pos, angle_threshold::Float32)
    idx < 0 && return
    if r.points_drawn >= FRAME_POINT_BUDGET
        r.budget_hit = true
        return
    end

    rec = w.meta_lod[idx+1]
    bmin, bmax = box_from_lod(rec)
    aabb_outside_frustum(fr, bmin, bmax) && return
    angular_size = angular_size_of(bmin, bmax, cam_pos)

    meta_node = w.meta_tree[idx+1]

    if angular_size < angle_threshold
        shard_center = node_star_pos(meta_node)
        dist = norm(cam_pos - shard_center)
        draw_star_point!(r, shard_center, dist, rec.count)
        r.nodes_collapsed += 1
        return
    end

    manifest_idx = meta_node.source_id - 1
    shard = ensure_shard_loaded!(w, manifest_idx)
    if shard !== nothing
        if shard.lod !== nothing
            cull_and_collect!(r, shard, Int64(0), fr, cam_pos, angle_threshold)
        else
            draw_shard_brute_force!(r, shard, cam_pos)
        end
    end
    r.nodes_expanded += 1

    cull_and_collect_meta!(w, r, meta_node.left_child, fr, cam_pos, angle_threshold)
    cull_and_collect_meta!(w, r, meta_node.right_child, fr, cam_pos, angle_threshold)
end
