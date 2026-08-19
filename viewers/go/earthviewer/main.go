// Go port of C/earth_viewer.c: a two-layer kd-tree (cities.metatree over
// city_tile_*.kdtree HEALPix-cell files, produced by geonames2kd +
// build_city_metatree) rendered as points on Earth's sphere, with an orbit
// camera and proximity-gated name labels. See geo.go/sphere.go/names.go/
// tiles.go/dots.go/camera.go for the pieces; this file wires them together.
package main

import (
	"fmt"
	"os"
	"strconv"

	rl "github.com/gen2brain/raylib-go/raylib"
)

const earthTexturePath = "earth_daymap.jpg"

func main() {
	if _, err := os.Stat("cities.metatree"); err != nil {
		fmt.Println("Error: cities.metatree not found in the current directory.")
		fmt.Println("Build it first (see README-cities.md):")
		fmt.Println("  ./geonames2kd cities1000.txt")
		fmt.Println("  ./build_city_metatree . cities")
		os.Exit(1)
	}

	fmt.Println("Loading city meta-index...")
	world, err := LoadWorld(".")
	if err != nil {
		fmt.Println("Error:", err)
		os.Exit(1)
	}
	defer world.Close()
	fmt.Printf("Meta-index ready: %d tiles indexed.\n", len(world.manifestPaths))

	names, err := LoadNames("cities.names")
	if err != nil {
		fmt.Println("Error loading cities.names:", err)
		os.Exit(1)
	}

	rl.SetConfigFlags(rl.FlagWindowResizable | rl.FlagVsyncHint)
	rl.InitWindow(1280, 720, "Earth Cities Viewer")
	defer rl.CloseWindow()

	renderer := NewRenderer() // needs a GL context, so after InitWindow
	defer renderer.Unload()

	// LoadTexture needs a GL context too. Textured Earth is optional - fall
	// back to a plain sphere if the file isn't there or fails to load.
	haveEarthTexture := false
	var earthMeshes []rl.Mesh
	var earthMaterial rl.Material
	if _, err := os.Stat(earthTexturePath); err == nil {
		earthTexture := rl.LoadTexture(earthTexturePath)
		if earthTexture.ID != 0 {
			// LoadTexture defaults to GL_NEAREST with no mipmaps - fine for
			// a flat-color sphere, but blocky/aliased up close for a real
			// texture. Mipmaps + trilinear filtering fix both.
			rl.GenTextureMipmaps(&earthTexture)
			rl.SetTextureFilter(earthTexture, rl.FilterTrilinear)

			earthMeshes = buildEarthMeshes(earthRadiusKm, earthSphereRings, earthSphereSlices)
			earthMaterial = rl.LoadMaterialDefault()
			earthMaterial.GetMap(rl.MapAlbedo).Texture = earthTexture
			haveEarthTexture = true
			fmt.Printf("Loaded Earth texture: %s (%d mesh band%s)\n", earthTexturePath, len(earthMeshes), plural(len(earthMeshes)))
		} else {
			fmt.Printf("Warning: found %s but couldn't load it as a texture; using a plain sphere.\n", earthTexturePath)
		}
	} else {
		fmt.Printf("No %s found; using a plain sphere (see README-cities.md to add a real texture).\n", earthTexturePath)
	}

	// Start over Cody, Wyoming (44.52634 N, 109.05653 W, per cities1000.txt's
	// own entry for it) rather than an arbitrary point. EV_LON/EV_LAT/EV_ALT
	// override the starting camera position; EV_SCREENSHOT/
	// EV_SCREENSHOT_FRAME capture and quit - mirrors
	// test_earth_viewer_visual.sh's env-var-driven smoke test convention,
	// since there's no interactive test harness for a GUI app.
	oc := OrbitCamera{Lon: -109.05653, Lat: 44.52634, Altitude: initialAltitudeKm}
	if v := os.Getenv("EV_LON"); v != "" {
		oc.Lon, _ = strconv.ParseFloat(v, 64)
	}
	if v := os.Getenv("EV_LAT"); v != "" {
		oc.Lat, _ = strconv.ParseFloat(v, 64)
	}
	if v := os.Getenv("EV_ALT"); v != "" {
		alt, _ := strconv.ParseFloat(v, 64)
		oc.Altitude = float32(alt)
	}
	screenshotPath := os.Getenv("EV_SCREENSHOT")
	screenshotFrame := 30
	if v := os.Getenv("EV_SCREENSHOT_FRAME"); v != "" {
		if n, err := strconv.Atoi(v); err == nil {
			screenshotFrame = n
		}
	}
	frameCount := 0

	camera := rl.Camera3D{
		Fovy:       60.0,
		Projection: rl.CameraPerspective,
	}

	rl.SetTargetFPS(60)
	const nearClip, farClip = 1.0, 300000.0
	rl.SetClipPlanes(nearClip, farClip)

	for !rl.WindowShouldClose() {
		frameCount++
		deltaTime := rl.GetFrameTime()
		oc.Update(&camera, deltaTime)

		currentWidth := int32(rl.GetScreenWidth())
		currentHeight := int32(rl.GetScreenHeight())
		aspect := float32(currentWidth) / float32(currentHeight)

		matView := rl.MatrixLookAt(camera.Position, camera.Target, camera.Up)
		matProj := rl.MatrixPerspective(camera.Fovy*rl.Deg2rad, aspect, nearClip, farClip)
		matViewProj := rl.MatrixMultiply(matView, matProj)
		frustum := ExtractFrustumPlanes(matViewProj)
		horizonTest := ComputeHorizonTest(camera.Position)

		camForward := rl.Vector3Normalize(rl.Vector3Subtract(camera.Target, camera.Position))
		renderer.billboardRight = rl.Vector3Normalize(rl.Vector3CrossProduct(camForward, camera.Up))
		renderer.billboardUp = rl.Vector3Normalize(rl.Vector3CrossProduct(renderer.billboardRight, camForward))

		renderer.ResetFrame()

		rl.BeginDrawing()
		rl.ClearBackground(rl.NewColor(5, 5, 15, 255))

		rl.BeginMode3D(camera)
		// Backface culling stays on for the Earth mesh itself - see
		// sphere.go's genEarthSphereMeshBand comment on triangle winding.
		rl.EnableBackfaceCulling()
		if haveEarthTexture {
			for _, m := range earthMeshes {
				rl.DrawMesh(m, earthMaterial, rl.MatrixIdentity())
			}
		} else {
			rl.DrawSphere(rl.NewVector3(0, 0, 0), earthRadiusKm, rl.NewColor(25, 60, 95, 255))
			rl.DrawSphereWires(rl.NewVector3(0, 0, 0), earthRadiusKm*1.001, 18, 36, rl.NewColor(255, 255, 255, 40))
		}

		// City-dot billboards are camera-facing quads, not a wound mesh -
		// backface culling has to stay off for them regardless of the
		// Earth mesh's winding.
		rl.DisableBackfaceCulling()
		world.WalkMetaTree(renderer, 0, frustum, horizonTest, camera.Position, oc.Altitude, names)
		renderer.FlushAndDraw()
		rl.EnableBackfaceCulling()
		rl.EndMode3D()

		// Labels are plain 2D screen text, drawn after EndMode3D via
		// GetWorldToScreen - every position here already passed the same
		// horizon+frustum test as its dot, so it's reliably in front of
		// the camera.
		for _, label := range renderer.labels {
			sp := rl.GetWorldToScreen(label.WorldPos, camera)
			tw := rl.MeasureText(label.Name, 14)
			rl.DrawRectangle(int32(sp.X)-2, int32(sp.Y)-2, tw+4, 16, rl.NewColor(0, 0, 0, 140))
			rl.DrawText(label.Name, int32(sp.X), int32(sp.Y), 14, rl.RayWhite)
		}

		rl.DrawFPS(10, 10)
		rl.DrawText(fmt.Sprintf("Points drawn this frame: %d", renderer.pointsDrawn), 10, 35, 18, rl.Green)
		rl.DrawText(fmt.Sprintf("Camera: lon=%.2f lat=%.2f altitude=%.0f km", oc.Lon, oc.Lat, oc.Altitude), 10, 58, 16, rl.RayWhite)
		rl.DrawText(fmt.Sprintf("Tiles mmap'd so far: %d / %d", world.tilesLoaded, len(world.manifestPaths)), 10, 80, 16, rl.RayWhite)
		rl.DrawText("Controls: drag with left mouse to orbit, scroll/W-S to zoom", 10, currentHeight-30, 14, rl.SkyBlue)

		rl.EndDrawing()

		// Screenshot is taken relative to the process's working directory
		// regardless of what path is given - mirrors
		// test_earth_viewer_visual.sh's convention.
		if screenshotPath != "" && frameCount == screenshotFrame {
			rl.TakeScreenshot(screenshotPath)
			break
		}
	}

	if haveEarthTexture {
		for i := range earthMeshes {
			rl.UnloadMesh(&earthMeshes[i])
		}
		rl.UnloadMaterial(earthMaterial) // also unloads earthMaterial's texture
	}

	fmt.Println("Viewer closed successfully.")
}

func plural(n int) string {
	if n == 1 {
		return ""
	}
	return "s"
}
