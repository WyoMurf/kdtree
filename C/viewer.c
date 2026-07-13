#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <glob.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <math.h>

#include "raylib.h"
#include "rlgl.h"
#include "raymath.h"

// Struct definition matching packed C mmap nodes from fits2kd
#pragma pack(push, 1)
typedef struct {
    uint64_t source_id;
    int64_t size[6];
    int64_t lo_min_bound;
    int64_t hi_max_bound;
    int64_t other_bound;
    int64_t left_child;
    int64_t right_child;
} kd_mmap_node_64;
#pragma pack(pop)

// Custom camera flying controls
void UpdateFreeCamera(Camera3D *camera, float *speed, float deltaTime) {
    // Keyboard speed adjustments (exponential)
    if (IsKeyDown(KEY_UP)) *speed *= 1.05f;
    if (IsKeyDown(KEY_DOWN)) *speed /= 1.05f;
    
    // Clamp speed to reasonable ranges (0.1 to 1000 parsecs per second)
    if (*speed < 0.1f) *speed = 0.1f;
    if (*speed > 1000.0f) *speed = 1000.0f;

    // Calculate direction vectors
    Vector3 forward = Vector3Subtract(camera->target, camera->position);
    float distance = Vector3Length(forward);
    forward = Vector3Scale(forward, 1.0f / distance); // Normalize
    
    Vector3 right = Vector3CrossProduct(forward, camera->up);
    right = Vector3Normalize(right);

    // Movement direction input mapping
    Vector3 move = { 0 };
    if (IsKeyDown(KEY_W)) move = Vector3Add(move, forward);
    if (IsKeyDown(KEY_S)) move = Vector3Subtract(move, forward);
    if (IsKeyDown(KEY_D)) move = Vector3Add(move, right);
    if (IsKeyDown(KEY_A)) move = Vector3Subtract(move, right);
    if (IsKeyDown(KEY_E)) move = Vector3Add(move, camera->up);
    if (IsKeyDown(KEY_Q)) move = Vector3Subtract(move, camera->up);

    if (Vector3Length(move) > 0.0f) {
        move = Vector3Normalize(move);
        Vector3 displacement = Vector3Scale(move, (*speed) * deltaTime);
        camera->position = Vector3Add(camera->position, displacement);
        camera->target = Vector3Add(camera->target, displacement);
    }

    // Camera rotation using mouse clicks & drags
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT)) {
        Vector2 mouseDelta = GetMouseDelta();
        if (mouseDelta.x != 0.0f || mouseDelta.y != 0.0f) {
            float sensitivity = 0.003f;
            float angleX = -mouseDelta.x * sensitivity;
            float angleY = -mouseDelta.y * sensitivity;

            // Rotate around Up axis (yaw)
            Vector3 targetOffset = Vector3Subtract(camera->target, camera->position);
            targetOffset = Vector3RotateByAxisAngle(targetOffset, camera->up, angleX);

            // Rotate around Right axis (pitch)
            targetOffset = Vector3RotateByAxisAngle(targetOffset, right, angleY);

            camera->target = Vector3Add(camera->position, targetOffset);
        }
    }
}

