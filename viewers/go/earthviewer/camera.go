package main

import (
	rl "github.com/gen2brain/raylib-go/raylib"
)

const (
	minAltitudeKm     float32 = 20.0
	maxAltitudeKm     float32 = 150000.0
	initialAltitudeKm float32 = 20000.0 // ~3.1 Earth radii out: a "respectful distance"
)

// OrbitCamera is (lon, lat, altitude) rather than viewer.c's free-fly
// scheme, since "start at a respectful distance, move closer" only makes
// sense with a genuine distance-from-center concept.
type OrbitCamera struct {
	Lon, Lat float64
	Altitude float32
}

func (oc *OrbitCamera) Update(camera *rl.Camera3D, deltaTime float32) {
	if rl.IsMouseButtonDown(rl.MouseButtonLeft) {
		d := rl.GetMouseDelta()
		// Drag sensitivity scales with altitude so panning feels
		// proportional at both a full-globe view and a close-up one.
		degPerPixel := 0.02 * (float64(oc.Altitude)/float64(earthRadiusKm) + 0.05)
		oc.Lon -= float64(d.X) * degPerPixel
		oc.Lat += float64(d.Y) * degPerPixel
		if oc.Lat > 89.0 {
			oc.Lat = 89.0
		}
		if oc.Lat < -89.0 {
			oc.Lat = -89.0
		}
		if oc.Lon > 180.0 {
			oc.Lon -= 360.0
		}
		if oc.Lon < -180.0 {
			oc.Lon += 360.0
		}
	}

	zoom := 1.0 - rl.GetMouseWheelMove()*0.1
	if rl.IsKeyDown(rl.KeyW) || rl.IsKeyDown(rl.KeyUp) {
		zoom *= 1.0 - 0.8*deltaTime
	}
	if rl.IsKeyDown(rl.KeyS) || rl.IsKeyDown(rl.KeyDown) {
		zoom *= 1.0 + 0.8*deltaTime
	}
	oc.Altitude *= zoom
	if oc.Altitude < minAltitudeKm {
		oc.Altitude = minAltitudeKm
	}
	if oc.Altitude > maxAltitudeKm {
		oc.Altitude = maxAltitudeKm
	}

	surfacePoint := LonLatToCartesian(oc.Lon, oc.Lat, earthRadiusKm)
	outward := rl.Vector3Normalize(surfacePoint)
	camera.Position = rl.Vector3Add(surfacePoint, rl.Vector3Scale(outward, oc.Altitude))
	camera.Target = rl.NewVector3(0, 0, 0)

	// "Up" is the local north tangent direction, not a blindly-fixed world
	// axis - projecting world-Y onto the tangent plane at the camera's
	// sub-point keeps the view from flipping/degenerating as you orbit
	// around, which a fixed (0,1,0) up would do near the poles. Only
	// undefined exactly at the poles, which the +/-89 degree clamp above
	// avoids.
	worldUp := rl.NewVector3(0, 1, 0)
	north := rl.Vector3Subtract(worldUp, rl.Vector3Scale(outward, rl.Vector3DotProduct(worldUp, outward)))
	camera.Up = rl.Vector3Normalize(north)
}
