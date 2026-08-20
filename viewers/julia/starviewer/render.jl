# Must match fits2kd.c's fixed-point scale (parsecs -> integer units).
const SCALE_FACTOR = 1.0e9

# This Julia binding (Raylib.jl, via Raylib_jll) is pinned to raylib 4.0,
# which predates rlSetClipPlanes - there is no runtime API to change the
# far clip distance in this build at all, and it's baked in at roughly
# 1000 world units (see earthviewer/geo.jl's WORLD_UNIT_KM comment for
# how this was found). The star catalog spans up to ~50,000 pc, which
# can't be used as world units directly, so every position/size fed to
# raylib is scaled down by WORLD_UNIT_PC - chosen so the catalog's full
# extent stays comfortably under the ~1000-unit ceiling. `speed` (parsecs
# per second, camera.jl) and the HUD display stay in real, unscaled
# parsecs - only the actual Vector3 positions/sizes handed to raylib go
# through this conversion. Combined with SCALE_FACTOR (the fixed-point
# integer -> parsec conversion), NODE_SCALE converts a raw fixed-point
# node coordinate directly to a scaled world-space unit in one division.
const WORLD_UNIT_PC = 100.0
const NODE_SCALE = SCALE_FACTOR * WORLD_UNIT_PC

# Hard safety valve: some subtrees (e.g. a parallax segment spanning
# thousands of parsecs) have a bounding box large enough that
# angular-size culling alone doesn't collapse them from certain camera
# positions, which would otherwise make a single frame's traversal
# unboundedly expensive. Once this many points are drawn in a frame,
# every further cull call returns immediately - the frame always
# finishes quickly, and the auto-adapt in main.jl reacts on the next
# frame.
const FRAME_POINT_BUDGET = 1_500_000

# A second, independent safety valve: opening a burst of never-before-seen
# shard files in a single frame (e.g. flying fast into unexplored
# territory) blocks on disk I/O for however long that many opens/mmaps
# take. Capping new loads per frame bounds worst-case frame time the same
# way FRAME_POINT_BUDGET does for drawing.
const MAX_SHARD_LOADS_PER_FRAME = 64

# LOD aggressiveness bounds: a subtree collapses to one representative
# point once its angular size (as seen from the camera) drops below this
# many screen pixels. Smaller = more detail + slower, larger = coarser +
# faster. Angular size is a dimensionless ratio (diag/distance), so it's
# scale-invariant - these thresholds need no WORLD_UNIT_PC conversion.
# Tuned live with '[' / ']' and nudged automatically toward a comfortable
# frame time (see main.jl's loop).
const LOD_PIXEL_TARGET_MIN = 0.25f0
const LOD_PIXEL_TARGET_MAX = 64.0f0

# Stars are drawn as camera-facing billboard quads, batched into dynamic
# Mesh objects and uploaded/drawn with one DrawMesh call per batch,
# instead of raw rlBegin(RL_QUADS)/rlVertex3f immediate mode (which is
# what viewer.c itself does, and what this port originally did too).
# That turned out not to work on this Julia binding's raylib 4.0 build
# specifically: raylib's rlgl has a fixed-size internal vertex batch
# buffer that's meant to auto-flush transparently when full, but on this
# build it instead prints "RLGL: Batch elements overflow" and leaves
# rlgl's internal state broken badly enough that the *entire* frame
# (including unrelated 2D HUD text) came out solid black - confirmed by
# testing against the real catalog, where a single frame near the Sun
# draws tens of thousands of stars, several times the batch's capacity.
# Mirrors earthviewer/dots.jl's batching approach, with two additions
# city dots didn't need: a bound texture (the glow sprite) and a
# per-vertex color buffer (star brightness varies per star, unlike city
# dots' single flat tint).
const STAR_BATCH_MAX_QUADS = 16384 # 4 verts/quad * 16384 = 65536 = raylib's Mesh.indices (u16) limit, exactly

mutable struct StarBatch
    mesh::Raylib.RayMesh
    quad_count::Int
end

# Holds all per-frame render state: the billboard basis vectors, the
# star-quad batches (grown lazily up to FRAME_POINT_BUDGET/
# STAR_BATCH_MAX_QUADS, since most frames need far fewer than the hard
# cap), and this frame's draw/cull stats.
mutable struct Renderer
    billboard_right::Raylib.RayVector3
    billboard_up::Raylib.RayVector3

    batches::Vector{StarBatch}
    batches_used::Int
    star_material::Raylib.RayMaterial

    points_drawn::Int
    nodes_expanded::Int
    nodes_collapsed::Int
    budget_hit::Bool

    star_texture::Raylib.RayTexture
end

