#pragma once

#include "WireUtil.h"
#include <cmath>

using namespace WireEngine;

// -----------------------------------------------------------------------------
// Render settings – fast but nice enough
// -----------------------------------------------------------------------------
RenderSettings init_render_settings(const std::string& baseName,
    int seconds = 4)
{
    RenderSettings s{};

    // Resolution
    s.width = 1280;
    s.height = 720;

    // Time
    s.frames = 60 * seconds;
    s.fps = 60.0f;

    // Single pass for quick iteration
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
    s.output_dir = "frames_tunnel_energy";

    // Output: unique video name
    s.output_mode = OutputMode::FFmpegVideo;
    s.ffmpeg_path = "ffmpeg";
    s.ffmpeg_output = g_base_output_filepath + "/" + baseName + ".mp4";
    s.ffmpeg_extra_args = "-c:v libx264 -preset veryfast -crf 18";

    return s;
}

// -----------------------------------------------------------------------------
// Tiny helper – one line with defaults
// -----------------------------------------------------------------------------
inline void emit_line(LineEmitContext& ctx,
    const Vec3& a,
    const Vec3& b,
    const Vec3& color,
    float thickness,
    float intensity,
    float jitter = 0.0f)
{
    LineParams lp{};

    lp.start_x = a.x; lp.start_y = a.y; lp.start_z = a.z;
    lp.end_x = b.x; lp.end_y = b.y; lp.end_z = b.z;

    lp.start_r = color.x;
    lp.start_g = color.y;
    lp.start_b = color.z;
    lp.end_r = color.x;
    lp.end_g = color.y;
    lp.end_b = color.z;

    lp.thickness = thickness;
    lp.jitter = jitter;
    lp.intensity = intensity;

    ctx.add(lp);
}

// -----------------------------------------------------------------------------
// Camera rig – parameters for inside/orbit camera
// -----------------------------------------------------------------------------
struct CameraRig
{
    bool  inside_mode = true;  // true = fly inside, false = orbit outside

    // Inside mode
    float fly_speed = 40.0f; // units per second along the tunnel
    float fov_inside = 75.0f;

    // Orbit mode
    float orbit_radius = 260.0f;
    float orbit_height = 60.0f;
    float orbit_speed = 0.10f;  // revs per second-ish
    float fov_orbit = 60.0f;
};

// -----------------------------------------------------------------------------
// TunnelSection – all the math describing the tunnel shape
// -----------------------------------------------------------------------------
struct TunnelSection
{
    int   segments = 6;   // hexagon
    int   rings = 24;  // how many frames deep

    float radius = 40.0f; // base ring radius
    float spacing = 25.0f; // distance between ring centers along "s"

    // Bending parameters
    float bend_freq_z = 0.03f;
    float bend_freq_time = 0.6f;
    float bend_amp_x = 30.0f;
    float bend_amp_y = 10.0f;

    // Radius breathing
    float radius_breath_amp = 0.12f;
    float radius_breath_freq_z = 0.05f;
    float radius_breath_freq_time = 0.9f;

    // For convenience
    float total_length() const
    {
        return (rings > 1) ? (float)(rings - 1) * spacing : 0.0f;
    }

    // Center of the tunnel at a given "baseZ" (distance along the tunnel axis)
    Vec3 center_at_baseZ(float baseZ, float t) const
    {
        float bendPhase = baseZ * bend_freq_z + t * bend_freq_time;

        float offsetX = std::sin(bendPhase) * bend_amp_x;
        float offsetY = std::cos(bendPhase * 0.8f) * bend_amp_y;

        return make_vec3(offsetX, offsetY, baseZ);
    }

    // Center of a specific ring index
    Vec3 center_for_ring(int ringIdx, float t) const
    {
        if (ringIdx < 0) ringIdx = 0;
        if (ringIdx > rings - 1) ringIdx = rings - 1;

        float baseZ = (float)ringIdx * spacing;
        return center_at_baseZ(baseZ, t);
    }

