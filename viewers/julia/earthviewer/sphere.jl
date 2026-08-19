const EARTH_SPHERE_RINGS = 720
const EARTH_SPHERE_SLICES = 1440

# raylib's Mesh.indices is 16-bit-wide - a hard 65536-vertex limit PER
# MESH, baked into the library itself. EARTH_SPHERE_RINGS/SLICES exceed
# that in one mesh (721*1441 = 1,038,961 vertices), so build_earth_meshes
# below splits the sphere into several latitude bands, each its own Mesh,
# all sharing one Material at draw time.
#
# Raylib.jl's own RayMesh.indices field is (mis-)typed Ptr{Cuchar} rather
# than the C header's actual `unsigned short *` - harmless, since only the
# raw pointer VALUE crosses the ccall boundary (Julia's Ptr{T} annotation
# doesn't affect what raylib's C code, compiled against the real
# `unsigned short *` type, does with those bytes). Confirmed by a direct
# test: allocating 2 bytes/index, writing UInt16s, and storing the pointer
# cast to Ptr{Cuchar} round-trips through UploadMesh/DrawMesh correctly.
const MAX_MESH_VERTICES = 65536

# Buffers are allocated with Base.Libc.malloc (the process's real libc
# malloc) rather than through Julia's GC-managed arrays. This matters
# because calling raylib's UnloadMesh on a mesh frees every non-null
# buffer pointer via RL_FREE (= MemFree, plain free() by default) -
# allocating with the matching malloc up front guarantees that free is
# legitimate, rather than handing libc a pointer from Julia's own
# (different) GC heap.
function mesh_malloc(::Type{T}, count::Integer) where {T}
    ptr = Base.Libc.malloc(count * sizeof(T))
    ptr == C_NULL && error("malloc failed allocating $(count * sizeof(T)) bytes")
    return Ptr{T}(ptr)
end

# Builds one latitude band (rows row_start..row_end inclusive, out of the
# overall 0..rings) of a UV-sphere textured with an equirectangular Earth
# image, using the SAME lon_lat_to_cartesian used to place every city dot
# - so the texture and the dots are guaranteed to agree on where a given
# (lon_deg, lat_deg) lands. UV coordinates are derived from the raw
# (lon_deg, lat_deg), not the Cartesian result, so lon_lat_to_cartesian's
# longitude negation has no effect on texture alignment. Standard
# equirectangular layout: u=0 at lon=-180 (west edge), u=1 at lon=+180
# increasing eastward; v=0 at the north pole (lat=+90), v=1 at the south
# pole (lat=-90).
function gen_earth_sphere_mesh_band(radius_km, rings, slices, row_start, row_end)
    band_rows = row_end - row_start
    ring_verts = slices + 1
    vertex_count = (band_rows + 1) * ring_verts
    triangle_count = band_rows * slices * 2

    vertex_count > MAX_MESH_VERTICES && error(
        "gen_earth_sphere_mesh_band: rows $row_start..$row_end x $slices slices = $vertex_count vertices, " *
        "exceeds raylib's 16-bit Mesh.indices limit ($MAX_MESH_VERTICES)",
    )

    vertices = mesh_malloc(Cfloat, 3 * vertex_count)
    normals = mesh_malloc(Cfloat, 3 * vertex_count)
    texcoords = mesh_malloc(Cfloat, 2 * vertex_count)
    indices = mesh_malloc(UInt16, 3 * triangle_count)

    v = 0
    for r in row_start:row_end
        lat_deg = 90.0 - (180.0 * r / rings)
        for s in 0:slices
            lon_deg = -180.0 + (360.0 * s / slices)
            p = lon_lat_to_cartesian(lon_deg, lat_deg, radius_km)
            n = p * (1.0f0 / radius_km)
            unsafe_store!(vertices, p[1], v * 3 + 1)
            unsafe_store!(vertices, p[2], v * 3 + 2)
            unsafe_store!(vertices, p[3], v * 3 + 3)
            unsafe_store!(normals, n[1], v * 3 + 1)
            unsafe_store!(normals, n[2], v * 3 + 2)
            unsafe_store!(normals, n[3], v * 3 + 3)
            unsafe_store!(texcoords, Float32(s) / Float32(slices), v * 2 + 1)
            unsafe_store!(texcoords, Float32(r) / Float32(rings), v * 2 + 2)
            v += 1
        end
    end

    # Winding: (a,b,c) and (c,b,d) is counter-clockwise as seen from
    # outside the sphere (verified numerically against the outward radial
    # direction at every sampled (r,s)), which is what makes this mesh's
    # outward faces the "front" faces under backface culling. See
    # earth_viewer.c's GenEarthSphereMeshBand comment for the bug the
    # opposite winding caused: inward-facing triangles let the far
    # (antipodal) hemisphere win the depth test once a real (non-flat)
    # texture was bound.
    idx = 0
    for r in 0:(band_rows-1)
        for s in 0:(slices-1)
            a = UInt16(r * ring_verts + s)
            b = a + UInt16(ring_verts)
            c = a + UInt16(1)
            d = b + UInt16(1)
            unsafe_store!(indices, a, idx + 1)
            unsafe_store!(indices, b, idx + 2)
            unsafe_store!(indices, c, idx + 3)
            unsafe_store!(indices, c, idx + 4)
            unsafe_store!(indices, b, idx + 5)
            unsafe_store!(indices, d, idx + 6)
            idx += 6
        end
    end

    mesh = Raylib.RayMesh(
        Cint(vertex_count), Cint(triangle_count),
        vertices, texcoords, Ptr{Cfloat}(C_NULL), normals, Ptr{Cfloat}(C_NULL),
        Ptr{Cuchar}(C_NULL), Ptr{Cuchar}(indices),
        Ptr{Cfloat}(C_NULL), Ptr{Cfloat}(C_NULL), Ptr{Cuchar}(C_NULL), Ptr{Cfloat}(C_NULL),
        Cuint(0), Ptr{Cuint}(C_NULL),
    )
    meshref = Ref(mesh)
    Binding.UploadMesh(meshref, false)
    return meshref[]
end

# Splits a rings x slices UV-sphere into however many latitude bands are
# needed to keep every individual Mesh under raylib's 16-bit
# Mesh.indices limit, and returns them as a plain Vector - draw_mesh (used
# in the render loop) works directly on that with a single shared
# Material, simpler than assembling a multi-mesh Model by hand for no
# benefit here (this Earth mesh has no animation, LOD, or per-mesh
# material need).
function build_earth_meshes(radius_km, rings, slices)
    max_band_rows = div(MAX_MESH_VERTICES, slices + 1) - 1
    band_count = cld(rings, max_band_rows)
    band_rows = cld(rings, band_count) # redistributed evenly

    meshes = Raylib.RayMesh[]
    row_start = 0
    while row_start < rings
        row_end = min(row_start + band_rows, rings)
        push!(meshes, gen_earth_sphere_mesh_band(radius_km, rings, slices, row_start, row_end))
        row_start += band_rows
    end
    return meshes
end
