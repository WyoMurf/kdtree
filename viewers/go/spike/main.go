// Spike: confirms the cgo + raylib-go toolchain builds and runs cleanly here,
// and exercises the handful of raylib APIs the real viewer ports will need:
// Camera3D, rlgl immediate-mode quads (rl.Begin/Vertex3f/Color4ub), Mesh/
// Material/DrawMesh, and screenshotting via env vars (mirrors
// test_earth_viewer_visual.sh's EV_SCREENSHOT convention).
package main

import (
	"fmt"
	"os"
	"strconv"

	rl "github.com/gen2brain/raylib-go/raylib"
)

func main() {
	rl.SetConfigFlags(rl.FlagWindowResizable | rl.FlagVsyncHint)
	rl.InitWindow(1280, 720, "raylib-go spike: cube + rlgl quad")
	defer rl.CloseWindow()

	camera := rl.Camera3D{
		Position:   rl.NewVector3(6, 6, 6),
		Target:     rl.NewVector3(0, 0, 0),
		Up:         rl.NewVector3(0, 1, 0),
		Fovy:       60,
		Projection: rl.CameraPerspective,
	}

	rl.SetTargetFPS(60)

	screenshotPath := os.Getenv("SPIKE_SCREENSHOT")
	screenshotFrame := 30
	if v := os.Getenv("SPIKE_SCREENSHOT_FRAME"); v != "" {
		if n, err := strconv.Atoi(v); err == nil {
			screenshotFrame = n
		}
	}
	frame := 0
	angle := float32(0)

	for !rl.WindowShouldClose() {
		frame++
		angle += rl.GetFrameTime() * 45

		rl.BeginDrawing()
		rl.ClearBackground(rl.Black)

		rl.BeginMode3D(camera)

		rl.PushMatrix()
		rl.Rotatef(angle, 0, 1, 0)
		rl.DrawCube(rl.NewVector3(0, 0, 0), 2, 2, 2, rl.Red)
		rl.DrawCubeWires(rl.NewVector3(0, 0, 0), 2, 2, 2, rl.RayWhite)
		rl.PopMatrix()

		// rlgl immediate-mode billboard quad, same shape as DrawStarPoint /
		// DrawCityPoint in the real viewers: a camera-facing quad built from
		// raw rl.Begin/Vertex3f/Color4ub calls rather than a helper function.
		rl.DisableBackfaceCulling()
		rl.Begin(rl.Quads)
		rl.Color4ub(80, 200, 255, 255)
		rl.Vertex3f(-3, -1, 0)
		rl.Vertex3f(-1, -1, 0)
		rl.Vertex3f(-1, 1, 0)
		rl.Vertex3f(-3, 1, 0)
		rl.End()
		rl.EnableBackfaceCulling()

		rl.EndMode3D()

		rl.DrawFPS(10, 10)
		rl.DrawText("raylib-go spike: cube + rlgl quad", 10, 35, 18, rl.RayWhite)
		rl.EndDrawing()

		if screenshotPath != "" && frame == screenshotFrame {
			rl.TakeScreenshot(screenshotPath)
			break
		}
	}

	fmt.Println("Spike closed successfully.")
}