# Builds a small soft-edged circular glow sprite: opaque white core
# fading to fully transparent at the edge. Multiplied per-star by each
# quad's own vertex color (set in draw_star_point!), so this only
# supplies the round shape/falloff, not the color itself.
function create_star_texture()
    img = Binding.GenImageGradientRadial(64, 64, 0.15f0, Raylib.WHITE, Raylib.raycolor(255, 255, 255, 0))
    tex = Binding.LoadTextureFromImage(img)
    tex_ref = Ref(tex)
    Binding.GenTextureMipmaps(tex_ref)
    tex = tex_ref[]
    Binding.SetTextureFilter(tex, Int(Raylib.TEXTURE_FILTER_TRILINEAR)) # avoids shimmering as distant stars shrink sub-pixel
    return tex
end

function init_star_batch(star_texture)
    max_verts = STAR_BATCH_MAX_QUADS * 4
    max_tris = STAR_BATCH_MAX_QUADS * 2

    vertices = mesh_malloc(Cfloat, 3 * max_verts)
    texcoords = mesh_malloc(Cfloat, 2 * max_verts)
    colors = mesh_malloc(Cuchar, 4 * max_verts)
    indices = mesh_malloc(UInt16, 3 * max_tris)

    for q in 0:(STAR_BATCH_MAX_QUADS-1)
        # Fixed per-quad-corner UVs (matches viewer.c's DrawStarPoint
        # rlTexCoord2f calls) - never changes frame to frame, uploaded once.
        base_v = q * 4
        unsafe_store!(texcoords, 0.0f0, base_v * 2 + 1); unsafe_store!(texcoords, 1.0f0, base_v * 2 + 2) # bottom-left
        unsafe_store!(texcoords, 1.0f0, base_v * 2 + 3); unsafe_store!(texcoords, 1.0f0, base_v * 2 + 4) # bottom-right
        unsafe_store!(texcoords, 1.0f0, base_v * 2 + 5); unsafe_store!(texcoords, 0.0f0, base_v * 2 + 6) # top-right
        unsafe_store!(texcoords, 0.0f0, base_v * 2 + 7); unsafe_store!(texcoords, 0.0f0, base_v * 2 + 8) # top-left

        v0 = UInt16(q * 4)
        base_i = q * 6
        unsafe_store!(indices, v0, base_i + 1)
        unsafe_store!(indices, v0 + 1, base_i + 2)
        unsafe_store!(indices, v0 + 2, base_i + 3)
        unsafe_store!(indices, v0, base_i + 4)
        unsafe_store!(indices, v0 + 2, base_i + 5)
        unsafe_store!(indices, v0 + 3, base_i + 6)
    end

    mesh = Raylib.RayMesh(
        Cint(max_verts), Cint(max_tris), # capacity; per-frame draws lower this to quad_count*2
        vertices, texcoords, Ptr{Cfloat}(C_NULL), Ptr{Cfloat}(C_NULL), Ptr{Cfloat}(C_NULL),
        Ptr{Cuchar}(colors), Ptr{Cuchar}(indices),
        Cint(0), Ptr{Cuchar}(C_NULL), Ptr{Cfloat}(C_NULL),
        Ptr{Cfloat}(C_NULL), Ptr{Cfloat}(C_NULL),
        Cuint(0), Ptr{Cuint}(C_NULL),
    )
    meshref = Ref(mesh)
    Binding.UploadMesh(meshref, true) # dynamic: positions/colors are re-uploaded every frame
    return StarBatch(meshref[], 0)
end

function new_renderer()
    star_texture = create_star_texture()
    material = Binding.LoadMaterialDefault()
    material_ref = Ref(material)
    Binding.SetMaterialTexture(material_ref, Int(Raylib.MATERIAL_MAP_ALBEDO), star_texture)
    material = material_ref[]

    return Renderer(
        Raylib.rayvector(1.0, 0.0, 0.0),
        Raylib.rayvector(0.0, 1.0, 0.0),
        StarBatch[],
        0,
        material,
        0, 0, 0, false,
        star_texture,
    )
end

function reset_frame!(r::Renderer)
    r.points_drawn = 0
    r.nodes_expanded = 0
    r.nodes_collapsed = 0
    r.budget_hit = false
    for i in 1:r.batches_used
        r.batches[i].quad_count = 0
    end
    r.batches_used = 0
end

# Returns the exact mathematical center from the 3D bounding box [min,
# max], already converted from raw fixed-point integer units directly to
# scaled world-space units (see NODE_SCALE above).
function node_star_pos(n::KdMmap.Node3I64)
    return Raylib.rayvector(
        (n.size[1] + n.size[4]) / (2.0 * NODE_SCALE),
        (n.size[2] + n.size[5]) / (2.0 * NODE_SCALE),
        (n.size[3] + n.size[6]) / (2.0 * NODE_SCALE),
    )
