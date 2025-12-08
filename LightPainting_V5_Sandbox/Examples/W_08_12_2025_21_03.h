#include "WireUtil.h"

using namespace WireEngine;

// -----------------------------------------------------------------------------
// Shared tunnel constants (camera + drawing both use these)
// -----------------------------------------------------------------------------
constexpr int   TUNNEL_SEGMENTS = 6;      // hexagon
constexpr int   TUNNEL_RINGS = 10;     // how many frames deep
constexpr float TUNNEL_RADIUS = 40.0f;  // ring radius
constexpr float TUNNEL_SPACING = 25.0f;  // distance between rings

// Depth center of tunnel (mainly for orbit mode)
constexpr float TUNNEL_Z_CENTER =
(TUNNEL_RINGS - 1) * TUNNEL_SPACING * 0.5f;

constexpr bool CAMERA_INSIDE = true; // true = inside, false = orbit

// -----------------------------------------------------------------------------
// Minimal render settings for fast, crisp debug
// -----------------------------------------------------------------------------
RenderSettings init_render_settings(const std::string& baseName,
    int seconds = 4)
{
    RenderSettings s{};

    // Lower res for quick iteration
    s.width = 1280;
    s.height = 720;

    s.frames = 60 * seconds;
    s.fps = 60.0f;

    // Single pass, no temporal accumulation
    s.accum_passes = 1;

    // Additive neon
    s.line_blend_mode = LineBlendMode::AdditiveLightPainting;

    // Simple tone; bloom OFF for clean debug
    s.exposure = 1.5f;
    s.bloom_enabled = false;
    s.bloom_threshold = 10.0f;
    s.bloom_strength = 0.0f;

    // Line rendering
    s.soft_edge = 0.85f;
    s.energy_per_hit = 5.0e-4f;
    s.thickness_scale = 1.0f;

    // Plenty of room
    s.max_line_segments_hint = 1'000'000;

    // Readback & IO
    s.use_pbo = true;
    s.output_dir = "frames_tunnel_debug";

    // Output: unique video name
    s.output_mode = OutputMode::FFmpegVideo;
    s.ffmpeg_path = "ffmpeg";
    s.ffmpeg_output = g_base_output_filepath + "/" + baseName + ".mp4";
    s.ffmpeg_extra_args = "-c:v libx264 -preset veryfast -crf 18";

    return s;
}

// -----------------------------------------------------------------------------
// Shared curve: center of the tunnel as a function of z + time
// -----------------------------------------------------------------------------
glm::vec3 tunnel_center(float z, float t)
{
    // Same bending logic as in draw_debug_tunnel
    float bendPhase = z * 0.03f + t * 0.6f;

    float offsetX = std::sin(bendPhase) * 30.0f; // 30 units left/right
    float offsetY = std::cos(bendPhase * 0.8f) * 10.0f; // 10 units up/down

    return glm::vec3(offsetX, offsetY, z);
}

// -----------------------------------------------------------------------------
// Camera: inside mode follows the tunnel curve; orbit mode orbits around it
// -----------------------------------------------------------------------------
void camera_callback(int frame, float t, CameraParams& cam)
{
    (void)frame;
    const float twoPi = 6.2831853f;

    if (CAMERA_INSIDE)
    {
        // ---- Fly INSIDE the tunnel ALONG its bent centerline ----

        // Parameter along z (we still treat the curve as parameterized by z)
        float speed = 40.0f;         // world units per second in "z space"
        float zCam = -50.0f + t * speed;

        // Camera position on the curve
        glm::vec3 eyePos = tunnel_center(zCam, t);

        // Look ahead along the same curve to get a forward direction
        float lookAheadDist = 60.0f;
        glm::vec3 aheadPos = tunnel_center(zCam + lookAheadDist, t);

        glm::vec3 forward = glm::normalize(aheadPos - eyePos);

        // Build a stable camera frame
        glm::vec3 worldUp(0.0f, 1.0f, 0.0f);
        glm::vec3 right = glm::normalize(glm::cross(forward, worldUp));
        if (glm::length(right) < 1e-3f)
            right = glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec3 up = glm::normalize(glm::cross(right, forward));

        // Fill camera params
        cam.eye_x = eyePos.x;
        cam.eye_y = eyePos.y;
        cam.eye_z = eyePos.z;

        // You can either use aheadPos or eyePos + forward * someDist
        glm::vec3 target = eyePos + forward * 80.0f;

        cam.target_x = target.x;
        cam.target_y = target.y;
        cam.target_z = target.z;

        cam.up_x = up.x;
        cam.up_y = up.y;
        cam.up_z = up.z;

        cam.has_custom_fov = true;
        cam.fov_y_deg = 75.0f;  // a bit wider for “speed” feeling
    }
    else
    {
        // ---- Original ORBIT mode (for debugging the shape in 3D) ----
        float orbitRadius = 220.0f;
        float orbitHeight = 40.0f;
        float orbitSpeed = 0.12f;

        float angle = t * orbitSpeed * twoPi;

        cam.eye_x = std::cos(angle) * orbitRadius;
        cam.eye_y = orbitHeight;
        cam.eye_z = std::sin(angle) * orbitRadius + TUNNEL_Z_CENTER;

        cam.target_x = 0.0f;
        cam.target_y = 0.0f;
        cam.target_z = TUNNEL_Z_CENTER;

        cam.up_x = 0.0f;
        cam.up_y = 1.0f;
        cam.up_z = 0.0f;

        cam.has_custom_fov = true;
        cam.fov_y_deg = 60.0f;
    }
}