int main(int argc, char **argv) {
    printf("Scanning /backup/star-catalogs/ for .kdtree files...\n");
    
    glob_t glob_result;
    int glob_ret = glob("/backup/star-catalogs/GaiaSource_Filtered_*.kdtree", GLOB_ERR, NULL, &glob_result);
    
    if (glob_ret != 0) {
        printf("No .kdtree files found in /backup/star-catalogs/. Please run build_kdtrees.sh first.\n");
        return 1;
    }
    
    printf("Found %zu KD-Tree files to load.\n", glob_result.gl_pathc);
    
    // Allocate coordinate buffer dynamically (each star is 3 floats)
    size_t capacity = 1000000; // Start with capacity for 1 million stars
    float *positions = malloc(capacity * 3 * sizeof(float));
    size_t star_count = 0;
    
    // Keep track of average star position to point the camera
    double sumX = 0, sumY = 0, sumZ = 0;
    
    for (size_t i = 0; i < glob_result.gl_pathc; i++) {
        char *path = glob_result.gl_pathv[i];
        int fd = open(path, O_RDONLY);
        if (fd == -1) {
            printf("Warning: Could not open %s, skipping.\n", path);
            continue;
        }
        
        struct stat sb;
        if (fstat(fd, &sb) == -1 || sb.st_size == 0) {
            close(fd);
            continue;
        }
        
        size_t node_count = sb.st_size / sizeof(kd_mmap_node_64);
        kd_mmap_node_64 *nodes = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
        
        if (nodes == MAP_FAILED) {
            printf("Warning: mmap failed for %s, skipping.\n", path);
            close(fd);
            continue;
        }
        
        char *base_name = strrchr(path, '/');
        base_name = base_name ? base_name + 1 : path;
        printf("Loading %s (%zu stars)...\n", base_name, node_count);
        
        // Ensure enough capacity
        if (star_count + node_count > capacity) {
            while (star_count + node_count > capacity) {
                capacity *= 2;
            }
            positions = realloc(positions, capacity * 3 * sizeof(float));
        }
        
        // Read active stars (source_id != 0)
        for (size_t n = 0; n < node_count; n++) {
            if (nodes[n].source_id != 0) {
                // Extract exact mathematical center from 3D bounding box [min, max]
                // Compatible with both old (min == max) and new (min = x - R, max = x + R) kdtree files.
                float fx = (float)(nodes[n].size[0] + nodes[n].size[3]) / 2000000000.0f;
                float fy = (float)(nodes[n].size[1] + nodes[n].size[4]) / 2000000000.0f;
                float fz = (float)(nodes[n].size[2] + nodes[n].size[5]) / 2000000000.0f;
                
                positions[star_count * 3 + 0] = fx;
                positions[star_count * 3 + 1] = fy;
                positions[star_count * 3 + 2] = fz;
                
                sumX += fx;
                sumY += fy;
                sumZ += fz;
                star_count++;
            }
        }
        
        munmap(nodes, sb.st_size);
        close(fd);
    }
    
    globfree(&glob_result);
    
    if (star_count == 0) {
        printf("Error: Loaded 0 stars. Cannot launch viewer.\n");
        free(positions);
        return 1;
    }
    
    Vector3 avgPos = {
        (float)(sumX / star_count),
        (float)(sumY / star_count),
        (float)(sumZ / star_count)
    };
    
    printf("Successfully loaded %zu stars into memory.\n", star_count);
    printf("Average direction of loaded catalog sector: (%.1f, %.1f, %.1f) pc\n", avgPos.x, avgPos.y, avgPos.z);
    printf("Launching Raylib 3D Viewer...\n");
    
    // Initialize Raylib window
    const int screenWidth = 1280;
    const int screenHeight = 720;
    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_VSYNC_HINT);
    InitWindow(screenWidth, screenHeight, "Gaia 3D Star Catalog Visualizer");
    
    // Setup camera pointing directly at the center of the loaded stars sector!
    Camera3D camera = { 0 };
    camera.position = (Vector3){ 0.0f, 0.0f, 0.0f }; // Centered at Sun/Earth
    camera.target = avgPos; // Look directly at the loaded star sector
    camera.up = (Vector3){ 0.0f, 1.0f, 0.0f };
    camera.fovy = 60.0f;
    camera.projection = CAMERA_PERSPECTIVE;
    
    float speed = 20.0f; // Speed in parsecs per second
    SetTargetFPS(60);
    
    // Set custom near/far clip planes so distant stars up to 50,000 pc are visible
    rlSetClipPlanes(0.1, 50000.0);
    
    while (!WindowShouldClose()) {
        float deltaTime = GetFrameTime();
        
        // Handle custom free-flying camera controls
        UpdateFreeCamera(&camera, &speed, deltaTime);
        
        // Handle window resizing
        int currentWidth = GetScreenWidth();
        int currentHeight = GetScreenHeight();
        
        BeginDrawing();
        ClearBackground(BLACK);
        
        BeginMode3D(camera);
            // Draw axis reference for spatial awareness (X=Red, Y=Green, Z=Blue)
            DrawLine3D((Vector3){0,0,0}, (Vector3){100,0,0}, RED);
            DrawLine3D((Vector3){0,0,0}, (Vector3){0,100,0}, GREEN);
            DrawLine3D((Vector3){0,0,0}, (Vector3){0,0,100}, BLUE);
            
            // Render stars dynamically using our distance-scaled line segments
            rlBegin(RL_LINES);
            for (size_t i = 0; i < star_count; i++) {
                float fx = positions[i * 3 + 0];
                float fy = positions[i * 3 + 1];
                float fz = positions[i * 3 + 2];
                
                // Calculate distance from camera to render relative apparent brightness
                float dx = fx - camera.position.x;
                float dy = fy - camera.position.y;
                float dz = fz - camera.position.z;
                float d = sqrtf(dx*dx + dy*dy + dz*dz);
                
                unsigned char alpha = 255;
                float line_len = 0.05f;
                
                // Physically accurate apparent brightness mapping: closer = brighter, larger footprint
                if (d < 50.0f) {
                    alpha = 255;
                    line_len = 0.20f; // Nearby stars are larger and bright
                } else if (d > 3000.0f) {
                    alpha = 45;       // Distant stars fade to faint background pinpoints
                    line_len = 0.01f;
                } else {
                    float t = (d - 50.0f) / 2950.0f; // 0.0 at 50pc, 1.0 at 3000pc
                    alpha = (unsigned char)(255.0f - t * 210.0f); // 255 down to 45
                    line_len = 0.20f - t * 0.19f;                 // 0.20 down to 0.01pc
                }
                
                rlColor4ub(255, 245, 218, alpha); // Warm yellow-white glowing stars
                rlVertex3f(fx, fy, fz);
                rlVertex3f(fx + line_len, fy, fz);
            }
            rlEnd();
        EndMode3D();
        
        // Render HUD Overlay
        DrawFPS(10, 10);
        DrawText(TextFormat("Total Stars Rendered: %zu", star_count), 10, 35, 20, GREEN);
        DrawText(TextFormat("Cam Position: (%.1f, %.1f, %.1f) pc", camera.position.x, camera.position.y, camera.position.z), 10, 60, 16, RAYWHITE);
        DrawText(TextFormat("Looking at Loaded Sector: (%.1f, %.1f, %.1f) pc", camera.target.x, camera.target.y, camera.target.z), 10, 80, 16, RAYWHITE);
        DrawText(TextFormat("Flight Speed: %.1f pc/s", speed), 10, 100, 16, YELLOW);
        
        DrawText("Controls:", 10, currentHeight - 90, 16, SKYBLUE);
        DrawText("W / S / A / D / Q / E : Fly Forward/Backward/Left/Right/Up/Down", 10, currentHeight - 70, 14, RAYWHITE);
        DrawText("Mouse Left/Right Click & Drag: Orbit / Look Around", 10, currentHeight - 50, 14, RAYWHITE);
        DrawText("UP / DOWN Arrow Keys : Adjust flight speed (exponentially)", 10, currentHeight - 30, 14, RAYWHITE);
        
        EndDrawing();
    }
    
    CloseWindow();
    free(positions);
    printf("Viewer closed successfully.\n");
    return 0;
}
