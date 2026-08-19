use raylib::prelude::*;

use crate::geo::lon_lat_to_cartesian;
use crate::mem::mem_alloc;

pub const EARTH_SPHERE_RINGS: i32 = 720;
pub const EARTH_SPHERE_SLICES: i32 = 1440;

/// raylib's Mesh.indices is `*mut u16` - a hard 16-bit limit on vertex
/// count PER MESH, baked into the library itself. EARTH_SPHERE_RINGS/
/// SLICES exceed that in one mesh (721*1441 = 1,038,961 vertices), so
/// build_earth_meshes below splits the sphere into several latitude bands,
/// each its own Mesh, all sharing one Material at draw time.
const MAX_MESH_VERTICES: i32 = 65536;

/// Builds one latitude band (rows row_start..row_end inclusive, out of the
/// overall 0..rings) of a UV-sphere textured with an equirectangular Earth
/// image, using the SAME lon_lat_to_cartesian used to place every city dot
/// - so the texture and the dots are guaranteed to agree on where a given
/// (lon_deg, lat_deg) lands. UV coordinates are derived from the raw
/// (lon_deg, lat_deg), not the Cartesian result, so lon_lat_to_cartesian's
/// longitude negation has no effect on texture alignment. Standard
/// equirectangular layout: u=0 at lon=-180 (west edge), u=1 at lon=+180
/// increasing eastward; v=0 at the north pole (lat=+90), v=1 at the south
/// pole (lat=-90).
fn gen_earth_sphere_mesh_band(
    radius_km: f32,
    rings: i32,
    slices: i32,
    row_start: i32,
    row_end: i32,
) -> Mesh {
    let band_rows = row_end - row_start;
    let ring_verts = slices + 1;
    let vertex_count = (band_rows + 1) * ring_verts;
    let triangle_count = band_rows * slices * 2;

    assert!(
        vertex_count <= MAX_MESH_VERTICES,
        "gen_earth_sphere_mesh_band: rows {row_start}..{row_end} x {slices} slices = {vertex_count} vertices, exceeds raylib's 16-bit Mesh.indices limit ({MAX_MESH_VERTICES})"
    );

    unsafe {
        let vertices = mem_alloc::<f32>(3 * vertex_count as usize);
        let normals = mem_alloc::<f32>(3 * vertex_count as usize);
        let texcoords = mem_alloc::<f32>(2 * vertex_count as usize);
        let indices = mem_alloc::<u16>(3 * triangle_count as usize);

        let mut v: isize = 0;
        for r in row_start..=row_end {
            let lat_deg = 90.0 - (180.0 * r as f64 / rings as f64);
            for s in 0..=slices {
                let lon_deg = -180.0 + (360.0 * s as f64 / slices as f64);
                let p = lon_lat_to_cartesian(lon_deg, lat_deg, radius_km);
                let n = p * (1.0 / radius_km);
                *vertices.offset(v * 3) = p.x;
                *vertices.offset(v * 3 + 1) = p.y;
                *vertices.offset(v * 3 + 2) = p.z;
                *normals.offset(v * 3) = n.x;
                *normals.offset(v * 3 + 1) = n.y;
                *normals.offset(v * 3 + 2) = n.z;
                *texcoords.offset(v * 2) = s as f32 / slices as f32;
                *texcoords.offset(v * 2 + 1) = r as f32 / rings as f32;
                v += 1;
            }
        }

        // Winding: (a,b,c) and (c,b,d) is counter-clockwise as seen from
        // outside the sphere (verified numerically against the outward
        // radial direction at every sampled (r,s)), which is what makes
        // this mesh's outward faces the "front" faces under backface
        // culling. See earth_viewer.c's GenEarthSphereMeshBand comment for
        // the bug the opposite winding caused: inward-facing triangles let
        // the far (antipodal) hemisphere win the depth test once a real
        // (non-flat) texture was bound.
        let mut idx: isize = 0;
        for r in 0..band_rows {
            for s in 0..slices {
                let a = (r * ring_verts + s) as u16;
                let b = a + ring_verts as u16;
                let c = a + 1;
                let d = b + 1;
                *indices.offset(idx) = a;
                *indices.offset(idx + 1) = b;
                *indices.offset(idx + 2) = c;
                *indices.offset(idx + 3) = c;
                *indices.offset(idx + 4) = b;
                *indices.offset(idx + 5) = d;
                idx += 6;
            }
        }

        let raw = raylib::ffi::Mesh {
            vertexCount: vertex_count,
            triangleCount: triangle_count,
            vertices,
            texcoords,
            texcoords2: std::ptr::null_mut(),
            normals,
            tangents: std::ptr::null_mut(),
            colors: std::ptr::null_mut(),
            indices,
            animVertices: std::ptr::null_mut(),
            animNormals: std::ptr::null_mut(),
            boneIds: std::ptr::null_mut(),
            boneWeights: std::ptr::null_mut(),
            boneMatrices: std::ptr::null_mut(),
            boneCount: 0,
            vaoId: 0,
            vboId: std::ptr::null_mut(),
        };
        let mut mesh = Mesh::from_raw(raw);
        mesh.upload(false);
        mesh
    }
}

/// Splits a rings x slices UV-sphere into however many latitude bands are
/// needed to keep every individual Mesh under raylib's 16-bit
/// Mesh.indices limit, and returns them as a plain Vec - DrawMesh (used in
/// the render loop) works directly on that with a single shared Material,
/// simpler than assembling a multi-mesh Model by hand for no benefit here
/// (this Earth mesh has no animation, LOD, or per-mesh material need).
pub fn build_earth_meshes(radius_km: f32, rings: i32, slices: i32) -> Vec<Mesh> {
    let max_band_rows = MAX_MESH_VERTICES / (slices + 1) - 1;
    let band_count = (rings + max_band_rows - 1) / max_band_rows; // ceil(rings/max_band_rows)
    let band_rows = (rings + band_count - 1) / band_count; // ceil(rings/band_count), redistributed evenly

    let mut meshes = Vec::new();
    let mut row_start = 0;
    while row_start < rings {
        let row_end = (row_start + band_rows).min(rings);
        meshes.push(gen_earth_sphere_mesh_band(
            radius_km, rings, slices, row_start, row_end,
        ));
        row_start += band_rows;
    }
    meshes
}