// -----------------------------------------------------------------------------
// Debug helper: one line with default look
// -----------------------------------------------------------------------------
void line(LineEmitContext& ctx,
    const glm::vec3& v0,
    const glm::vec3& v1,
    const glm::vec3& color = glm::vec3(1.3f, 0.4f, 1.3f),
    float            thickness = 0.35f,
    float            intensity = 120.0f)
{
    LineParams lp{};

    lp.start_x = v0.x; lp.start_y = v0.y; lp.start_z = v0.z;
    lp.end_x = v1.x; lp.end_y = v1.y; lp.end_z = v1.z;

    lp.start_r = color.x;
    lp.start_g = color.y;
    lp.start_b = color.z;
    lp.end_r = color.x;
    lp.end_g = color.y;
    lp.end_b = color.z;

    lp.thickness = thickness;
    lp.jitter = 0.0f;     // no animation / flicker in debug
    lp.intensity = intensity;

    ctx.add(lp);
}

// -----------------------------------------------------------------------------
// Build a simple hex-tunnel from rings + connectors, with a gentle bend
// -----------------------------------------------------------------------------
void draw_debug_tunnel(LineEmitContext& ctx, float t)
{
    const float twoPi = 6.2831853f;
    const float angleOffset = twoPi * 0.5f / TUNNEL_SEGMENTS; // flat top/bottom

    auto ring_vertex = [&](int ringIdx, int segIdx) -> glm::vec3
        {
            // Base "distance along the tunnel"
            float baseZ = ringIdx * TUNNEL_SPACING;

            // Center of this ring follows the same curve as the camera
            glm::vec3 center = tunnel_center(baseZ, t);

            // Slight radius breathing so the tunnel feels alive
            float radius = TUNNEL_RADIUS *
                (1.0f + 0.12f * std::sin(baseZ * 0.05f + t * 0.9f));

            // Local hex vertex around this bent center
            float a = twoPi * (float)segIdx / (float)TUNNEL_SEGMENTS + angleOffset;
            float x = std::cos(a) * radius;
            float y = std::sin(a) * radius;

            return center + glm::vec3(x, y, 0.0f);
        };

    // Colors similar to your reference: blue frames, magenta connectors
    glm::vec3 frameColor = glm::vec3(0.25f, 0.55f, 1.6f) * 2.0f;
    glm::vec3 barColor = glm::vec3(1.6f, 0.4f, 1.6f) * 2.0f;

    // 1) Draw all hex frames
    for (int r = 0; r < TUNNEL_RINGS; ++r)
    {
        for (int s = 0; s < TUNNEL_SEGMENTS; ++s)
        {
            int sNext = (s + 1) % TUNNEL_SEGMENTS;
            glm::vec3 a = ring_vertex(r, s);
            glm::vec3 b = ring_vertex(r, sNext);

            line(ctx, a, b, frameColor, 0.32f, 110.0f);
        }
    }

    // 2) Connect frames with longitudinal bars
    for (int r = 0; r < TUNNEL_RINGS - 1; ++r)
    {
        for (int s = 0; s < TUNNEL_SEGMENTS; ++s)
        {
            glm::vec3 a = ring_vertex(r, s);
            glm::vec3 b = ring_vertex(r + 1, s);

            line(ctx, a, b, barColor, 0.36f, 130.0f);
        }
    }
}

// -----------------------------------------------------------------------------
// Line callback – draw the (slightly animated, bent) tunnel
// -----------------------------------------------------------------------------
void line_push_callback(int frame, float t, LineEmitContext& ctx)
{
    (void)frame;
    draw_debug_tunnel(ctx, t);
    ctx.flush_now();
}

// -----------------------------------------------------------------------------
// Entry
// -----------------------------------------------------------------------------
int main()
{
    std::cout << "example_tunnel_debug_follow_curve\n";
    std::cout << "This code is in file: " << __FILE__ << "\n";

    const std::string uniqueName = WIRE_UNIQUE_NAME(g_base_output_filepath);
    std::cout << "Video name: " << uniqueName << "\n";
    std::cout << "Output path: " << g_base_output_filepath
        << "/" << uniqueName << ".mp4\n";

    RenderSettings settings = init_render_settings(uniqueName, 4);

    renderSequencePush(
        settings,
        camera_callback,
        line_push_callback,
        nullptr            // no user_ptr needed
    );

    VLC::play(g_base_output_filepath + "/" + uniqueName + ".mp4");
    return 0;
}