end

# A physically-motivated apparent-brightness mapping: closer =
# brighter/larger. When a single point stands in for a collapsed subtree
# of hidden_count unresolved stars, brighten/enlarge it a bit so dense
# regions still read as dense from far away, instead of looking like one
# dim star. dist is in the same scaled world-space units as every
# position here, so the distance thresholds and size outputs (originally
# tuned in real parsecs) are divided by WORLD_UNIT_PC to match - the
# earthviewer port's city-marker-size clamp bug is exactly the mistake
# being avoided here (an absolute distance/size constant left unscaled
# after the positions it's compared against were scaled).
function star_brightness(dist::Float32, hidden_count::UInt32)
    near_thresh = Float32(50.0 / WORLD_UNIT_PC)
    far_thresh = Float32(3000.0 / WORLD_UNIT_PC)

    local a::UInt8, sz::Float32
    if dist < near_thresh
        a, sz = 0xff, Float32(0.20 / WORLD_UNIT_PC)
    elseif dist > far_thresh
        a, sz = 0x2d, Float32(0.01 / WORLD_UNIT_PC)
    else
        t = (dist - near_thresh) / (far_thresh - near_thresh)
        a = UInt8(round(255.0f0 - t * 210.0f0))
        sz = Float32((0.20 - t * 0.19) / WORLD_UNIT_PC)
    end

    if hidden_count > 1
        boost = 20.0f0 * log2(Float32(hidden_count))
        boosted = Int(a) + Int(round(boost))
        a = UInt8(min(boosted, 255))
        sz += Float32(0.02 * log2(Float32(hidden_count)) / WORLD_UNIT_PC)
    end

    return a, sz
end

# Writes one camera-facing billboard quad directly into the current
# batch's vertex/color buffers (billboard_right/up are recomputed once
# per frame, not per star, in main.jl's per-frame setup). Grows
# r.batches lazily the first time a given batch index is needed.
function draw_star_point!(r::Renderer, pos, dist::Float32, hidden_count::UInt32)
    alpha, size = star_brightness(dist, hidden_count)

    global_quad_idx = r.points_drawn
    batch_idx = div(global_quad_idx, STAR_BATCH_MAX_QUADS) + 1 # 1-based
    while batch_idx > length(r.batches)
        push!(r.batches, init_star_batch(r.star_texture))
    end
    local_quad_idx = mod(global_quad_idx, STAR_BATCH_MAX_QUADS) # 0-based within the batch

    right = r.billboard_right * size
    up = r.billboard_up * size
    p0 = pos - right - up # bottom-left
    p1 = pos + right - up # bottom-right
    p2 = pos + right + up # top-right
    p3 = pos - right + up # top-left

    batch = r.batches[batch_idx]
    vptr = batch.mesh.vertices
    base_vertex = local_quad_idx * 4 # 0-based
    for (k, p) in enumerate((p0, p1, p2, p3))
        off = (base_vertex + k - 1) * 3
        unsafe_store!(vptr, p[1], off + 1)
        unsafe_store!(vptr, p[2], off + 2)
        unsafe_store!(vptr, p[3], off + 3)
    end

    cptr = batch.mesh.colors
    for k in 0:3
        off = (base_vertex + k) * 4
        unsafe_store!(cptr, 0xff, off + 1)
        unsafe_store!(cptr, 0xf5, off + 2)
        unsafe_store!(cptr, 0xda, off + 3) # warm yellow-white glowing stars
        unsafe_store!(cptr, alpha, off + 4)
    end

    batch.quad_count = local_quad_idx + 1
    batch_idx > r.batches_used && (r.batches_used = batch_idx)
    r.points_drawn += 1
end

# Uploads and draws every batch touched this frame. Only the filled
# prefix of each batch's vertex/color buffers is uploaded (quad_count*4
# verts), matching the equivalent partial UpdateMeshBuffer calls in
# earthviewer/dots.jl.
function flush_and_draw!(r::Renderer)
    for i in 1:r.batches_used
        batch = r.batches[i]
        batch.quad_count == 0 && continue
        vert_count = batch.quad_count * 4
        vert_bytes = vert_count * 3 * sizeof(Cfloat)
        color_bytes = vert_count * 4 * sizeof(Cuchar)
        Binding.UpdateMeshBuffer(batch.mesh, 0, Ptr{Cvoid}(batch.mesh.vertices), vert_bytes, 0) # position buffer
        Binding.UpdateMeshBuffer(batch.mesh, 3, Ptr{Cvoid}(batch.mesh.colors), color_bytes, 0)  # color buffer
        batch.mesh = with_triangle_count(batch.mesh, batch.quad_count * 2)
        Binding.DrawMesh(batch.mesh, r.star_material, Binding.MatrixIdentity())
    end
end
