#include "WireUtil.h"

using namespace WireEngine;

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

    // Keep it fairly neutral; bloom OFF for clean debug
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
// Simple camera: fixed position looking down the tunnel
// -----------------------------------------------------------------------------
void camera_callback(int frame, float t, CameraParams& cam)
{
    (void)frame; (void)t;

    // Camera behind the first ring, looking forward along +Z
    cam.eye_x = 0.0f;
    cam.eye_y = 0.0f;
    cam.eye_z = -150.0f;

    cam.target_x = 0.0f;
    cam.target_y = 0.0f;
    cam.target_z = 150.0f;

    cam.up_x = 0.0f;
    cam.up_y = 1.0f;
    cam.up_z = 0.0f;

    cam.has_custom_fov = true;
    cam.fov_y_deg = 60.0f;
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
// Build a simple hex-tunnel from rings + connectors
// -----------------------------------------------------------------------------
void draw_debug_tunnel(LineEmitContext& ctx)
{
    const float twoPi = 6.2831853f;

    const int   segments = 6;      // hexagon
    const int   rings = 10;     // how many frames deep
    const float radius = 40.0f;  // ring radius
    const float spacing = 25.0f;  // distance between rings
    const float angleOffset = twoPi * 0.5f / segments; // so top/bottom are flat

    auto ring_vertex = [&](int ringIdx, int segIdx) -> glm::vec3
        {
            float z = ringIdx * spacing; // depth
            float a = twoPi * (float)segIdx / (float)segments + angleOffset;

            float x = std::cos(a) * radius;
            float y = std::sin(a) * radius;
            return glm::vec3(x, y, z);
        };

    // Colors similar to reference: blue frames, magenta connectors
    glm::vec3 frameColor = glm::vec3(0.25f, 0.55f, 1.6f);
    glm::vec3 barColor = glm::vec3(1.6f, 0.4f, 1.6f);

    // 1) Draw all hex frames
    for (int r = 0; r < rings; ++r)
    {
        for (int s = 0; s < segments; ++s)
        {
            int sNext = (s + 1) % segments;
            glm::vec3 a = ring_vertex(r, s);
            glm::vec3 b = ring_vertex(r, sNext);

            line(ctx, a, b, frameColor, 0.32f, 110.0f);
        }
    }

    // 2) Connect frames with longitudinal bars
    for (int r = 0; r < rings - 1; ++r)
    {
        for (int s = 0; s < segments; ++s)
        {
            glm::vec3 a = ring_vertex(r, s);
            glm::vec3 b = ring_vertex(r + 1, s);

            line(ctx, a, b, barColor, 0.36f, 130.0f);
        }
    }
}

// -----------------------------------------------------------------------------
// Line callback – just draw the static tunnel
// -----------------------------------------------------------------------------
void line_push_callback(int frame, float t, LineEmitContext& ctx)
{
    (void)frame; (void)t;
    draw_debug_tunnel(ctx);
    ctx.flush_now();
}

// -----------------------------------------------------------------------------
// Entry
// -----------------------------------------------------------------------------
int main()
{
    std::cout << "example_tunnel_debug\n";
    std::cout << "This code is in file: " << __FILE__ << "\n";

    const std::string uniqueName = WIRE_UNIQUE_NAME(g_base_output_filepath);
    std::cout << "Video name: " << uniqueName << "\n";
    std::cout << "Output path: " << g_base_output_filepath
        << "/" << uniqueName << ".mp4\n";

    RenderSettings settings = init_render_settings(uniqueName, 1);

    renderSequencePush(
        settings,
        camera_callback,
        line_push_callback,
        nullptr            // we don't use user_ptr in this debug version
    );

    VLC::play(g_base_output_filepath + "/" + uniqueName + ".mp4");
    return 0;
}
