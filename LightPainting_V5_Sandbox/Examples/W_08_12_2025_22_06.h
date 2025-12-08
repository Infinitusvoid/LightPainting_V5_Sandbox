#pragma once

#include "WireUtil.h"

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
    s.max_line_segments_hint = 2'000'000;

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

// Simple clamp
inline float clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

// -----------------------------------------------------------------------------
// FlightPath – precomputed random walk inside a big cube
// -----------------------------------------------------------------------------
struct FlightPath
{
    // List of nodes; each consecutive pair is approximately step_length apart
    std::vector<Vec3> nodes;

    float step_length = 40.0f;    // units between samples along the path
    float box_half = 2000.0f;  // cube is [-box_half, box_half]^3

    // Build a random wandering path that stays inside the cube.
    void build_random_walk(int nodeCount,
        float stepLen,
        float cubeHalf)
    {
        nodes.clear();
        if (nodeCount < 2) nodeCount = 2;

        step_length = stepLen;
        box_half = cubeHalf;

        // Start somewhere near the "front" of the box
        Vec3 pos = make_vec3(0.0f, 0.0f, -cubeHalf * 0.25f);
        Vec3 dir = make_vec3(0.0f, 0.0f, 1.0f); // initial forward

        nodes.push_back(pos);

        for (int i = 1; i < nodeCount; ++i)
        {
            // Random steering, slightly damped in Y
            Vec3 randomSteer = make_vec3(
                Random::random_signed(),
                Random::random_signed() * 0.4f,
                Random::random_signed()
            );
            float rsLen = length3(randomSteer);
            if (rsLen < 1.0e-4f)
                randomSteer = make_vec3(0.0f, 0.0f, 1.0f);
            else
                randomSteer = randomSteer * (1.0f / rsLen);

            float wanderStrength = 0.6f;
            Vec3  forwardBias = make_vec3(0.0f, 0.0f, 1.0f);

            // Boundary push to keep us inside [-box_half, box_half]^3
            Vec3 boundaryPush = make_vec3(0.0f, 0.0f, 0.0f);
            float inner = box_half * 0.6f;
            float outer = box_half * 0.9f;

            auto addAxisPush = [&](float coord, int axis)
                {
                    float av = std::abs(coord);
                    if (av <= inner) return;

                    float t = (av - inner) / (outer - inner);
                    if (t > 1.0f) t = 1.0f;

                    float sign = (coord >= 0.0f) ? 1.0f : -1.0f;
                    float push = (0.3f + 0.9f * t) * (-sign); // toward center

                    if (axis == 0)      boundaryPush.x += push;
                    else if (axis == 1) boundaryPush.y += push;
                    else                boundaryPush.z += push;
                };

            addAxisPush(pos.x, 0);
            addAxisPush(pos.y, 1);
            addAxisPush(pos.z, 2);

            // Combine previous direction + forward bias + randomness + boundary
            Vec3 combined =
                dir * 1.4f +
                randomSteer * wanderStrength +
                forwardBias * 0.8f +
                boundaryPush * 0.7f;

            float cLen = length3(combined);
            if (cLen < 1.0e-4f)
                combined = forwardBias;
            else
                combined = combined * (1.0f / cLen);

            Vec3 newPos = pos + combined * step_length;

            // Clamp to cube
            newPos.x = clampf(newPos.x, -box_half, box_half);
            newPos.y = clampf(newPos.y, -box_half, box_half);
            newPos.z = clampf(newPos.z, -box_half, box_half);

            nodes.push_back(newPos);
            pos = newPos;
            dir = combined;
        }
    }

    float total_length() const
    {
        if (nodes.size() < 2) return 0.0f;
        return step_length * (float)(nodes.size() - 1);
    }

    // Sample position along the path by distance "s" from start
    Vec3 sample_at(float s) const
    {
        if (nodes.empty())
            return make_vec3(0.0f, 0.0f, 0.0f);

        if (s <= 0.0f)
            return nodes.front();

        float maxS = total_length();
        if (maxS <= 0.0f)
            return nodes.front();

        if (s >= maxS)
            return nodes.back();

        float fIndex = s / step_length;
        int   i0 = (int)fIndex;
        if (i0 < 0) i0 = 0;
        int lastIndex = (int)nodes.size() - 2;
        if (i0 > lastIndex) i0 = lastIndex;
        int i1 = i0 + 1;

        float alpha = fIndex - (float)i0;
        const Vec3& p0 = nodes[(size_t)i0];
        const Vec3& p1 = nodes[(size_t)i1];

        return p0 * (1.0f - alpha) + p1 * alpha;
    }
};

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
// TunnelSection – uses a FlightPath as its centerline
// -----------------------------------------------------------------------------
struct TunnelSection
{
    const FlightPath* path = nullptr;