    float radius_at_baseZ(float baseZ, float t) const
    {
        float arg = baseZ * radius_breath_freq_z + t * radius_breath_freq_time;
        return radius * (1.0f + radius_breath_amp * std::sin(arg));
    }

    float radius_for_ring(int ringIdx, float t) const
    {
        if (ringIdx < 0) ringIdx = 0;
        if (ringIdx > rings - 1) ringIdx = rings - 1;

        float baseZ = (float)ringIdx * spacing;
        return radius_at_baseZ(baseZ, t);
    }

    // Vertex of a ring (hex) at ringIdx/segIdx
    Vec3 ring_vertex(int ringIdx, int segIdx, float t) const
    {
        const float twoPi = 6.2831853f;

        Vec3 center = center_for_ring(ringIdx, t);
        float R = radius_for_ring(ringIdx, t);

        float angleOffset = twoPi * 0.5f / (float)segments; // flat top/bottom
        float a = twoPi * (float)segIdx / (float)segments + angleOffset;

        float x = std::cos(a) * R;
        float y = std::sin(a) * R;

        return make_vec3(center.x + x, center.y + y, center.z);
    }

    // Continuous center along the tunnel, for camera and energy pulses
    Vec3 center_along(float s, float t) const
    {
        if (rings <= 1 || spacing <= 0.0f)
            return center_for_ring(0, t);

        if (s <= 0.0f)
            return center_for_ring(0, t);

        float maxS = total_length();
        if (s >= maxS)
            return center_for_ring(rings - 1, t);

        float ringf = s / spacing; // e.g. 3.2 means between ring 3 and 4
        int i0 = (int)ringf;
        if (i0 < 0) i0 = 0;
        if (i0 > rings - 2) i0 = rings - 2;
        int i1 = i0 + 1;

        float alpha = ringf - (float)i0; // [0,1] between i0 and i1

        Vec3 c0 = center_for_ring(i0, t);
        Vec3 c1 = center_for_ring(i1, t);

        return c0 * (1.0f - alpha) + c1 * alpha;
    }
};

// -----------------------------------------------------------------------------
// Tunnel – draws the geometry
// -----------------------------------------------------------------------------
struct Tunnel
{
    TunnelSection section{};

    // Colors inspired by your reference
    Vec3 frameColor = make_vec3(0.25f, 0.55f, 1.6f) * 2.0f;   // ring frames
    Vec3 barColor = make_vec3(1.6f, 0.4f, 1.6f) * 2.0f;     // longitudinal bars
    Vec3 coreColor = make_vec3(1.4f, 1.2f, 1.8f) * 2.0f;     // central core

    bool draw_core = true;

    void draw(LineEmitContext& ctx, float t) const
    {
        const int   rings = section.rings;
        const int   segments = section.segments;

        if (rings < 2 || segments < 3)
            return;

        // 1) Draw all ring frames
        for (int r = 0; r < rings; ++r)
        {
            float pathFrac = (rings > 1) ? (float)r / (float)(rings - 1) : 0.0f;
            float fade = 0.4f + 0.6f * (1.0f - pathFrac);

            for (int s = 0; s < segments; ++s)
            {
                int sn = (s + 1) % segments;

                Vec3 a = section.ring_vertex(r, s, t);
                Vec3 b = section.ring_vertex(r, sn, t);

                emit_line(ctx,
                    a, b,
                    frameColor * fade,
                    0.32f,
                    110.0f);
            }
        }

        // 2) Longitudinal bars between rings
        for (int r = 0; r < rings - 1; ++r)
        {
            float pathFrac = (rings > 1) ? (float)r / (float)(rings - 1) : 0.0f;
            float fade = 0.5f + 0.5f * (1.0f - pathFrac);

            for (int s = 0; s < segments; ++s)
            {
                Vec3 a = section.ring_vertex(r, s, t);
                Vec3 b = section.ring_vertex(r + 1, s, t);

                emit_line(ctx,
                    a, b,
                    barColor * fade,
                    0.36f,
                    130.0f);
            }
        }

        // 3) Optional bright core line along tunnel center
        if (draw_core)
        {
            for (int r = 0; r < rings - 1; ++r)
            {
                float baseZ0 = (float)r * section.spacing;
                float baseZ1 = (float)(r + 1) * section.spacing;

                Vec3 c0 = section.center_at_baseZ(baseZ0, t);
                Vec3 c1 = section.center_at_baseZ(baseZ1, t);

                float pathFrac = (rings > 1) ? (float)r / (float)(rings - 1) : 0.0f;
                float pulse = 0.7f + 0.3f * std::sin(6.2831853f * pathFrac + t * 1.3f);

                emit_line(ctx,
                    c0, c1,
                    coreColor * pulse,
                    0.45f,
                    180.0f);
            }
        }
    }
};

