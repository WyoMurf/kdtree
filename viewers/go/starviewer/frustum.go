package main

import (
	rl "github.com/gen2brain/raylib-go/raylib"
)

// Plane is a frustum half-space: ax + by + cz + d >= 0 is "inside". Verbatim
// from viewer.c (earth_viewer.c later copied the same math from here).
type Plane struct{ A, B, C, D float32 }

// ExtractFrustumPlanes is Gribb-Hartmann plane extraction from a combined
// view*projection matrix. raylib's Matrix stores rows as (m0,m4,m8,m12),
// (m1,m5,m9,m13), (m2,m6,m10,m14), (m3,m7,m11,m15); planes are (row3 +/-
// rowN).
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

// AABBOutsideFrustum is the positive-vertex (p-vertex) box/frustum test:
// for each plane, pick the AABB corner most aligned with the plane's
// normal. If even that corner is on the negative side, the whole box is
// outside this plane (and thus the frustum).
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
