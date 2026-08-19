package main

import (
	"math"
	"unsafe"

	rl "github.com/gen2brain/raylib-go/raylib"
)

// City dots are drawn as camera-facing billboard quads, batched into a
// handful of dynamic Mesh objects and uploaded/drawn with one DrawMesh call
// per batch, instead of one rlBegin(RL_QUADS)/rlVertex3f call per dot -
// ~88k individual immediate-mode vertex calls per frame capped the C
// viewer at ~23 FPS at typical viewing distance (confirmed by disabling dot
// rendering alone and watching FPS jump to a smooth 60). raylib's
// Mesh.Indices is still a hard 16-bit-per-mesh limit, so quads are split
// across several fixed-capacity batch meshes the same way the Earth sphere
// is split into latitude bands - the index buffer (which vertices form
// which triangles) never changes and is uploaded once at init; only the
// position buffer is re-uploaded per frame. Color/UV are dropped entirely:
// every dot is the same flat amber, reproduced by the shared Material's
// diffuse tint, and an untextured mesh needs no UVs.
const (
	dotBatchMaxQuads       = 16384 // 4 verts/quad * 16384 = 65536 = raylib's Mesh.Indices (uint16) limit, exactly
	maxVisibleDotsPerFrame = 300000
	maxLabelsPerFrame      = 4000
)

var dotBatchCount = (maxVisibleDotsPerFrame + dotBatchMaxQuads - 1) / dotBatchMaxQuads

type DotBatch struct {
	mesh      rl.Mesh
	vertices  []float32 // capacity dotBatchMaxQuads*4*3 floats; mesh.Vertices points into this
	quadCount int
}

type PendingLabel struct {
	WorldPos rl.Vector3
	Name     string
}

// Renderer holds all per-frame render state: the billboard basis vectors,
// the dot batches, and this frame's pending labels.
type Renderer struct {
	billboardRight rl.Vector3
	billboardUp    rl.Vector3
	pointsDrawn    int

	batches     []DotBatch
	batchesUsed int
	dotMaterial rl.Material

	labels []PendingLabel
}

func NewRenderer() *Renderer {
	r := &Renderer{
		billboardRight: rl.NewVector3(1, 0, 0),
		billboardUp:    rl.NewVector3(0, 1, 0),
	}
	r.initDotBatches()
	return r
}

func (r *Renderer) initDotBatches() {
	r.dotMaterial = rl.LoadMaterialDefault()
	r.dotMaterial.GetMap(rl.MapDiffuse).Color = rl.NewColor(255, 210, 90, 255) // warm amber marker dots

	r.batches = make([]DotBatch, dotBatchCount)
	for b := 0; b < dotBatchCount; b++ {
		maxVerts := dotBatchMaxQuads * 4
		maxTris := dotBatchMaxQuads * 2

		vertices := make([]float32, 3*maxVerts)
		indices := make([]uint16, 3*maxTris)
		for q := 0; q < dotBatchMaxQuads; q++ {
			v0 := uint16(q * 4)
			indices[q*6+0], indices[q*6+1], indices[q*6+2] = v0, v0+1, v0+2
			indices[q*6+3], indices[q*6+4], indices[q*6+5] = v0, v0+2, v0+3
		}

		mesh := rl.Mesh{
			VertexCount:   int32(maxVerts),
			TriangleCount: int32(maxTris), // capacity; per-frame draws lower this to quadCount*2
			Vertices:      (*float32)(unsafe.Pointer(&vertices[0])),
			Indices:       (*uint16)(unsafe.Pointer(&indices[0])),
		}
		rl.UploadMesh(&mesh, true) // dynamic: positions are re-uploaded every frame

		r.batches[b] = DotBatch{mesh: mesh, vertices: vertices}
	}
}

// CityMarkerWorldSize sizes markers by a roughly-constant angular radius
// (world size = angle * distance) so neighboring towns in densely-settled
// regions don't overlap into a blob up close, while markers still shrink
// correctly with perspective from far away. log10Pop is passed in
// pre-computed since the caller (DrawTilePoints) also needs it for the
// label-threshold curve, and log10 is a real cost at ~88k calls/frame.
func CityMarkerWorldSize(distKm, log10Pop float32) float32 {
	popBoost := 1.0 + 0.15*log10Pop
	angularRadius := 0.0009 * popBoost
	sz := angularRadius * distKm
	if sz < 0.3 {
		sz = 0.3
	}
	if sz > 150.0 {
		sz = 150.0
	}
	return sz
}

func log10f(x float32) float32 {
	return float32(math.Log10(float64(x)))
}

// DrawCityPoint writes one camera-facing billboard quad directly into the
// current batch's vertex buffer (billboardRight/Up are recomputed once per
// frame, not per dot, in the caller's per-frame setup).
func (r *Renderer) DrawCityPoint(pos rl.Vector3, sizeKm float32) {
	globalQuadIdx := r.pointsDrawn
	batchIdx := globalQuadIdx / dotBatchMaxQuads
	if batchIdx >= len(r.batches) {
		return // hit maxVisibleDotsPerFrame; should never happen at this dataset's scale
	}
	localQuadIdx := globalQuadIdx % dotBatchMaxQuads

	right := rl.Vector3Scale(r.billboardRight, sizeKm)
	up := rl.Vector3Scale(r.billboardUp, sizeKm)
	p0 := rl.Vector3Subtract(rl.Vector3Subtract(pos, right), up)
	p1 := rl.Vector3Subtract(rl.Vector3Add(pos, right), up)
	p2 := rl.Vector3Add(rl.Vector3Add(pos, right), up)
	p3 := rl.Vector3Add(rl.Vector3Subtract(pos, right), up)

	batch := &r.batches[batchIdx]
	v := batch.vertices[localQuadIdx*4*3:]
	v[0], v[1], v[2] = p0.X, p0.Y, p0.Z
	v[3], v[4], v[5] = p1.X, p1.Y, p1.Z
	v[6], v[7], v[8] = p2.X, p2.Y, p2.Z
	v[9], v[10], v[11] = p3.X, p3.Y, p3.Z
	batch.quadCount = localQuadIdx + 1
	if batchIdx+1 > r.batchesUsed {
		r.batchesUsed = batchIdx + 1
	}

	r.pointsDrawn++
}

// FlushAndDraw uploads and draws every batch touched this frame. Only the
// filled prefix of each batch's vertex buffer is uploaded (quadCount*4
// verts), matching earth_viewer.c's partial UpdateMeshBuffer call.
func (r *Renderer) FlushAndDraw() {
	for b := 0; b < r.batchesUsed; b++ {
		batch := &r.batches[b]
		if batch.quadCount == 0 {
			continue
		}
		floatCount := batch.quadCount * 4 * 3
		byteView := unsafe.Slice((*byte)(unsafe.Pointer(&batch.vertices[0])), floatCount*4)
		rl.UpdateMeshBuffer(batch.mesh, 0, byteView, 0) // index 0 = position buffer
		batch.mesh.TriangleCount = int32(batch.quadCount * 2)
		rl.DrawMesh(batch.mesh, r.dotMaterial, rl.MatrixIdentity())
	}
}

func (r *Renderer) ResetFrame() {
	r.pointsDrawn = 0
	r.labels = r.labels[:0]
	for b := 0; b < r.batchesUsed; b++ {
		r.batches[b].quadCount = 0
	}
	r.batchesUsed = 0
}

func (r *Renderer) Unload() {
	for b := range r.batches {
		rl.UnloadMesh(&r.batches[b].mesh)
	}
	rl.UnloadMaterial(r.dotMaterial)
}