// -----------------------------------------------------------------------------
// EnergyFlow – fast glowing pulses traveling through the tunnel center
// -----------------------------------------------------------------------------
struct EnergyFlow
{
    int   pulse_count = 7;    // how many pulses
    float pulse_speed = 25.0f; // units per second along the tunnel
    float pulse_length = 18.0f; // length of each pulse segment
    float thickness = 0.75f;
    float base_intensity = 260.0f;

    Vec3 baseColor = make_vec3(2.0f, 1.8f, 0.6f); // warm golden-white

    // Draw pulses using TunnelSection as the path
    void draw(LineEmitContext& ctx,
        const TunnelSection& sec,
        float t) const
    {
        float L = sec.total_length();
        if (L <= 0.0f)
            return;

        for (int i = 0; i < pulse_count; ++i)
        {
            // Each pulse has its own phase offset
            float phase = (float)i / (float)pulse_count;

            // u in [0,1)
            float u = std::fmod(t * (pulse_speed / L) + phase, 1.0f);
            if (u < 0.0f) u += 1.0f;

            float s_center = u * L;

            float halfLen = 0.5f * pulse_length;
            float s0 = s_center - halfLen;
            float s1 = s_center + halfLen;

            // Clamp s0/s1 to [0,L]
            if (s1 < 0.0f || s0 > L)
                continue;
            if (s0 < 0.0f) s0 = 0.0f;
            if (s1 > L)   s1 = L;

            Vec3 p0 = sec.center_along(s0, t);
            Vec3 p1 = sec.center_along(s1, t);

            // Additional flicker per pulse
            float flicker = 0.75f + 0.25f *
                std::sin(6.2831853f * (u + t * 0.5f));

            Vec3 color = baseColor * flicker;

            emit_line(ctx,
                p0, p1,
                color,
                thickness,
                base_intensity * (0.7f + 0.3f * flicker));
        }
    }
};

// -----------------------------------------------------------------------------
// Universe – your scene container
// -----------------------------------------------------------------------------
struct Universe
{
    CameraRig  camera{};
    Tunnel     tunnel{};
    EnergyFlow energy{};
};

