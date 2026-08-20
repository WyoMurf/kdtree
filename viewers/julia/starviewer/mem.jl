# Allocates via Base.Libc.malloc (the process's real libc malloc) rather
# than through Julia's GC-managed arrays. This matters because dropping
# a raylib Mesh calls UnloadMesh, which frees every non-null buffer
# pointer via RL_FREE (= MemFree, plain free() by default) - allocating
# with the matching malloc up front guarantees that free is legitimate,
# rather than handing libc a pointer from Julia's own (different) GC
# heap. Mirrors earthviewer/sphere.jl's mesh_malloc.
function mesh_malloc(::Type{T}, count::Integer) where {T}
    ptr = Base.Libc.malloc(count * sizeof(T))
    ptr == C_NULL && error("malloc failed allocating $(count * sizeof(T)) bytes")
    return Ptr{T}(ptr)
end

# RayMesh is an immutable struct, so changing just triangleCount (done
# every frame, to draw only the filled prefix of a batch) means
# rebuilding the whole struct. Mirrors earthviewer/dots.jl's helper of
# the same name.
# raylib 6.0's Mesh gained boneCount and reordered bone*/anim* fields
# (boneIds -> boneIndices) relative to the 4.0 layout this was written against.
with_triangle_count(mesh::Raylib.RayMesh, tc::Integer) = Raylib.RayMesh(
    mesh.vertexCount, Cint(tc), mesh.vertices, mesh.texcoords, mesh.texcoords2,
    mesh.normals, mesh.tangents, mesh.colors, mesh.indices, mesh.boneCount,
    mesh.boneIndices, mesh.boneWeights, mesh.animVertices, mesh.animNormals,
    mesh.vaoId, mesh.vboId,
)
