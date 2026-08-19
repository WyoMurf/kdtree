package main

import (
	"fmt"
	"os"
	"unsafe"

	rl "github.com/gen2brain/raylib-go/raylib"
)

const (
	earthSphereRings  = 720
	earthSphereSlices = 1440
)

// raylib's Mesh.Indices is *uint16 - a hard 16-bit limit on vertex count PER
// MESH, baked into the library itself. earthSphereRings/Slices exceed that
// in one mesh (721*1441 = 1,038,961 vertices), so buildEarthMeshes below
// splits the sphere into several latitude bands, each its own Mesh, all
// sharing one Material at draw time - see genEarthSphereMeshBand.
const maxMeshVertices = 65536

// genEarthSphereMeshBand builds one latitude band (rows rowStart..rowEnd
// inclusive, out of the overall 0..rings) of a UV-sphere textured with an
// equirectangular Earth image, using the SAME LonLatToCartesian used to
// place every city dot - so the texture and the dots are guaranteed to
// agree on where a given (lonDeg, latDeg) lands. UV coordinates are derived
// from the raw (lonDeg, latDeg), not the Cartesian result, so
// LonLatToCartesian's longitude negation has no effect on texture
// alignment. Standard equirectangular layout: u=0 at lon=-180 (west edge),
// u=1 at lon=+180 increasing eastward; v=0 at the north pole (lat=+90),
// v=1 at the south pole (lat=-90).
func genEarthSphereMeshBand(radiusKm float32, rings, slices, rowStart, rowEnd int) rl.Mesh {
	bandRows := rowEnd - rowStart
	ringVerts := slices + 1
	vertexCount := (bandRows + 1) * ringVerts
	triangleCount := bandRows * slices * 2

	if vertexCount > maxMeshVertices {
		fmt.Fprintf(os.Stderr, "genEarthSphereMeshBand: rows %d..%d x %d slices = %d vertices, exceeds raylib's 16-bit Mesh.Indices limit (%d)\n",
			rowStart, rowEnd, slices, vertexCount, maxMeshVertices)
		os.Exit(1)
	}

	vertices := make([]float32, 3*vertexCount)
	normals := make([]float32, 3*vertexCount)
	texcoords := make([]float32, 2*vertexCount)
	indices := make([]uint16, 3*triangleCount)

	v := 0
	for r := rowStart; r <= rowEnd; r++ {
		latDeg := 90.0 - (180.0 * float64(r) / float64(rings))
		for s := 0; s <= slices; s++ {
			lonDeg := -180.0 + (360.0 * float64(s) / float64(slices))
			p := LonLatToCartesian(lonDeg, latDeg, radiusKm)
			n := rl.Vector3Scale(p, 1.0/radiusKm)
			vertices[v*3+0] = p.X
			vertices[v*3+1] = p.Y
			vertices[v*3+2] = p.Z
			normals[v*3+0] = n.X
			normals[v*3+1] = n.Y
			normals[v*3+2] = n.Z
			texcoords[v*2+0] = float32(s) / float32(slices)
			texcoords[v*2+1] = float32(r) / float32(rings)
			v++
		}
	}

	// Winding: (a,b,c) and (c,b,d) is counter-clockwise as seen from outside
	// the sphere (verified numerically against the outward radial direction
	// at every sampled (r,s)), which is what makes this mesh's outward
	// faces the "front" faces under backface culling. See
	// earth_viewer.c's GenEarthSphereMeshBand comment for the bug the
	// opposite winding caused: inward-facing triangles let the far
	// (antipodal) hemisphere win the depth test once a real (non-flat)
	// texture was bound.
	idx := 0
	for r := 0; r < bandRows; r++ {
		for s := 0; s < slices; s++ {
			a := uint16(r*ringVerts + s)
			b := a + uint16(ringVerts)
			c := a + 1
			d := b + 1
			indices[idx+0] = a
			indices[idx+1] = b
			indices[idx+2] = c
			indices[idx+3] = c
			indices[idx+4] = b
			indices[idx+5] = d
			idx += 6
		}
	}

	mesh := rl.Mesh{
		VertexCount:   int32(vertexCount),
		TriangleCount: int32(triangleCount),
		Vertices:      (*float32)(unsafe.Pointer(&vertices[0])),
		Normals:       (*float32)(unsafe.Pointer(&normals[0])),
		Texcoords:     (*float32)(unsafe.Pointer(&texcoords[0])),
		Indices:       (*uint16)(unsafe.Pointer(&indices[0])),
	}
	rl.UploadMesh(&mesh, false)
	return mesh
}

// buildEarthMeshes splits a rings x slices UV-sphere into however many
// latitude bands are needed to keep every individual Mesh under raylib's
// 16-bit Mesh.Indices limit, and returns them as a plain slice - DrawMesh
// (used in the render loop) works directly on that with a single shared
// Material, simpler than assembling a multi-mesh Model by hand for no
// benefit here (this Earth mesh has no animation, LOD, or per-mesh material
// need).
func buildEarthMeshes(radiusKm float32, rings, slices int) []rl.Mesh {
	maxBandRows := (maxMeshVertices/(slices+1) - 1)
	bandCount := (rings + maxBandRows - 1) / maxBandRows // ceil(rings/maxBandRows)
	bandRows := (rings + bandCount - 1) / bandCount      // ceil(rings/bandCount), redistributed evenly

	var meshes []rl.Mesh
	for rowStart := 0; rowStart < rings; rowStart += bandRows {
		rowEnd := rowStart + bandRows
		if rowEnd > rings {
			rowEnd = rings
		}
		meshes = append(meshes, genEarthSphereMeshBand(radiusKm, rings, slices, rowStart, rowEnd))
	}
	return meshes
}