// -----------------------------------------------------------------------------
// Camera callback – reads Universe and positions camera
// -----------------------------------------------------------------------------
void camera_callback(int frame, float t, CameraParams& cam)
{
    (void)frame;
    const float twoPi = 6.2831853f;

    auto* uni = static_cast<Universe*>(cam.user_ptr);

    // Fallback: no universe -> simple static camera
    if (!uni)
    {
        cam.eye_x = 0.0f; cam.eye_y = 0.0f; cam.eye_z = -200.0f;
        cam.target_x = 0.0f; cam.target_y = 0.0f; cam.target_z = 0.0f;
        cam.up_x = 0.0f; cam.up_y = 1.0f; cam.up_z = 0.0f;
        cam.has_custom_fov = true;
        cam.fov_y_deg = 60.0f;
        return;
    }

    CameraRig& cr = uni->camera;
    TunnelSection& sec = uni->tunnel.section;

    if (cr.inside_mode)
    {
        // --- Camera flies INSIDE the tunnel along its centerline ---
        float totalLen = sec.total_length();
        if (totalLen <= 0.0f) totalLen = 1.0f;

        // Distance along the tunnel
        float sCam = std::fmod(t * cr.fly_speed, totalLen);
        if (sCam < 0.0f) sCam += totalLen;

        float lookAheadDist = 40.0f;
        float sAhead = sCam + lookAheadDist;
        if (sAhead > totalLen) sAhead = totalLen;

        Vec3 eye = sec.center_along(sCam, t);
        Vec3 target = sec.center_along(sAhead, t);

        Vec3 forward = target - eye;
        float fLen = length3(forward);
        if (fLen < 1.0e-4f) forward = make_vec3(0.0f, 0.0f, 1.0f);
        else                forward = forward * (1.0f / fLen);

        Vec3 worldUp = make_vec3(0.0f, 1.0f, 0.0f);
        Vec3 right = cross3(forward, worldUp);
        float rLen = length3(right);
        if (rLen < 1.0e-4f) right = make_vec3(1.0f, 0.0f, 0.0f);
        else                right = right * (1.0f / rLen);
        Vec3 up = normalize3(cross3(right, forward));

        cam.eye_x = eye.x;    cam.eye_y = eye.y;    cam.eye_z = eye.z;
        cam.target_x = target.x; cam.target_y = target.y; cam.target_z = target.z;
        cam.up_x = up.x;     cam.up_y = up.y;     cam.up_z = up.z;

        cam.has_custom_fov = true;
        cam.fov_y_deg = cr.fov_inside;
    }
    else
    {
        // --- External orbit around approximate tunnel center ---
        float centerS = sec.total_length() * 0.5f;
        Vec3  center = sec.center_along(centerS, t);

        float angle = t * cr.orbit_speed * twoPi;

        float ox = std::cos(angle) * cr.orbit_radius;
        float oz = std::sin(angle) * cr.orbit_radius;

        Vec3 eye = make_vec3(center.x + ox,
            center.y + cr.orbit_height,
            center.z + oz);

        Vec3 target = center;
        Vec3 up = make_vec3(0.0f, 1.0f, 0.0f);

        cam.eye_x = eye.x;    cam.eye_y = eye.y;    cam.eye_z = eye.z;
        cam.target_x = target.x; cam.target_y = target.y; cam.target_z = target.z;
        cam.up_x = up.x;     cam.up_y = up.y;     cam.up_z = up.z;

        cam.has_custom_fov = true;
        cam.fov_y_deg = cr.fov_orbit;
    }
}

// -----------------------------------------------------------------------------
// Line callback – draws the tunnel + energy pulses from Universe
// -----------------------------------------------------------------------------
void line_push_callback(int frame, float t, LineEmitContext& ctx)
{
    (void)frame;

    auto* uni = static_cast<Universe*>(ctx.user_ptr);
    if (!uni) return;

    // 1) Solid tunnel geometry
    uni->tunnel.draw(ctx, t);

    // 2) Energy pulses flying through the center
    uni->energy.draw(ctx, uni->tunnel.section, t);

    ctx.flush_now();
}

// -----------------------------------------------------------------------------
// Entry
// -----------------------------------------------------------------------------
int main()
{
    std::cout << "example_tunnel_energy_universe\n";
    std::cout << "This code is in file: " << __FILE__ << "\n";

    const std::string uniqueName = WIRE_UNIQUE_NAME(g_base_output_filepath);
    std::cout << "Video name: " << uniqueName << "\n";
    std::cout << "Output path: " << g_base_output_filepath
        << "/" << uniqueName << ".mp4\n";

    RenderSettings settings = init_render_settings(uniqueName, 4);

    Universe universe{};
    // Play with these:
    // universe.camera.inside_mode = false;          // external orbit
    // universe.energy.pulse_count = 10;
    // universe.tunnel.section.rings = 32;

    renderSequencePush(
        settings,
        camera_callback,
        line_push_callback,
        &universe // passed as user_ptr to both callbacks
    );

    VLC::play(g_base_output_filepath + "/" + uniqueName + ".mp4");
    return 0;
}
