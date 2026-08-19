package main

import (
	"math"

	rl "github.com/gen2brain/raylib-go/raylib"
)

const earthRadiusKm float32 = 6371.0

func degToRad(d float64) float64 { return d * (math.Pi / 180.0) }

// LonLatToCartesian is verbatim from earth_viewer.c's function of the same
// name. Longitude is negated: raylib/OpenGL's right-handed, Y-up world means
// +sin(lon) for Z maps increasing (eastward) longitude to the geometrically
// wrong side once actually viewed through a standard look-at camera - the
// whole globe came out mirrored east-west. Negating flips it back without
// touching the north/south mapping (driven by Y alone) at all.
func LonLatToCartesian(lonDeg, latDeg float64, radiusKm float32) rl.Vector3 {
	lonRad := degToRad(-lonDeg)
	latRad := degToRad(latDeg)
	cl := math.Cos(latRad)
	return rl.NewVector3(
		float32(float64(radiusKm)*cl*math.Cos(lonRad)),
		float32(float64(radiusKm)*math.Sin(latRad)),
		float32(float64(radiusKm)*cl*math.Sin(lonRad)),
	)
}

// HorizonTest hoists the camera-only part of the sphere-horizon visibility
// test out to once per frame (see ComputeHorizonTest/IsAboveHorizonFast in
// earth_viewer.c): for a camera outside a sphere of radius R centered at the
// origin, a surface point is visible iff the angle between its direction
// from center and the camera's direction from center is within the
// tangent-line angle to the horizon.
type HorizonTest struct {
	camDir       rl.Vector3
	cosThreshold float32
	allInside    bool
}

func ComputeHorizonTest(camPos rl.Vector3) HorizonTest {
	d := rl.Vector3Length(camPos)
	if d <= earthRadiusKm {
		return HorizonTest{allInside: true} // shouldn't happen in practice
	}
	return HorizonTest{
		camDir:       rl.Vector3Scale(camPos, 1.0/d),
		cosThreshold: earthRadiusKm / d,
	}
}

// IsAboveHorizonFast assumes pointOnSphere already has magnitude
// earthRadiusKm exactly (true for every point LonLatToCartesian(...,
// earthRadiusKm) produces), so normalizing it is just a scale by a
// compile-time-known constant, not a sqrt.
func IsAboveHorizonFast(pointOnSphere rl.Vector3, ht HorizonTest) bool {
	if ht.allInside {
		return true
	}
	pointDir := rl.Vector3Scale(pointOnSphere, 1.0/earthRadiusKm)
	cosAngle := rl.Vector3DotProduct(pointDir, ht.camDir)
	return cosAngle >= ht.cosThreshold
}

// Plane is a frustum half-space: ax + by + cz + d >= 0 is "inside".
type Plane struct{ A, B, C, D float32 }

// ExtractFrustumPlanes is verbatim Gribb-Hartmann plane extraction from a
// combined view*projection matrix, ported from earth_viewer.c/viewer.c.
// raylib's Matrix stores rows as (m0,m4,m8,m12), (m1,m5,m9,m13),
// (m2,m6,m10,m14), (m3,m7,m11,m15); planes are (row3 +/- rowN).
func ExtractFrustumPlanes(m rl.Matrix) [6]Plane {
	return [6]Plane{
		{m.M3 + m.M0, m.M7 + m.M4, m.M11 + m.M8, m.M15 + m.M12},  // left
		{m.M3 - m.M0, m.M7 - m.M4, m.M11 - m.M8, m.M15 - m.M12},  // right
		{m.M3 + m.M1, m.M7 + m.M5, m.M11 + m.M9, m.M15 + m.M13},  // bottom
		{m.M3 - m.M1, m.M7 - m.M5, m.M11 - m.M9, m.M15 - m.M13},  // top
		{m.M3 + m.M2, m.M7 + m.M6, m.M11 + m.M10, m.M15 + m.M14}, // near
		{m.M3 - m.M2, m.M7 - m.M6, m.M11 - m.M10, m.M15 - m.M14}, // far
	}
}

func PointInFrustum(fr [6]Plane, p rl.Vector3) bool {
	for _, pl := range fr {
		if pl.A*p.X+pl.B*p.Y+pl.C*p.Z+pl.D < 0.0 {
			return false
		}
	}
	return true
}

// AABBOutsideFrustum is the positive-vertex (p-vertex) box/frustum test:
// for each plane, pick the AABB corner most aligned with the plane's normal.
// If even that corner is on the negative side, the whole box is outside
// this plane (and thus the frustum). Tests whether the box itself can
// overlap the frustum volume, not whether any sampled point lies inside it.
func AABBOutsideFrustum(fr [6]Plane, bmin, bmax rl.Vector3) bool {
	for _, pl := range fr {
		p := rl.Vector3{}
		if pl.A >= 0.0 {
			p.X = bmax.X
		} else {
			p.X = bmin.X
		}
		if pl.B >= 0.0 {
			p.Y = bmax.Y
		} else {
			p.Y = bmin.Y
		}
		if pl.C >= 0.0 {
			p.Z = bmax.Z
		} else {
			p.Z = bmin.Z
		}
		if pl.A*p.X+pl.B*p.Y+pl.C*p.Z+pl.D < 0.0 {
			return true
		}
	}
	return false
}

// CellVisible samples a grid across a HEALPix cell's lon/lat bounding box
// (not just its 4 corners - a box much larger than a narrow close-up
// frustum can have every sampled corner outside the frustum while its true
// extent still slices through it), builds the Cartesian AABB of those
// samples, and tests that box against the frustum. This only gates
// *loading* a tile, not final rendering - every point drawn still gets its
// own precise per-point horizon+frustum test in DrawTilePoints.
func CellVisible(lonMin, latMin, lonMax, latMax float64, fr [6]Plane, ht HorizonTest) bool {
	bmin := rl.NewVector3(1e9, 1e9, 1e9)
	bmax := rl.NewVector3(-1e9, -1e9, -1e9)
	anyAboveHorizon := false
	const steps = 6
	for i := 0; i <= steps; i++ {
		lon := lonMin + (lonMax-lonMin)*float64(i)/steps
		for j := 0; j <= steps; j++ {
			lat := latMin + (latMax-latMin)*float64(j)/steps
			p := LonLatToCartesian(lon, lat, earthRadiusKm)
			if p.X < bmin.X {
				bmin.X = p.X
			}
			if p.X > bmax.X {
				bmax.X = p.X
			}
			if p.Y < bmin.Y {
				bmin.Y = p.Y
			}
			if p.Y > bmax.Y {
				bmax.Y = p.Y
			}
			if p.Z < bmin.Z {
				bmin.Z = p.Z
			}
			if p.Z > bmax.Z {
				bmax.Z = p.Z
			}
			if IsAboveHorizonFast(p, ht) {
				anyAboveHorizon = true
			}
		}
	}
	if !anyAboveHorizon {
		return false
	}
	return !AABBOutsideFrustum(fr, bmin, bmax)
}