    int   segments = 6;    // hexagon
    int   rings = 40;   // how many cross-sections
    float radius = 40.0f;
    float spacing = 60.0f; // nominal spacing used for length mapping

    float length_used = 0.0f; // part of the path we actually occupy

    void bind_path(const FlightPath* p)
    {
        path = p;
        if (!path)
        {
            length_used = 0.0f;
            return;
        }

        float desired = (rings > 1) ? (float)(rings - 1) * spacing : 0.0f;
        float maxPath = path->total_length();

        if (desired <= 0.0f || maxPath <= 0.0f)
            length_used = 0.0f;
        else
            length_used = (desired < maxPath) ? desired : maxPath;
    }

    float total_length() const
    {
        return length_used;
    }

    float s_for_ring(int ringIdx) const
    {
        if (rings <= 1)
            return 0.0f;

        if (ringIdx < 0) ringIdx = 0;
        if (ringIdx > rings - 1) ringIdx = rings - 1;

        float t = (float)ringIdx / (float)(rings - 1);
        return t * length_used;
    }

    Vec3 center_for_ring(int ringIdx) const
    {
        if (!path)
            return make_vec3(0.0f, 0.0f, (float)ringIdx * spacing);

        float s = s_for_ring(ringIdx);
        return path->sample_at(s);
    }

    Vec3 center_along(float s) const
    {
        if (!path)
            return make_vec3(0.0f, 0.0f, s);

        if (s < 0.0f)      s = 0.0f;
        if (s > length_used) s = length_used;
        return path->sample_at(s);
    }

    // Approximate tangent along centerline (for attachments / camera)
    Vec3 tangent_along(float s) const
    {
        float L = total_length();
        if (L <= 0.0f) return make_vec3(0.0f, 0.0f, 1.0f);

        float eps = spacing * 0.5f;
        if (eps <= 0.0f) eps = L * 0.02f;

        float s0 = s - eps;
        float s1 = s + eps;
        if (s0 < 0.0f) s0 = 0.0f;
        if (s1 > L)    s1 = L;

        Vec3 p0 = center_along(s0);
        Vec3 p1 = center_along(s1);
        Vec3 v = p1 - p0;

        float len = length3(v);
        if (len < 1.0e-4f) return make_vec3(0.0f, 0.0f, 1.0f);
        return v * (1.0f / len);
    }

    float radius_for_ring(int /*ringIdx*/) const
    {
        return radius;
    }

    // Vertex of a ring (hex) at ringIdx/segIdx
    Vec3 ring_vertex(int ringIdx, int segIdx) const
    {
        const float twoPi = 6.2831853f;

        Vec3 center = center_for_ring(ringIdx);
        float R = radius_for_ring(ringIdx);

        float angleOffset = twoPi * 0.5f / (float)segments; // flat top/bottom
        float a = twoPi * (float)segIdx / (float)segments + angleOffset;

        float x = std::cos(a) * R;
        float y = std::sin(a) * R;

        return make_vec3(center.x + x, center.y + y, center.z);
    }
};

