# City dots are drawn as camera-facing billboard quads, batched into a
# handful of dynamic Mesh objects and uploaded/drawn with one DrawMesh
# call per batch, instead of one rlBegin(RL_QUADS)/rlVertex3f call per dot
# - ~88k individual immediate-mode vertex calls per frame capped the C
# viewer at ~23 FPS at typical viewing distance (confirmed by disabling
# dot rendering alone and watching FPS jump to a smooth 60). raylib's
# Mesh.indices is still a hard 16-bit-per-mesh limit, so quads are split
# across several fixed-capacity batch meshes the same way the Earth
# sphere is split into latitude bands - the index buffer (which vertices
# form which triangles) never changes and is uploaded once at init; only
# the position buffer is re-uploaded per frame. Color/UV are dropped
# entirely: every dot is the same flat amber, reproduced by the shared
# Material's diffuse tint, and an untextured mesh needs no UVs.
const DOT_BATCH_MAX_QUADS = 16384 # 4 verts/quad * 16384 = 65536 = raylib's Mesh.indices (u16) limit, exactly
const MAX_VISIBLE_DOTS_PER_FRAME = 300_000
const MAX_LABELS_PER_FRAME = 4000

mutable struct DotBatch
    mesh::Raylib.RayMesh
    quad_count::Int
end

struct PendingLabel
    world_pos::Raylib.RayVector3
    name::String
end

# Holds all per-frame render state: the billboard basis vectors, the dot
# batches, and this frame's pending labels.
mutable struct Renderer
    billboard_right::Raylib.RayVector3
    billboard_up::Raylib.RayVector3
    points_drawn::Int
    batches::Vector{DotBatch}
    batches_used::Int
    dot_material::Raylib.RayMaterial
    labels::Vector{PendingLabel}
end

# raylib 6.0's Mesh gained boneCount and reordered bone*/anim* fields
# (boneIds -> boneIndices) relative to the 4.0 layout this was written against.
with_triangle_count(mesh::Raylib.RayMesh, tc::Integer) = Raylib.RayMesh(
    mesh.vertexCount, Cint(tc), mesh.vertices, mesh.texcoords, mesh.texcoords2,
    mesh.normals, mesh.tangents, mesh.colors, mesh.indices, mesh.boneCount,
    mesh.boneIndices, mesh.boneWeights, mesh.animVertices, mesh.animNormals,
    mesh.vaoId, mesh.vboId,
)

function init_dot_batch()
    max_verts = DOT_BATCH_MAX_QUADS * 4
    max_tris = DOT_BATCH_MAX_QUADS * 2

    vertices = mesh_malloc(Cfloat, 3 * max_verts)
    indices = mesh_malloc(UInt16, 3 * max_tris)
    for q in 0:(DOT_BATCH_MAX_QUADS-1)
        v0 = UInt16(q * 4)
        base = q * 6
        unsafe_store!(indices, v0, base + 1)
        unsafe_store!(indices, v0 + 1, base + 2)
        unsafe_store!(indices, v0 + 2, base + 3)
        unsafe_store!(indices, v0, base + 4)
        unsafe_store!(indices, v0 + 2, base + 5)
        unsafe_store!(indices, v0 + 3, base + 6)
    end

    mesh = Raylib.RayMesh(
        Cint(max_verts), Cint(max_tris), # capacity; per-frame draws lower this to quad_count*2
        vertices, Ptr{Cfloat}(C_NULL), Ptr{Cfloat}(C_NULL), Ptr{Cfloat}(C_NULL), Ptr{Cfloat}(C_NULL),
        Ptr{Cuchar}(C_NULL), Ptr{Cuchar}(indices),
        Cint(0), Ptr{Cuchar}(C_NULL), Ptr{Cfloat}(C_NULL),
        Ptr{Cfloat}(C_NULL), Ptr{Cfloat}(C_NULL),
        Cuint(0), Ptr{Cuint}(C_NULL),
    )
    meshref = Ref(mesh)
    Binding.UploadMesh(meshref, true) # dynamic: positions are re-uploaded every frame
    return meshref[]
