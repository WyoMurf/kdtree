package main

import (
	rl "github.com/gen2brain/raylib-go/raylib"
)

// UpdateFreeCamera is a custom camera flying control scheme: WASD/QE to
// move, click-drag with either mouse button to look around, up/down arrows
// to adjust flight speed exponentially.
func UpdateFreeCamera(camera *rl.Camera3D, speed *float32, deltaTime float32) {
	if rl.IsKeyDown(rl.KeyUp) {
		*speed *= 1.05
	}
	if rl.IsKeyDown(rl.KeyDown) {
		*speed /= 1.05
	}
	if *speed < 0.1 {
		*speed = 0.1
	}
	if *speed > 1000.0 {
		*speed = 1000.0
	}

	forward := rl.Vector3Subtract(camera.Target, camera.Position)
	distance := rl.Vector3Length(forward)
	forward = rl.Vector3Scale(forward, 1.0/distance)

	right := rl.Vector3Normalize(rl.Vector3CrossProduct(forward, camera.Up))

	move := rl.Vector3{}
	if rl.IsKeyDown(rl.KeyW) {
		move = rl.Vector3Add(move, forward)
	}
	if rl.IsKeyDown(rl.KeyS) {
		move = rl.Vector3Subtract(move, forward)
	}
	if rl.IsKeyDown(rl.KeyD) {
		move = rl.Vector3Add(move, right)
	}
	if rl.IsKeyDown(rl.KeyA) {
		move = rl.Vector3Subtract(move, right)
	}
	if rl.IsKeyDown(rl.KeyE) {
		move = rl.Vector3Add(move, camera.Up)
	}
	if rl.IsKeyDown(rl.KeyQ) {
		move = rl.Vector3Subtract(move, camera.Up)
	}

	if rl.Vector3Length(move) > 0.0 {
		move = rl.Vector3Normalize(move)
		displacement := rl.Vector3Scale(move, (*speed)*deltaTime)
		camera.Position = rl.Vector3Add(camera.Position, displacement)
		camera.Target = rl.Vector3Add(camera.Target, displacement)
	}

	if rl.IsMouseButtonDown(rl.MouseButtonLeft) || rl.IsMouseButtonDown(rl.MouseButtonRight) {
		mouseDelta := rl.GetMouseDelta()
		if mouseDelta.X != 0.0 || mouseDelta.Y != 0.0 {
			const sensitivity = 0.003
			angleX := -mouseDelta.X * sensitivity
			angleY := -mouseDelta.Y * sensitivity

			targetOffset := rl.Vector3Subtract(camera.Target, camera.Position)
			targetOffset = rl.Vector3RotateByAxisAngle(targetOffset, camera.Up, angleX) // yaw
			targetOffset = rl.Vector3RotateByAxisAngle(targetOffset, right, angleY)     // pitch

			camera.Target = rl.Vector3Add(camera.Position, targetOffset)
		}
	}
}
