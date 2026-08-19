package main

import (
	"math"

	rl "github.com/gen2brain/raylib-go/raylib"

	"github.com/WyoMurf/kdtree/viewers/go/kdmmap"
)

// scaleFactor must match fits2kd.c's fixed-point scale (parsecs -> integer
// units).
const scaleFactor = 1000000000.0

// Hard safety valve: some subtrees (e.g. a parallax segment spanning
// thousands of parsecs) have a bounding box large enough that angular-size
// culling alone doesn't collapse them from certain camera positions, which
// would otherwise make a single frame's traversal unboundedly expensive.
// Once this many points are drawn in a frame, every further cull call
// returns immediately - the frame always finishes quickly, and the
// auto-adapt in main.go reacts on the next frame.
const framePointBudget = 1500000

// A second, independent safety valve: opening a burst of never-before-seen
// shard files in a single frame (e.g. flying fast into unexplored
// territory) blocks on disk I/O for however long that many opens/mmaps
// take. Capping new loads per frame bounds worst-case frame time the same
// way framePointBudget does for drawing.
const maxShardLoadsPerFrame = 64

// LOD aggressiveness bounds: a subtree collapses to one representative
// point once its angular size (as seen from the camera) drops below this
// many screen pixels. Smaller = more detail + slower, larger = coarser +
// faster. Tuned live with '[' / ']' and nudged automatically toward a
// comfortable frame time (see main.go's loop).
const (
	lodPixelTargetMin = 0.25
	lodPixelTargetMax = 64.0
)

// Renderer holds all per-frame render state: the billboard basis vectors
// and this frame's draw/cull stats.
type Renderer struct {
	billboardRight rl.Vector3
	billboardUp    rl.Vector3

	pointsDrawn    uint64
	nodesExpanded  uint64
	nodesCollapsed uint64
	budgetHit      bool

	starTexture rl.Texture2D
}

func NewRenderer() *Renderer {
	return &Renderer{
		billboardRight: rl.NewVector3(1, 0, 0),
		billboardUp:    rl.NewVector3(0, 1, 0),
		starTexture:    createStarTexture(),
	}
}

func (r *Renderer) Unload() {
	rl.UnloadTexture(r.starTexture)
}

func (r *Renderer) ResetFrame() {
	r.pointsDrawn = 0
	r.nodesExpanded = 0
	r.nodesCollapsed = 0
	r.budgetHit = false
}

// createStarTexture builds a small soft-edged circular glow sprite: opaque
// white core fading to fully transparent at the edge. Tinted per-star via
// rl.Color4ub in DrawStarPoint, so this only supplies the round
// shape/falloff, not the color itself.
func createStarTexture() rl.Texture2D {
	img := rl.GenImageGradientRadial(64, 64, 0.15, rl.White, rl.NewColor(255, 255, 255, 0))
	tex := rl.LoadTextureFromImage(img)
	rl.UnloadImage(img)
	rl.GenTextureMipmaps(&tex)
	rl.SetTextureFilter(tex, rl.FilterTrilinear) // avoids shimmering as distant stars shrink sub-pixel
	return tex
}

// NodeStarPos returns the exact mathematical center from the 3D bounding
// box [min, max].
func NodeStarPos(n *kdmmap.Node3I64) rl.Vector3 {
	return rl.NewVector3(
		float32(float64(n.Size[0]+n.Size[3])/(2.0*scaleFactor)),
		float32(float64(n.Size[1]+n.Size[4])/(2.0*scaleFactor)),
		float32(float64(n.Size[2]+n.Size[5])/(2.0*scaleFactor)),
	)
}

// StarBrightness is a physically-motivated apparent-brightness mapping:
// closer = brighter/larger. When a single point stands in for a collapsed
// subtree of hiddenCount unresolved stars, brighten/enlarge it a bit so
// dense regions still read as dense from far away, instead of looking like
// one dim star.
func StarBrightness(dist float32, hiddenCount uint32) (alpha uint8, size float32) {
	var a uint8
	var sz float32

	switch {
	case dist < 50.0:
		a, sz = 255, 0.20
	case dist > 3000.0:
		a, sz = 45, 0.01
	default:
		t := (dist - 50.0) / 2950.0
		a = uint8(255.0 - t*210.0)
		sz = 0.20 - t*0.19
	}

	if hiddenCount > 1 {
		boost := 20.0 * log2f(float32(hiddenCount))
		boosted := int(a) + int(boost)
		if boosted > 255 {
			a = 255
		} else {
			a = uint8(boosted)
		}
		sz += 0.02 * log2f(float32(hiddenCount))
	}

	return a, sz
}

func log2f(x float32) float32 { return float32(math.Log2(float64(x))) }

// DrawStarPoint draws a camera-facing quad (billboarded via
// billboardRight/Up, recomputed once per frame in main.go, not per star)
// textured with the star texture's soft radial glow - looks round from
// every viewing angle and distance, unlike a fixed-axis world-space line
// segment. Must be called between rl.Begin(rl.Quads)/rl.End() with the
// star texture bound.
func (r *Renderer) DrawStarPoint(pos rl.Vector3, dist float32, hiddenCount uint32) {
	alpha, size := StarBrightness(dist, hiddenCount)

	right := rl.Vector3Scale(r.billboardRight, size)
	up := rl.Vector3Scale(r.billboardUp, size)
	p0 := rl.Vector3Subtract(rl.Vector3Subtract(pos, right), up) // bottom-left
	p1 := rl.Vector3Subtract(rl.Vector3Add(pos, right), up)      // bottom-right
	p2 := rl.Vector3Add(rl.Vector3Add(pos, right), up)           // top-right
	p3 := rl.Vector3Add(rl.Vector3Subtract(pos, right), up)      // top-left

	rl.Color4ub(255, 245, 218, alpha) // warm yellow-white glowing stars
	rl.TexCoord2f(0.0, 1.0)
	rl.Vertex3f(p0.X, p0.Y, p0.Z)
	rl.TexCoord2f(1.0, 1.0)
	rl.Vertex3f(p1.X, p1.Y, p1.Z)
	rl.TexCoord2f(1.0, 0.0)
	rl.Vertex3f(p2.X, p2.Y, p2.Z)
	rl.TexCoord2f(0.0, 0.0)
	rl.Vertex3f(p3.X, p3.Y, p3.Z)
	r.pointsDrawn++
}