// -----------------------------------------------------------------------------
// Tunnel – draws the geometry following TunnelSection
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
        (void)t;

        const int rings = section.rings;
        const int segments = section.segments;

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

                Vec3 a = section.ring_vertex(r, s);
                Vec3 b = section.ring_vertex(r, sn);

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
                Vec3 a = section.ring_vertex(r, s);
                Vec3 b = section.ring_vertex(r + 1, s);

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
            float L = section.total_length();
            if (L <= 0.0f)
                return;

            int coreSegs = rings * 3; // more refined centerline

            for (int i = 0; i < coreSegs - 1; ++i)
            {
                float u0 = (float)i / (float)(coreSegs - 1);
                float u1 = (float)(i + 1) / (float)(coreSegs - 1);

                float s0 = u0 * L;
                float s1 = u1 * L;

                Vec3 c0 = section.center_along(s0);
                Vec3 c1 = section.center_along(s1);

                float pulse = 0.7f + 0.3f * std::sin(6.2831853f * u0 + t * 1.3f);

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
    int   pulse_count = 7;     // how many pulses
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

            Vec3 p0 = sec.center_along(s0);
            Vec3 p1 = sec.center_along(s1);

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
// Attachments – simple billboards “glued” outside the tunnel wall
// -----------------------------------------------------------------------------
struct Attachments
{
    std::vector<float> anchor_s; // positions along tunnel centerline

    void build(const TunnelSection& sec)
    {
        anchor_s.clear();

        float L = sec.total_length();
        if (L <= 0.0f) return;

        int count = 8; // a few anchors along the visible segment
        for (int i = 0; i < count; ++i)
        {
            float u = (float)i / (float)count; // [0,1)
            float s = u * L;
            anchor_s.push_back(s);
        }
    }

    void draw(LineEmitContext& ctx,
        const TunnelSection& sec,
        float /*t*/) const
    {
        if (anchor_s.empty()) return;

        Vec3 worldUp = make_vec3(0.0f, 1.0f, 0.0f);

        for (float s : anchor_s)
        {
            Vec3 c = sec.center_along(s);
            Vec3 tg = sec.tangent_along(s);

            // Build local frame: tg (forward), right, outward
            Vec3 right = cross3(tg, worldUp);
            float rLen = length3(right);
            if (rLen < 1.0e-4f)
                right = make_vec3(1.0f, 0.0f, 0.0f);
            else
                right = right * (1.0f / rLen);

            Vec3 outward = cross3(right, tg);
            float oLen = length3(outward);
            if (oLen < 1.0e-4f)
                outward = worldUp;
            else
                outward = outward * (1.0f / oLen);

            // Move slightly outside the tunnel wall
            float wallOffset = sec.radius + 10.0f;
            Vec3  base = c + outward * wallOffset;

            // Simple rectangular billboard
            float halfW = 35.0f;
            float halfH = 20.0f;

            Vec3 rightScaled = right * halfW;
            Vec3 upScaled = outward * halfH; // outward as "up" in this plane

            Vec3 pTL = base - rightScaled + upScaled;
            Vec3 pTR = base + rightScaled + upScaled;
            Vec3 pBR = base + rightScaled - upScaled;
            Vec3 pBL = base - rightScaled - upScaled;

            Vec3 frameCol = make_vec3(1.8f, 0.8f, 0.4f);
            float thick = 0.25f;
            float inten = 150.0f;

            // Frame
            emit_line(ctx, pTL, pTR, frameCol, thick, inten);
            emit_line(ctx, pTR, pBR, frameCol, thick, inten);
            emit_line(ctx, pBR, pBL, frameCol, thick, inten);
            emit_line(ctx, pBL, pTL, frameCol, thick, inten);

            // Simple "X" inside – placeholder for graffiti / text later
            emit_line(ctx, pTL, pBR, frameCol * 0.8f, thick * 0.7f, inten * 0.8f);
            emit_line(ctx, pTR, pBL, frameCol * 0.8f, thick * 0.7f, inten * 0.8f);
        }
    }
};

// -----------------------------------------------------------------------------
// Universe – your scene container
// -----------------------------------------------------------------------------
struct Universe
{
    FlightPath  path{};
    CameraRig   camera{};
    Tunnel      tunnel{};
    EnergyFlow  energy{};
    Attachments attachments{};

    Universe()
    {
        // 1) Build a global flight path inside a 4km cube
        const int   nodeCount = 600;
        const float step = 40.0f;
        const float cubeHalf = 2000.0f;
        path.build_random_walk(nodeCount, step, cubeHalf);

        // 2) Bind path to tunnel
        tunnel.section.path = &path;
        tunnel.section.rings = 40;
        tunnel.section.spacing = 60.0f;
        tunnel.section.radius = 40.0f;
        tunnel.section.bind_path(&path);

        // 3) Camera defaults
        camera.inside_mode = true;
        camera.fly_speed = 40.0f;

        // 4) Energy tweaks (feel free to play)
        energy.pulse_count = 9;

        // 5) Build initial attachments along the tunnel
        attachments.build(tunnel.section);
    }
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

        Vec3 eye = sec.center_along(sCam);
        Vec3 target = sec.center_along(sAhead);

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
        Vec3  center = sec.center_along(centerS);

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
// Line callback – draws tunnel + energy pulses + attachments
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

    // 3) First-pass attachments (billboards outside the wall)
    uni->attachments.draw(ctx, uni->tunnel.section, t);

    ctx.flush_now();
}

// -----------------------------------------------------------------------------
// Entry
// -----------------------------------------------------------------------------
int main()
{
    std::cout << "example_tunnel_energy_universe_cube\n";
    std::cout << "This code is in file: " << __FILE__ << "\n";

    const std::string uniqueName = WIRE_UNIQUE_NAME(g_base_output_filepath);
    std::cout << "Video name: " << uniqueName << "\n";
    std::cout << "Output path: " << g_base_output_filepath
        << "/" << uniqueName << ".mp4\n";

    RenderSettings settings = init_render_settings(uniqueName, 4);

    Universe universe{};
    // You can play with, for example:
    // universe.camera.inside_mode = false;
    // universe.tunnel.section.rings = 60; universe.tunnel.section.bind_path(&universe.path);

    renderSequencePush(
        settings,
        camera_callback,
        line_push_callback,
        &universe // passed as user_ptr to both callbacks
    );

    VLC::play(g_base_output_filepath + "/" + uniqueName + ".mp4");
    return 0;
}
