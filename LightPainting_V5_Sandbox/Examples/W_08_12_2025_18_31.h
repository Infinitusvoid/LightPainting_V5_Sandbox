#include "WireUtil.h"

using namespace WireEngine;  // RenderSettings, LineParams, etc.

// -----------------------------------------------------------------------------
// Render settings  (unchanged, just renamed output_dir for clarity)
// -----------------------------------------------------------------------------
RenderSettings init_render_settings(const std::string& baseName,
    int seconds = 4)
{
    RenderSettings settings;

    // Resolution
    settings.width = 1920 / 2;
    settings.height = 1080 / 2;

    // frames / fps
    settings.frames = 60 * seconds;
    settings.fps = 60.0f;

    // Light-painting feel
    settings.accum_passes = 1; // enough jitter, still fast-ish

    // Blending
    settings.line_blend_mode = LineBlendMode::AdditiveLightPainting;

    // Glow / bloom
    settings.exposure = 1.8f;
    settings.bloom_threshold = 0.35f;
    settings.bloom_strength = 2.2f * 4.2f;
    settings.bloom_enabled = true;

    // Line softness & energy
    settings.soft_edge = 0.9f;
    settings.energy_per_hit = 2.0e-4f;
    settings.thickness_scale = 1.0f;

    // Capacity hint – plenty of room for tube + grid
    settings.max_line_segments_hint = 1000 * 1000 * 4;

    // Readback & IO
    settings.use_pbo = true;
    settings.output_dir = "frames_tube_push"; // only used in PNG mode

    // Output: unique video name
    settings.output_mode = OutputMode::FFmpegVideo;
    settings.ffmpeg_path = "ffmpeg";
    settings.ffmpeg_output = g_base_output_filepath + "/" + baseName + ".mp4";
    settings.ffmpeg_extra_args = "-c:v libx264 -preset veryfast -crf 18";

    return settings;
}

// -----------------------------------------------------------------------------
// Tube parameters
// -----------------------------------------------------------------------------
constexpr int TUBE_MAX_POINTS = 260;

// -----------------------------------------------------------------------------
// Scene parameters  (shared state for camera + tube path)
// -----------------------------------------------------------------------------
struct SceneParams
{
    // --- Camera base values (orbiting around the whole construction) ---
    float camera_base_radius = 260.0f;
    float camera_radius_breath = 40.0f;

    float camera_base_height = 40.0f;
    float camera_height_breath = 20.0f;

    float camera_base_fov = 55.0f;
    float camera_fov_breath = 15.0f;

    // Per-frame offsets written by camera_callback, read by line_push_callback
    float camera_radius_offset = 0.0f;
    float camera_height_offset = 0.0f;
    float camera_fov_offset = 0.0f;

    // Generic phase you can reuse in multiple places
    float shared_phase = 0.0f;

    // Camera basis & position (computed in camera_callback)
    Vec3 cam_eye{ 0.0f, 80.0f, 320.0f };
    Vec3 cam_target{ 0.0f,  0.0f,   0.0f };
    Vec3 cam_forward{ 0.0f,  0.0f,  -1.0f };
    Vec3 cam_right{ 1.0f,  0.0f,   0.0f };
    Vec3 cam_up_vec{ 0.0f,  1.0f,   0.0f };

    // --- Tube state ---
    bool tube_initialized = false;
    int  last_frame_index = -1;

    int  tube_count = 200;                 // number of points along path
    Vec3 tube_points[TUBE_MAX_POINTS];
    Vec3 tube_dirs[TUBE_MAX_POINTS];

    float tube_segment_length = 4.0f;              // distance between points
    float tube_radius = 22.0f;             // base radius of the tube
    float tube_bound_radius = 180.0f;            // soft boundary for centerline
    float tube_twist = 0.0f;              // used to twist the rings
    int   tube_ring_segments = 24;                // resolution around the tube
};

// -----------------------------------------------------------------------------
// Tube helpers – initialize and advance an endless wandering path
// -----------------------------------------------------------------------------
void init_tube_path(SceneParams& scene)
{
    if (scene.tube_initialized)
        return;

    if (scene.tube_count > TUBE_MAX_POINTS)
        scene.tube_count = TUBE_MAX_POINTS;
    if (scene.tube_count < 4)
        scene.tube_count = 4;

    // Start as a straight line along +Z, centered around the origin.
    Vec3 pos = make_vec3(0.0f, 0.0f,
        -scene.tube_segment_length * scene.tube_count * 0.5f);
    Vec3 dir = make_vec3(0.0f, 0.0f, 1.0f);

    for (int i = 0; i < scene.tube_count; ++i)
    {
        scene.tube_points[i] = pos;
        scene.tube_dirs[i] = dir;
        pos = pos + dir * scene.tube_segment_length;
    }

    scene.tube_initialized = true;
    scene.last_frame_index = -1;
}

