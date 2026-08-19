# The per-tile mmap cache, same lazy-load-once-and-keep pattern as
# viewer.c's Shard/EnsureShardLoaded (no eviction - at ~390 tiles totaling
# maybe a few MB, keeping them all mapped for the process lifetime once
# touched isn't a concern the way it was for the Gaia viewer's ~33,500
# star shards).
struct Tile
    nodes::SubArray{KdMmap.Node2F64,1}
    # Cached lon_lat_to_cartesian(lon, lat, EARTH_RADIUS_KM) per node,
    # computed once on load - fixed city data never changes frame to
    # frame, so there's no reason to redo this trig every single frame.
    positions::Vector{Raylib.RayVector3}
end

mutable struct World
    meta_tree::SubArray{KdMmap.Node2F64,1}
    manifest_paths::Vector{String}
    tiles::Vector{Union{Nothing,Tile}}
    tiles_loaded::Int
end

function load_world(dir::AbstractString)
    meta_nodes, _ = KdMmap.open_nodes2f64(joinpath(dir, "cities.metatree"))
    manifest_paths = KdMmap.load_manifest(joinpath(dir, "cities.manifest"))
    if length(manifest_paths) != length(meta_nodes)
        error(
            "cities.manifest has $(length(manifest_paths)) entries, expected $(length(meta_nodes)) " *
            "(doesn't match cities.metatree)",
        )
    end
    tiles = Vector{Union{Nothing,Tile}}(nothing, length(manifest_paths))
    return World(meta_nodes, manifest_paths, tiles, 0)
end

manifest_count(w::World) = length(w.manifest_paths)

# Lazily mmaps the tile at this manifest index (0-based, matching the C
# node.source_id - 1 convention), if it hasn't been tried yet, and
# precomputes every node's Cartesian position.
function ensure_tile_loaded!(w::World, manifest_idx::Integer)
    idx = manifest_idx + 1 # Julia is 1-based
    (idx < 1 || idx > length(w.tiles)) && return nothing
    if w.tiles[idx] === nothing
        try
            nodes, _ = KdMmap.open_nodes2f64(w.manifest_paths[idx])
            positions = Vector{Raylib.RayVector3}(undef, length(nodes))
            for i in eachindex(nodes)
                n = nodes[i]
                lon = (n.size[1] + n.size[3]) / 2.0
                lat = (n.size[2] + n.size[4]) / 2.0
                positions[i] = lon_lat_to_cartesian(lon, lat, EARTH_RADIUS_KM)
            end
            w.tiles[idx] = Tile(nodes, positions)
            w.tiles_loaded += 1
        catch
            return nothing
        end
    end
    return w.tiles[idx]
end

# Draws every point in this tile's already-loaded data that passes its
# own horizon+frustum test, and queues a name label for it if close
# enough - the tile-level cell_visible check only gates whether the tile
# was loaded/scanned at all, so any looseness there never leaks into
# what's actually rendered.
function draw_tile_points!(tile::Tile, fr, ht::HorizonTest, cam_pos, altitude_km, names, r::Renderer)
    for i in eachindex(tile.nodes)
        node = tile.nodes[i]
        node.source_id == 0 && continue
        pos = tile.positions[i]
        is_above_horizon_fast(pos, ht) || continue
        point_in_frustum(fr, pos) || continue

        entry = get(names, node.source_id, nothing)
        population = entry === nothing ? Int64(0) : entry.population
        popf = population > 10 ? Float32(population) : 10.0f0
        log10_pop = log10(popf)
        dist_km = norm(cam_pos - pos)
        draw_city_point!(r, pos, city_marker_world_size(dist_km, log10_pop))

        if entry !== nothing && length(r.labels) < MAX_LABELS_PER_FRAME
            # Steep population curve so labels appear progressively, the
            # way a map app would: tiny villages (~1000 people) only
            # label once you're within ~50km; a capital-sized city labels
            # from several hundred km out.
            label_threshold_km = 60.0f0 * log10_pop - 130.0f0
            if altitude_km < label_threshold_km
                push!(r.labels, PendingLabel(pos, entry.name))
            end
        end
    end
end

# Visits every meta-tree node (each holds exactly one tile's own bounding
# box - there's no subtree-aggregated box without a kd2lod-style
# annotation, which this viewer deliberately doesn't build; at ~390
# entries a full scan every frame is trivial) and draws any tile whose
# box survives cell_visible. idx is a 0-based C-style node index (-1 = no
# node); this is the convention left_child/right_child use throughout.
function walk_meta_tree!(w::World, r::Renderer, idx::Int64, fr, ht::HorizonTest, cam_pos, altitude_km, names)
    idx < 0 && return
    node = w.meta_tree[idx+1]

    if cell_visible(node.size[1], node.size[2], node.size[3], node.size[4], fr, ht)
        manifest_idx = node.source_id - 1
        tile = ensure_tile_loaded!(w, manifest_idx)
        tile !== nothing && draw_tile_points!(tile, fr, ht, cam_pos, altitude_km, names, r)
    end

    walk_meta_tree!(w, r, node.left_child, fr, ht, cam_pos, altitude_km, names)
    walk_meta_tree!(w, r, node.right_child, fr, ht, cam_pos, altitude_km, names)
end