end

function new_renderer()
    material = Binding.LoadMaterialDefault()
    old_map = unsafe_load(material.maps, 1) # index 1 = MATERIAL_MAP_ALBEDO
    new_map = Raylib.RayMaterialMap(old_map.texture, Raylib.raycolor(255, 210, 90, 255), old_map.value) # warm amber marker dots
    unsafe_store!(material.maps, new_map, 1)

    batch_count = cld(MAX_VISIBLE_DOTS_PER_FRAME, DOT_BATCH_MAX_QUADS)
    batches = [DotBatch(init_dot_batch(), 0) for _ in 1:batch_count]

    return Renderer(
        Raylib.rayvector(1.0, 0.0, 0.0),
        Raylib.rayvector(0.0, 1.0, 0.0),
        0,
        batches,
        0,
        material,
        PendingLabel[],
    )
end

# A fixed world-space size looks fine from far away (shrinks correctly
# with perspective) but is far too large up close: real neighboring towns
# in a densely-settled region can be closer together than a generously-
# sized fixed marker, so their billboards overlap and merge into a solid
# blob instead of showing as distinct dots. Sizing by a roughly-constant
# angular radius (world size = angle * distance) keeps markers a
# sensible, mostly distance-independent size on screen instead, clamped
# so it neither vanishes at planetary range nor balloons absurdly at very
# low altitude.
function city_marker_world_size(dist_km, log10_pop)
    pop_boost = 1.0f0 + 0.15f0 * Float32(log10_pop)
    angular_radius = 0.0009f0 * pop_boost
    # The angular_radius*dist term is scale-invariant (dist_km is now
    # computed from already-scaled positions, see geo.jl's WORLD_UNIT_KM
    # comment, and the result is used directly in that same scaled
    # space) - but the clamp bounds are absolute sizes tuned in real km
    # (0.3 km min, 150 km max), so they need the same conversion.
    return clamp(angular_radius * Float32(dist_km), 0.3f0 / WORLD_UNIT_KM, 150.0f0 / WORLD_UNIT_KM)
end

# Writes one camera-facing billboard quad directly into the current
# batch's vertex buffer (billboard_right/up are recomputed once per
# frame, not per dot, in the caller's per-frame setup).
function draw_city_point!(r::Renderer, pos, size_km)
    global_quad_idx = r.points_drawn
    batch_idx = div(global_quad_idx, DOT_BATCH_MAX_QUADS) + 1 # 1-based
    batch_idx > length(r.batches) && return # hit MAX_VISIBLE_DOTS_PER_FRAME; should never happen at this dataset's scale
    local_quad_idx = mod(global_quad_idx, DOT_BATCH_MAX_QUADS) # 0-based within the batch

    right = r.billboard_right * size_km
    up = r.billboard_up * size_km
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
    batch.quad_count = local_quad_idx + 1
    batch_idx > r.batches_used && (r.batches_used = batch_idx)

    r.points_drawn += 1
end

# Uploads and draws every batch touched this frame. Only the filled
# prefix of each batch's vertex buffer is uploaded (quad_count*4 verts),
# matching earth_viewer.c's partial UpdateMeshBuffer call.
function flush_and_draw!(r::Renderer)
    for i in 1:r.batches_used
        batch = r.batches[i]
        batch.quad_count == 0 && continue
        byte_len = batch.quad_count * 4 * 3 * sizeof(Cfloat)
        Binding.UpdateMeshBuffer(batch.mesh, 0, Ptr{Cvoid}(batch.mesh.vertices), byte_len, 0) # index 0 = position buffer
        batch.mesh = with_triangle_count(batch.mesh, batch.quad_count * 2)
        Binding.DrawMesh(batch.mesh, r.dot_material, Binding.MatrixIdentity())
    end
end

function reset_frame!(r::Renderer)
    r.points_drawn = 0
    empty!(r.labels)
    for i in 1:r.batches_used
        r.batches[i].quad_count = 0
    end
    r.batches_used = 0
end