void advance_tube_path(SceneParams& scene, int frame)
{
    if (!scene.tube_initialized)
        return;

    // Make sure we only advance once per frame index.
    if (scene.last_frame_index == frame)
        return;

    scene.last_frame_index = frame;

    // Shift all points down: [1] -> [0], [2] -> [1], ...
    for (int i = 0; i < scene.tube_count - 1; ++i)
    {
        scene.tube_points[i] = scene.tube_points[i + 1];
        scene.tube_dirs[i] = scene.tube_dirs[i + 1];
    }

    // Head point: we extend from here.
    Vec3 headPos = scene.tube_points[scene.tube_count - 1];
    Vec3 headDir = normalize3(scene.tube_dirs[scene.tube_count - 1]);
    if (length3(headDir) < 1e-4f)
        headDir = make_vec3(0.0f, 0.0f, 1.0f);

    // Random wandering direction (less vertical wobble)
    Vec3 randomSteer = make_vec3(
        Random::random_signed(),
        Random::random_signed() * 0.4f,
        Random::random_signed());
    randomSteer = normalize3(randomSteer);

    float wanderStrength = 0.35f;
    Vec3  steer = randomSteer * wanderStrength;

    // Soft boundary: if we get close to tube_bound_radius, steer inward.
    float dist = length3(headPos);
    float bound = scene.tube_bound_radius;

    if (dist > bound * 0.6f)
    {
        Vec3 inward = (dist > 1e-3f)
            ? (-headPos / dist)
            : make_vec3(0.0f, 0.0f, -1.0f);

        float t = (dist - bound * 0.6f) / (bound * 0.4f); // 0..1 from 60%..100%
        if (t > 1.0f) t = 1.0f;

        // Blend random wander with a strong inward push as we approach the edge.
        steer = steer * (1.0f - t) + inward * (0.8f + 0.7f * t);
    }

    Vec3 newDir = normalize3(headDir + steer);
    if (length3(newDir) < 1e-4f)
        newDir = headDir;

    float step = scene.tube_segment_length;
    Vec3  newPos = headPos + newDir * step;

    scene.tube_points[scene.tube_count - 1] = newPos;
    scene.tube_dirs[scene.tube_count - 1] = newDir;

    // Slowly twist the tube rings
    scene.tube_twist += 0.04f;
}

// -----------------------------------------------------------------------------
// Camera callback  – still an orbit, good for debugging the tube
// -----------------------------------------------------------------------------
void camera_callback(int frame, float t, CameraParams& cam)
{
    (void)frame;

    auto* scene = static_cast<SceneParams*>(cam.user_ptr);
    if (!scene) return;

    // Smooth low-frequency breathing / sway patterns
    float radiusPhase = std::sin(t * 0.4f);
    float heightPhase = std::sin(t * 0.7f + 1.3f);
    float fovPhase = std::sin(t * 0.3f + 2.1f);

    scene->camera_radius_offset = scene->camera_radius_breath * radiusPhase;
    scene->camera_height_offset = scene->camera_height_breath * heightPhase;
    scene->camera_fov_offset = scene->camera_fov_breath * fovPhase;

    // Shared phase – lines can reuse this so shapes + camera feel coherent
    scene->shared_phase = t * 0.6f;

    float radius = scene->camera_base_radius + scene->camera_radius_offset;
    float height = scene->camera_base_height + scene->camera_height_offset;

    const float orbitSpeed = 0.18f;
    const float angle = t * orbitSpeed * 2.0f * 3.14159265f;

    cam.eye_x = std::cos(angle) * radius;
    cam.eye_y = height;
    cam.eye_z = std::sin(angle) * radius;

    cam.target_x = 0.0f;
    cam.target_y = 0.0f;
    cam.target_z = 0.0f;

    cam.up_x = 0.0f;
    cam.up_y = 1.0f;
    cam.up_z = 0.0f;

    cam.has_custom_fov = true;
    cam.fov_y_deg = scene->camera_base_fov + scene->camera_fov_offset;

    // --- write camera basis into SceneParams for possible future use ---
    scene->cam_eye = make_vec3(cam.eye_x, cam.eye_y, cam.eye_z);
    scene->cam_target = make_vec3(cam.target_x, cam.target_y, cam.target_z);

    Vec3 worldUp = make_vec3(cam.up_x, cam.up_y, cam.up_z);

    Vec3 forward = normalize3(scene->cam_target - scene->cam_eye); // camera look direction
    if (length3(forward) < 1e-5f) forward = make_vec3(0.0f, 0.0f, -1.0f);

    Vec3 right = normalize3(cross3(forward, worldUp));
    if (length3(right) < 1e-5f) right = make_vec3(1.0f, 0.0f, 0.0f);

    Vec3 up = normalize3(cross3(right, forward));

    scene->cam_forward = forward;
    scene->cam_right = right;
    scene->cam_up_vec = up;
}

