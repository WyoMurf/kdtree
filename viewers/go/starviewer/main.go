// Go port of C/viewer.c: a two-level kd-tree (catalog.metatree over
// per-shard .kdtree files, produced by fits2kd + build_metatree) of Gaia
// DR3 star positions, flown through with a free-fly camera and rendered
// with frustum-culled, angular-size LOD collapsing. See frustum.go/
// render.go/shard.go/camera.go for the pieces; this file wires them
// together.
package main

import (
	"fmt"
	"os"
	"strconv"

	rl "github.com/gen2brain/raylib-go/raylib"
)

func main() {
	// catalog.metatree/.metatree.lod/.manifest are produced by
	// build_metatree + kd2lod - a kd-tree over every shard file's own
	// bounding box. The viewer never eagerly opens every shard at startup;
	// it walks this meta-tree per frame and lazily mmaps only the shards
	// whose bounding box actually survives frustum culling.
	if _, err := os.Stat("catalog.metatree"); err != nil {
		fmt.Println("Error: catalog.metatree not found in the current directory.")
		fmt.Println("Build it first (run these from the directory containing your .kdtree shard files):")
		fmt.Println("  ./build_metatree . catalog")
		fmt.Println("  ./kd2lod catalog.metatree catalog.metatree.lod")
		os.Exit(1)
	}

	fmt.Println("Loading meta-index from current directory...")
	world, err := LoadWorld(".")
	if err != nil {
		fmt.Println("Error:", err)
		os.Exit(1)
	}
	defer world.Close()
	fmt.Printf("Meta-index ready: %d shard files indexed (mmap'd lazily as you fly).\n", len(world.manifestPaths))

	fmt.Println("Launching Raylib 3D Viewer...")
	rl.SetConfigFlags(rl.FlagWindowResizable | rl.FlagVsyncHint)
	rl.InitWindow(1280, 720, "Gaia 3D Star Catalog Visualizer")
	defer rl.CloseWindow()

	renderer := NewRenderer() // needs a GL context, so after InitWindow
	defer renderer.Unload()

	// Setup camera at the Sun/Earth, looking toward an arbitrary nearby
	// direction. There's no eagerly-computed "average star position" to
	// aim at - the meta-tree covers the whole catalog lazily, and any
	// direction is virtually guaranteed to have solar-neighborhood shards
	// nearby; fly/look around to reveal whatever's actually there.
	camera := rl.Camera3D{
		Position:   rl.NewVector3(0, 0, 0), // centered at Sun/Earth
		Target:     rl.NewVector3(0, 0, 20),
		Up:         rl.NewVector3(0, 1, 0),
		Fovy:       60.0,
		Projection: rl.CameraPerspective,
	}
	speed := float32(20.0) // parsecs per second

	// SV_CAM_*/SV_TARGET_* override the starting camera position/target;
	// SV_SCREENSHOT/SV_SCREENSHOT_FRAME capture and quit - the same
	// env-var-driven smoke-test convention earth_viewer.c established
	// (EV_LON/EV_LAT/EV_ALT/EV_SCREENSHOT), adapted to this viewer's
	// free-fly camera instead of an orbit camera. There's no interactive
	// test harness for a GUI app, so this is the automatable substitute.
	if v := os.Getenv("SV_CAM_X"); v != "" {
		if f, err := strconv.ParseFloat(v, 32); err == nil {
			camera.Position.X = float32(f)
		}
	}
	if v := os.Getenv("SV_CAM_Y"); v != "" {
		if f, err := strconv.ParseFloat(v, 32); err == nil {
			camera.Position.Y = float32(f)
		}
	}
	if v := os.Getenv("SV_CAM_Z"); v != "" {
		if f, err := strconv.ParseFloat(v, 32); err == nil {
			camera.Position.Z = float32(f)
		}
	}
	if v := os.Getenv("SV_TARGET_X"); v != "" {
		if f, err := strconv.ParseFloat(v, 32); err == nil {
			camera.Target.X = float32(f)
		}
	}
	if v := os.Getenv("SV_TARGET_Y"); v != "" {
		if f, err := strconv.ParseFloat(v, 32); err == nil {
			camera.Target.Y = float32(f)
		}
	}
	if v := os.Getenv("SV_TARGET_Z"); v != "" {
		if f, err := strconv.ParseFloat(v, 32); err == nil {
			camera.Target.Z = float32(f)
		}
	}
	screenshotPath := os.Getenv("SV_SCREENSHOT")
	screenshotFrame := 30
	if v := os.Getenv("SV_SCREENSHOT_FRAME"); v != "" {
		if n, err := strconv.Atoi(v); err == nil {
			screenshotFrame = n
		}
	}
	frameCount := 0

	rl.SetTargetFPS(60)

	// Custom near/far clip planes so distant stars up to 50,000 pc are visible.
	const nearClip, farClip = 0.1, 50000.0
	rl.SetClipPlanes(nearClip, farClip)

	lodPixelTarget := float32(2.0)

	for !rl.WindowShouldClose() {
		frameCount++
		deltaTime := rl.GetFrameTime()

		UpdateFreeCamera(&camera, &speed, deltaTime)

		currentWidth := int32(rl.GetScreenWidth())
		currentHeight := int32(rl.GetScreenHeight())
		aspect := float32(currentWidth) / float32(currentHeight)

		// Manual LOD tuning: '[' = more detail/slower, ']' = coarser/faster
		if rl.IsKeyDown(rl.KeyLeftBracket) {
			lodPixelTarget *= 1.0 - 1.5*deltaTime
		}
		if rl.IsKeyDown(rl.KeyRightBracket) {
			lodPixelTarget *= 1.0 + 1.5*deltaTime
		}

		// Gentle auto-adaptation toward a comfortable frame time.
		if deltaTime > 1.0/30.0 {
			lodPixelTarget *= 1.01
		} else if deltaTime < 1.0/55.0 {
			lodPixelTarget *= 0.998
		}
		if lodPixelTarget < lodPixelTargetMin {
			lodPixelTarget = lodPixelTargetMin
		}
		if lodPixelTarget > lodPixelTargetMax {
			lodPixelTarget = lodPixelTargetMax
		}

		// Build this frame's view frustum for subtree culling.
		matView := rl.MatrixLookAt(camera.Position, camera.Target, camera.Up)
		matProj := rl.MatrixPerspective(camera.Fovy*rl.Deg2rad, aspect, nearClip, farClip)
		matViewProj := rl.MatrixMultiply(matView, matProj)
		frustum := ExtractFrustumPlanes(matViewProj)

		// Subtrees collapse once they'd subtend fewer than this many pixels.
		radPerPixel := (camera.Fovy * rl.Deg2rad) / float32(currentHeight)
		angleThreshold := radPerPixel * lodPixelTarget

		// Recomputed once per frame (not per star) so every star's
		// billboard faces the camera regardless of view direction.
		camForward := rl.Vector3Normalize(rl.Vector3Subtract(camera.Target, camera.Position))
		renderer.billboardRight = rl.Vector3Normalize(rl.Vector3CrossProduct(camForward, camera.Up))
		renderer.billboardUp = rl.Vector3Normalize(rl.Vector3CrossProduct(renderer.billboardRight, camForward))

		renderer.ResetFrame()
		world.shardLoadsThisFrame = 0

		rl.BeginDrawing()
		rl.ClearBackground(rl.Black)

		rl.BeginMode3D(camera)
		// Draw axis reference for spatial awareness (X=Red, Y=Green, Z=Blue).
		rl.DrawLine3D(rl.NewVector3(0, 0, 0), rl.NewVector3(100, 0, 0), rl.Red)
		rl.DrawLine3D(rl.NewVector3(0, 0, 0), rl.NewVector3(0, 100, 0), rl.Green)
		rl.DrawLine3D(rl.NewVector3(0, 0, 0), rl.NewVector3(0, 0, 100), rl.Blue)

		// Walk the meta-tree of shards: cull whole subtrees of shards
		// against the view frustum, collapse distant ones to a single
		// representative point, and lazily mmap + walk only the shards
		// that actually matter this frame. Stars are billboarded quads, so
		// backface culling is disabled for this batch - a quad built from
		// the camera's own right/up vectors always faces the camera.
		rl.DisableBackfaceCulling()
		rl.SetTexture(renderer.starTexture.ID)
		rl.Begin(rl.Quads)
		world.CullAndCollectMeta(renderer, 0, frustum, camera.Position, angleThreshold)
		rl.End()
		rl.SetTexture(rl.GetTextureIdDefault())
		rl.EnableBackfaceCulling()
		rl.EndMode3D()

		// If the hard point budget was hit this frame, some subtree's
		// bounding box was too large relative to distance for
		// angular-size culling to collapse it fast enough - jump the LOD
		// threshold up sharply rather than waiting on the gentle
		// auto-adapt above, so it recovers in a frame or two instead of
		// staying pegged at the budget cap.
		if renderer.budgetHit {
			lodPixelTarget *= 1.5
		}

		rl.DrawFPS(10, 10)
		budgetSuffix := ""
		statsColor := rl.Green
		if renderer.budgetHit {
			budgetSuffix = " [BUDGET CAP HIT]"
			statsColor = rl.Orange
		}
		rl.DrawText(fmt.Sprintf("Points Drawn This Frame: %d%s (tree nodes: %d expanded / %d collapsed)",
			renderer.pointsDrawn, budgetSuffix, renderer.nodesExpanded, renderer.nodesCollapsed), 10, 35, 18, statsColor)
		rl.DrawText(fmt.Sprintf("Shards mmap'd so far: %d / %d indexed | Stars discovered: ~%d",
			world.shardsLoadedCount, len(world.manifestPaths), world.starsDiscovered), 10, 58, 16, rl.RayWhite)
		rl.DrawText(fmt.Sprintf("Cam Position: (%.1f, %.1f, %.1f) pc", camera.Position.X, camera.Position.Y, camera.Position.Z), 10, 80, 16, rl.RayWhite)
		rl.DrawText(fmt.Sprintf("Looking at Loaded Sector: (%.1f, %.1f, %.1f) pc", camera.Target.X, camera.Target.Y, camera.Target.Z), 10, 100, 16, rl.RayWhite)
		rl.DrawText(fmt.Sprintf("Flight Speed: %.1f pc/s", speed), 10, 120, 16, rl.Yellow)
		rl.DrawText(fmt.Sprintf("LOD Target: %.2f px/subtree", lodPixelTarget), 10, 140, 16, rl.Yellow)

		rl.DrawText("Controls:", 10, currentHeight-110, 16, rl.SkyBlue)
		rl.DrawText("W / S / A / D / Q / E : Fly Forward/Backward/Left/Right/Up/Down", 10, currentHeight-90, 14, rl.RayWhite)
		rl.DrawText("Mouse Left/Right Click & Drag: Orbit / Look Around", 10, currentHeight-70, 14, rl.RayWhite)
		rl.DrawText("UP / DOWN Arrow Keys : Adjust flight speed (exponentially)", 10, currentHeight-50, 14, rl.RayWhite)
		rl.DrawText("[ / ] : More detail (slower) / Coarser detail (faster)", 10, currentHeight-30, 14, rl.RayWhite)

		rl.EndDrawing()

		if screenshotPath != "" && frameCount == screenshotFrame {
			rl.TakeScreenshot(screenshotPath)
			break
		}
	}

	fmt.Println("Viewer closed successfully.")
}