// -----------------------------------------------------------------------------
// Push-style line callback – floor grid + wandering tube
// -----------------------------------------------------------------------------
void line_push_callback(int frame, float t, LineEmitContext& ctx)
{
    auto* scene = static_cast<SceneParams*>(ctx.user_ptr);
    if (!scene) return;

    const float twoPi = 6.2831853f;

    // Camera <-> lines coupling: use camera breathing as a normalized 0..1 signal.
    float breathNorm = 0.0f;
    if (scene->camera_radius_breath != 0.0f)
    {
        breathNorm = scene->camera_radius_offset / scene->camera_radius_breath; // ~[-1,1]
    }
    breathNorm = 0.5f + 0.5f * breathNorm; // -> [0,1]

    float phase = scene->shared_phase;

    auto hueToRGB = [&](float h) -> Vec3
        {
            float r = 0.5f + 0.5f * std::sin(twoPi * (h + 0.0f));
            float g = 0.5f + 0.5f * std::sin(twoPi * (h + 1.0f / 3.0f));
            float b = 0.5f + 0.5f * std::sin(twoPi * (h + 2.0f / 3.0f));
            return { r, g, b };
        };

    // =========================================================================
    // 0) World-space reference: floor grid + emphasized axes
    // =========================================================================
    {
        const float floorY = -80.0f;   // a bit below tube
        const float halfSize = 320.0f;
        const float step = 32.0f;
        const int   linesEach = static_cast<int>(halfSize / step);

        Vec3  baseGridCol = { 0.45f, 0.54f, 0.78f };
        float gridIntensity = 130.0f * 32.0f;
        float gridThickness = 1.0f;

        for (int i = -linesEach; i <= linesEach; ++i)
        {
            float x = i * step;

            // Lines parallel to X (varying Z)
            {
                LineParams lp{};
                lp.start_x = -halfSize; lp.start_y = floorY; lp.start_z = x;
                lp.end_x = halfSize; lp.end_y = floorY; lp.end_z = x;

                float distZ = std::abs(x) / halfSize;         // 0..1
                float fadeEdge = 0.35f + 0.65f * (1.0f - distZ); // center brighter
                float bright = 0.8f * fadeEdge;

                Vec3 col = baseGridCol;
                lp.start_r = bright * col.x;
                lp.start_g = bright * col.y;
                lp.start_b = bright * col.z;
                lp.end_r = lp.start_r;
                lp.end_g = lp.start_g;
                lp.end_b = lp.start_b;

                lp.thickness = gridThickness;
                lp.jitter = 0.0f;
                lp.intensity = gridIntensity;

                ctx.add(lp);
            }

            // Lines parallel to Z (varying X)
            {
                LineParams lp{};
                lp.start_x = x; lp.start_y = floorY; lp.start_z = -halfSize;
                lp.end_x = x; lp.end_y = floorY; lp.end_z = halfSize;

                float distX = std::abs(x) / halfSize;
                float fadeEdge = 0.35f + 0.65f * (1.0f - distX);
                float bright = 0.8f * fadeEdge;

                Vec3 col = baseGridCol;
                lp.start_r = bright * col.x;
                lp.start_g = bright * col.y;
                lp.start_b = bright * col.z;
                lp.end_r = lp.start_r;
                lp.end_g = lp.start_g;
                lp.end_b = lp.start_b;

                lp.thickness = gridThickness;
                lp.jitter = 0.0f;
                lp.intensity = gridIntensity;

                ctx.add(lp);
            }
        }

        // Highlight X and Z axes on the floor for extra orientation
        {
            // X axis (red-ish)
            LineParams lp{};
            lp.start_x = -halfSize; lp.start_y = floorY; lp.start_z = 0.0f;
            lp.end_x = halfSize; lp.end_y = floorY; lp.end_z = 0.0f;

            lp.start_r = 1.2f; lp.start_g = 0.3f; lp.start_b = 0.3f;
            lp.end_r = lp.start_r;
            lp.end_g = lp.start_g;
            lp.end_b = lp.start_b;

            lp.thickness = gridThickness * 1.4f;
            lp.jitter = 0.0f;
            lp.intensity = gridIntensity * 1.4f;

            ctx.add(lp);
        }
        {
            // Z axis (blue-ish)
            LineParams lp{};
            lp.start_x = 0.0f; lp.start_y = floorY; lp.start_z = -halfSize;
            lp.end_x = 0.0f; lp.end_y = floorY; lp.end_z = halfSize;

            lp.start_r = 0.35f; lp.start_g = 0.5f; lp.start_b = 1.3f;
            lp.end_r = lp.start_r;
            lp.end_g = lp.start_g;
            lp.end_b = lp.start_b;

            lp.thickness = gridThickness * 1.4f;
            lp.jitter = 0.0f;
            lp.intensity = gridIntensity * 1.4f;

            ctx.add(lp);
        }

        // Vertical axis through origin (world up).
        {
            LineParams lp{};
            lp.start_x = 0.0f;
            lp.start_y = floorY;
            lp.start_z = 0.0f;
            lp.end_x = 0.0f;
            lp.end_y = floorY + 260.0f;
            lp.end_z = 0.0f;

            Vec3  axisCol = hueToRGB(0.58f); // soft cyan
            float bright = 1.6f;

            lp.start_r = bright * axisCol.x;
            lp.start_g = bright * axisCol.y;
            lp.start_b = bright * axisCol.z;
            lp.end_r = lp.start_r;
            lp.end_g = lp.start_g;
            lp.end_b = lp.start_b;

            lp.thickness = 0.015f;
            lp.jitter = 0.0f;
            lp.intensity = 180.0f;

            ctx.add(lp);
        }
    }

    ctx.flush_now(); // "floor/axis layer done"

    // =========================================================================
    // 1) Wandering tube: rings + longitudinal bands
    // =========================================================================
    init_tube_path(*scene);
    advance_tube_path(*scene, frame);

    int pointCount = scene->tube_count;
    int ringSegments = scene->tube_ring_segments;
    if (ringSegments < 3) ringSegments = 3;
    if (pointCount < 2) return;

    float baseRadius = scene->tube_radius;
    float twistBase = scene->tube_twist;

    auto ringPos = [&](int i, int j) -> Vec3
        {
            float fracPath = (pointCount > 1)
                ? (float)i / (float)(pointCount - 1)
                : 0.0f;

            Vec3 center = scene->tube_points[i];
            Vec3 dir = normalize3(scene->tube_dirs[i]);
            if (length3(dir) < 1e-4f)
                dir = make_vec3(0.0f, 0.0f, 1.0f);

            // Build a local frame for the ring.
            Vec3 tmpUp = make_vec3(0.0f, 1.0f, 0.0f);
            if (std::abs(dot3(dir, tmpUp)) > 0.95f)
                tmpUp = make_vec3(1.0f, 0.0f, 0.0f);

            Vec3 right = normalize3(cross3(dir, tmpUp));
            Vec3 up = normalize3(cross3(right, dir));

            float radius = baseRadius *
                (0.85f + 0.25f * std::sin(2.0f * fracPath * twoPi + phase * 0.7f));

            float ringAngleOffset = twistBase + fracPath * 4.0f;

            float a = twoPi * (float)j / (float)ringSegments + ringAngleOffset;
            float ca = std::cos(a);
            float sa = std::sin(a);

            Vec3 offset = right * (radius * ca) + up * (radius * sa);
            return center + offset;
        };

    for (int i = 0; i < pointCount; ++i)
    {
        float pathFrac = (pointCount > 1)
            ? (float)i / (float)(pointCount - 1)
            : 0.0f;

        for (int j = 0; j < ringSegments; ++j)
        {
            int jNext = (j + 1) % ringSegments;

            Vec3 a = ringPos(i, j);
            Vec3 b = ringPos(i, jNext);

            float ringFrac = (float)j / (float)ringSegments;

            // Color: mix "distance along path" and around-ring pattern.
            float hue1 = 0.58f
                + 0.18f * std::sin(pathFrac * 6.0f + t * 0.35f)
                + 0.05f * std::sin(ringFrac * twoPi * 2.0f);
            Vec3 col1 = hueToRGB(hue1);

            float bright1 =
                1.3f *
                (0.3f + 0.7f * (1.0f - pathFrac)) *
                (0.45f + 0.55f * breathNorm);

            // Ring segment
            {
                LineParams lp{};
                lp.start_x = a.x; lp.start_y = a.y; lp.start_z = a.z;
                lp.end_x = b.x; lp.end_y = b.y; lp.end_z = b.z;

                lp.start_r = bright1 * col1.x;
                lp.start_g = bright1 * col1.y;
                lp.start_b = bright1 * col1.z;
                lp.end_r = lp.start_r * 0.9f;
                lp.end_g = lp.start_g * 0.9f;
                lp.end_b = lp.start_b * 0.9f;

                lp.thickness = 0.0065f;
                lp.jitter = 0.003f;
                lp.intensity = 120.0f;

                ctx.add(lp);
            }

            // Longitudinal segment (only if we have next point)
            if (i < pointCount - 1)
            {
                Vec3 c = ringPos(i + 1, j);

                float hue2 = hue1 + 0.03f;
                Vec3  col2 = hueToRGB(hue2);
                float bright2 =
                    1.6f *
                    (0.35f + 0.65f * (1.0f - pathFrac)) *
                    (0.55f + 0.45f * breathNorm);

                LineParams lp2{};
                lp2.start_x = a.x; lp2.start_y = a.y; lp2.start_z = a.z;
                lp2.end_x = c.x; lp2.end_y = c.y; lp2.end_z = c.z;

                lp2.start_r = bright2 * col2.x;
                lp2.start_g = bright2 * col2.y;
                lp2.start_b = bright2 * col2.z;
                lp2.end_r = lp2.start_r * 0.9f;
                lp2.end_g = lp2.start_g * 0.9f;
                lp2.end_b = lp2.start_b * 0.9f;

                lp2.thickness = 1.0f;
                lp2.jitter = 0.004f;
                lp2.intensity = 160.0f;

                ctx.add(lp2);
            }
        }
    }

    // Optional: a bright "core line" along the center of the tube.
    {
        for (int i = 0; i < pointCount - 1; ++i)
        {
            float pathFrac = (pointCount > 1)
                ? (float)i / (float)(pointCount - 1)
                : 0.0f;

            Vec3 a = scene->tube_points[i];
            Vec3 b = scene->tube_points[i + 1];

            float hueCore = 0.02f + 0.1f * std::sin(t * 0.4f + pathFrac * 8.0f);
            Vec3  colCore = hueToRGB(hueCore);
            float brightCore =
                2.4f *
                (0.6f + 0.4f * std::sin(t * 0.8f + pathFrac * 10.0f));

            LineParams lp{};
            lp.start_x = a.x; lp.start_y = a.y; lp.start_z = a.z;
            lp.end_x = b.x; lp.end_y = b.y; lp.end_z = b.z;

            lp.start_r = brightCore * colCore.x;
            lp.start_g = brightCore * colCore.y;
            lp.start_b = brightCore * colCore.z;
            lp.end_r = lp.start_r * 0.8f;
            lp.end_g = lp.start_g * 0.8f;
            lp.end_b = lp.start_b * 0.8f;

            lp.thickness = 1.0f;
            lp.jitter = 0.002f;
            lp.intensity = 260.0f;

            ctx.add(lp);
        }
    }

    ctx.flush_now(); // "tube layer done"
}

// -----------------------------------------------------------------------------
// Entry
// -----------------------------------------------------------------------------
int main()
{
    std::cout << "example_tube_push\n";
    std::cout << "This code is in file: " << __FILE__ << "\n";

    const std::string uniqueName = WIRE_UNIQUE_NAME(g_base_output_filepath);
    std::cout << "Video name: " << uniqueName << "\n";
    std::cout << "Output path: " << g_base_output_filepath
        << "/" << uniqueName << ".mp4\n";

    SceneParams   scene{};
    RenderSettings settings = init_render_settings(uniqueName, 1);

    renderSequencePush(
        settings,
        camera_callback,
        line_push_callback,
        &scene
    );

    VLC::play(g_base_output_filepath + "/" + uniqueName + ".mp4");

    return 0;
}
