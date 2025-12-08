#pragma once

#include "WireUtil.h"

using namespace WireEngine;

// ============================================================================
//  Render settings – fast but nice enough
// ============================================================================
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
    s.output_dir = "frames_tunnel_energy_sections";

    // Output: unique video name
    s.output_mode = OutputMode::FFmpegVideo;
    s.ffmpeg_path = "ffmpeg";
    s.ffmpeg_output = g_base_output_filepath + "/" + baseName + ".mp4";
    s.ffmpeg_extra_args = "-c:v libx264 -preset veryfast -crf 18";

    return s;
}

// ============================================================================
//  Tiny helpers
// ============================================================================
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

inline float clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

// ============================================================================
//  FlightPath – precomputed random walk inside a big cube
// ============================================================================
struct FlightPath
{
    std::vector<Vec3> nodes;

    float step_length = 40.0f;  // units between samples along the path
    float box_half = 2000.0f; // cube is [-box_half, box_half]^3

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

                    if (axis == 0) boundaryPush.x += push;
                    else if (axis == 1) boundaryPush.y += push;
                    else                boundaryPush.z += push;
                };

            addAxisPush(pos.x, 0);
            addAxisPush(pos.y, 1);
            addAxisPush(pos.z, 2);

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

// ============================================================================
//  PathFrame – local coordinate frame along the path
// ============================================================================
struct PathFrame
{
    Vec3 origin;   // position on path at distance s
    Vec3 forward;  // tangent along path
    Vec3 right;    // horizontal-ish
    Vec3 up;       // vertical-ish
};

inline PathFrame path_frame_at(const FlightPath& path, float s)
{
    PathFrame F{};

    float L = path.total_length();
    if (L <= 0.0f)
    {
        F.origin = make_vec3(0.0f, 0.0f, 0.0f);
        F.forward = make_vec3(0.0f, 0.0f, 1.0f);
        F.right = make_vec3(1.0f, 0.0f, 0.0f);
        F.up = make_vec3(0.0f, 1.0f, 0.0f);
        return F;
    }

    if (s < 0.0f) s = 0.0f;
    if (s > L)    s = L;

    float eps = 0.5f * path.step_length;
    if (eps <= 0.0f) eps = 0.02f * L;

    float s0 = s - eps;
    float s1 = s + eps;
    if (s0 < 0.0f) s0 = 0.0f;
    if (s1 > L)    s1 = L;

    Vec3 p0 = path.sample_at(s0);
    Vec3 p1 = path.sample_at(s1);
    Vec3 o = path.sample_at(s);

    Vec3 f = p1 - p0;
    float fl = length3(f);
    if (fl < 1.0e-4f) f = make_vec3(0.0f, 0.0f, 1.0f);
    else              f = f * (1.0f / fl);

    Vec3 worldUp = make_vec3(0.0f, 1.0f, 0.0f);
    Vec3 r = cross3(f, worldUp);
    float rl = length3(r);
    if (rl < 1.0e-4f) r = make_vec3(1.0f, 0.0f, 0.0f);
    else              r = r * (1.0f / rl);

    Vec3 u = normalize3(cross3(r, f));

    F.origin = o;
    F.forward = f;
    F.right = r;
    F.up = u;
    return F;
}

inline Vec3 local_to_world(const PathFrame& F, const Vec3& local)
{
    // local = (x along right, y along up, z along forward)
    return F.origin
        + local.x * F.right
        + local.y * F.up
        + local.z * F.forward;
}

// ============================================================================
//  Camera rig – parameters for inside/orbit camera
// ============================================================================
struct CameraRig
{
    bool  inside_mode = true;  // true = fly inside, false = orbit outside

    // Inside mode
    float fly_speed = 40.0f; // units per second along the path
    float fov_inside = 75.0f;

    // Orbit mode
    float orbit_radius = 260.0f;
    float orbit_height = 60.0f;
    float orbit_speed = 0.10f;  // revs per second-ish
    float fov_orbit = 60.0f;
};

// ============================================================================
//  Sections along the path
// ============================================================================
enum class SectionKind
{
    Tunnel,
    Empty,
    RingField
};

struct Section
{
    float s_start = 0.0f;
    float s_end = 0.0f;
    SectionKind kind = SectionKind::Tunnel;
};

// ============================================================================
//  TunnelSection – just shape parameters
// ============================================================================
struct TunnelSection
{
    int   segments = 6;    // hexagon
    int   rings = 40;   // discretization for drawing
    float radius = 40.0f;
};

// ============================================================================
//  TunnelSurfacePoint & Painter
// ============================================================================
struct TunnelSurfacePoint
{
    float u = 0.0f; // 0..1 along the section
    float v = 0.0f; // 0..1 around circumference
    Vec3  worldPos;
    PathFrame frame;
};

inline TunnelSurfacePoint make_surface_point(const FlightPath& path,
    const TunnelSection& sec,
    float s0, float s1,
    float u, float v)
{
    TunnelSurfacePoint P{};

    u = clampf(u, 0.0f, 1.0f);
    v = clampf(v, 0.0f, 1.0f);

    float L = s1 - s0;
    if (L <= 0.0f) L = 1.0f;

    float s = s0 + u * L;

    PathFrame F = path_frame_at(path, s);

    const float twoPi = 6.2831853f;
    float angle = v * twoPi;
    float R = sec.radius;

    Vec3 local = make_vec3(std::cos(angle) * R,
        std::sin(angle) * R,
        0.0f);

    Vec3 worldPos = local_to_world(F, local);

    P.u = u;
    P.v = v;
    P.worldPos = worldPos;
    P.frame = F;
    return P;
}

struct TunnelSurfacePainter
{
    Vec3 stripeColor = make_vec3(1.8f, 0.8f, 0.9f);

    // Simple animated diagonal stripes on the tunnel walls
    void paint_stripes(LineEmitContext& ctx,
        const FlightPath& path,
        const TunnelSection& sec,
        float s0, float s1,
        float t) const
    {
        const int stripeCount = 7;
        const int segs = 40;

        const float twoPi = 6.2831853f;

        for (int i = 0; i < stripeCount; ++i)
        {
            // Base position along the tunnel
            float uBase = 0.15f + 0.7f * (float)i / (float)(stripeCount - 1);

            for (int j = 0; j < segs; ++j)
            {
                float v0 = (float)j / (float)segs;
                float v1 = (float)(j + 1) / (float)segs;

                // Tilt the stripe in (u,v) space
                float tilt = 0.25f;
                float u0 = uBase + tilt * (v0 - 0.5f);
                float u1 = uBase + tilt * (v1 - 0.5f);

                // Wrap u into [0,1]
                auto wrap01 = [](float x)
                    {
                        x = std::fmod(x, 1.0f);
                        if (x < 0.0f) x += 1.0f;
                        return x;
                    };
                u0 = wrap01(u0);
                u1 = wrap01(u1);

                TunnelSurfacePoint P0 = make_surface_point(path, sec, s0, s1, u0, v0);
                TunnelSurfacePoint P1 = make_surface_point(path, sec, s0, s1, u1, v1);

                float anim = 0.5f +
                    0.5f * std::sin(twoPi * u0 + t * 1.8f + (float)i * 0.7f);

                Vec3 col = stripeColor * (0.35f + 0.65f * anim);

                emit_line(ctx,
                    P0.worldPos,
                    P1.worldPos,
                    col,
                    0.18f,
                    85.0f);
            }
        }
    }
};

// ============================================================================
//  Tunnel – draws geometry following the FlightPath using TunnelSection shape
// ============================================================================
struct Tunnel
{
    TunnelSection section{};

    // Colors inspired by your reference
    Vec3 frameColor = make_vec3(0.25f, 0.55f, 1.6f) * 2.0f;  // ring frames
    Vec3 barColor = make_vec3(1.6f, 0.4f, 1.6f) * 2.0f;    // longitudinal bars
    Vec3 coreColor = make_vec3(1.4f, 1.2f, 1.8f) * 2.0f;    // central core

    bool draw_core = true;

    void draw_interval(LineEmitContext& ctx,
        const FlightPath& path,
        float s0, float s1,
        float t) const
    {
        (void)t;

        const int rings = section.rings;
        const int segments = section.segments;
        if (rings < 2 || segments < 3) return;
        if (s1 <= s0) return;

        float L = s1 - s0;

        // 1) Rings
        for (int r = 0; r < rings; ++r)
        {
            float u = (rings > 1) ? (float)r / (float)(rings - 1) : 0.0f;
            float s = s0 + u * L;

            PathFrame F = path_frame_at(path, s);
            float R = section.radius;

            float pathFrac = u;
            float fade = 0.4f + 0.6f * (1.0f - pathFrac);

            const float twoPi = 6.2831853f;
            float angleOffset = twoPi * 0.5f / (float)segments;

            for (int seg = 0; seg < segments; ++seg)
            {
                int segNext = (seg + 1) % segments;

                float a0 = twoPi * (float)seg / (float)segments + angleOffset;
                float a1 = twoPi * (float)segNext / (float)segments + angleOffset;

                Vec3 local0 = make_vec3(std::cos(a0) * R,
                    std::sin(a0) * R,
                    0.0f);
                Vec3 local1 = make_vec3(std::cos(a1) * R,
                    std::sin(a1) * R,
                    0.0f);

                Vec3 p0 = local_to_world(F, local0);
                Vec3 p1 = local_to_world(F, local1);

                emit_line(ctx,
                    p0, p1,
                    frameColor * fade,
                    0.32f,
                    110.0f);
            }
        }

        // 2) Longitudinal bars
        for (int r = 0; r < rings - 1; ++r)
        {
            float u0 = (float)r / (float)(rings - 1);
            float u1 = (float)(r + 1) / (float)(rings - 1);

            float sA = s0 + u0 * L;
            float sB = s0 + u1 * L;

            PathFrame F0 = path_frame_at(path, sA);
            PathFrame F1 = path_frame_at(path, sB);
            float R = section.radius;

            float pathFrac = u0;
            float fade = 0.5f + 0.5f * (1.0f - pathFrac);

            const float twoPi = 6.2831853f;
            float angleOffset = twoPi * 0.5f / (float)segments;

            for (int seg = 0; seg < segments; ++seg)
            {
                float a = twoPi * (float)seg / (float)segments + angleOffset;

                Vec3 local0 = make_vec3(std::cos(a) * R,
                    std::sin(a) * R,
                    0.0f);
                Vec3 local1 = local0; // same angle

                Vec3 p0 = local_to_world(F0, local0);
                Vec3 p1 = local_to_world(F1, local1);

                emit_line(ctx,
                    p0, p1,
                    barColor * fade,
                    0.36f,
                    130.0f);
            }
        }

        // 3) Core line (center of the tunnel)
        if (draw_core)
        {
            int coreSegs = rings * 3;
            if (coreSegs < 4) coreSegs = 4;

            for (int i = 0; i < coreSegs - 1; ++i)
            {
                float u0 = (float)i / (float)(coreSegs - 1);
                float u1 = (float)(i + 1) / (float)(coreSegs - 1);

                float ss0 = s0 + u0 * L;
                float ss1 = s0 + u1 * L;

                Vec3 c0 = path.sample_at(ss0);
                Vec3 c1 = path.sample_at(ss1);

                float pulse = 0.7f + 0.3f *
                    std::sin(6.2831853f * u0 + t * 1.3f);

                emit_line(ctx,
                    c0, c1,
                    coreColor * pulse,
                    0.45f,
                    180.0f);
            }
        }
    }
};

// ============================================================================
//  EnergyFlow – pulses traveling through the tunnel center (per section)
// ============================================================================
struct EnergyFlow
{
    int   pulse_count = 7;
    float pulse_speed = 25.0f;  // units per second along section
    float pulse_length = 18.0f;  // pulse length along the section
    float thickness = 0.75f;
    float base_intensity = 260.0f;

    Vec3 baseColor = make_vec3(2.0f, 1.8f, 0.6f); // warm golden-white

    void draw_interval(LineEmitContext& ctx,
        const FlightPath& path,
        float s0, float s1,
        float t) const
    {
        float L = s1 - s0;
        if (L <= 0.0f) return;

        for (int i = 0; i < pulse_count; ++i)
        {
            float phase = (float)i / (float)pulse_count;

            float u = std::fmod(t * (pulse_speed / L) + phase, 1.0f);
            if (u < 0.0f) u += 1.0f;

            float s_center = s0 + u * L;

            float halfLen = 0.5f * pulse_length;
            float sa = s_center - halfLen;
            float sb = s_center + halfLen;

            if (sb < s0 || sa > s1) continue;

            if (sa < s0) sa = s0;
            if (sb > s1) sb = s1;

            Vec3 p0 = path.sample_at(sa);
            Vec3 p1 = path.sample_at(sb);

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

// ============================================================================
//  External geometry: GeoInstance / GeoSet
// ============================================================================
enum class GeoType
{
    Billboard,
    WireBox
};

struct GeoInstance
{
    float s = 0.0f;   // along path
    Vec3  localPos;   // local (x,y,z) in path frame
    Vec3  scale = make_vec3(1.0f, 1.0f, 1.0f);

    float yaw = 0.0f; // rotation around local up (radians)
    float pitch = 0.0f;
    float roll = 0.0f;

    GeoType type = GeoType::Billboard;

    Vec3  color = make_vec3(1.0f, 1.0f, 1.0f);
    float thickness = 0.25f;
    float intensity = 150.0f;
};

struct GeoSet
{
    std::vector<GeoInstance> instances;

    void build_demo(const FlightPath& path,
        const TunnelSection& tunnelShape,
        const std::vector<Section>& sections)
    {
        instances.clear();

        const float twoPi = 6.2831853f;
        float R = tunnelShape.radius + 30.0f;

        // Place a few billboards around each Tunnel section
        for (const Section& sec : sections)
        {
            if (sec.kind != SectionKind::Tunnel) continue;
            if (sec.s_end <= sec.s_start) continue;

            int countPerSection = 6;
            for (int i = 0; i < countPerSection; ++i)
            {
                float u = Random::random_01();
                float s = sec.s_start + u * (sec.s_end - sec.s_start);

                float angle = twoPi * Random::random_01();

                Vec3 localPos = make_vec3(std::cos(angle) * R,
                    std::sin(angle) * R,
                    0.0f);

                GeoInstance inst;
                inst.s = s;
                inst.localPos = localPos;
                inst.scale = make_vec3(35.0f, 22.0f, 1.0f);
                inst.yaw = 0.0f;
                inst.type = GeoType::Billboard;
                inst.color = make_vec3(1.8f, 0.8f, 0.4f);
                inst.thickness = 0.23f;
                inst.intensity = 155.0f;

                instances.push_back(inst);
            }
        }

        (void)path; // not needed inside build, but kept for future extensions
    }

    void draw_billboard(LineEmitContext& ctx,
        const PathFrame& F,
        const GeoInstance& inst,
        float t) const
    {
        (void)t;

        // Base center in world space
        Vec3 base = local_to_world(F, inst.localPos);

        // Local axes
        Vec3 right = F.right;
        Vec3 up = F.up;

        // Simple yaw around local up (if we want later)
        // For now, we ignore yaw/pitch/roll to keep it simple.

        Vec3 rightScaled = right * inst.scale.x;
        Vec3 upScaled = up * inst.scale.y;

        Vec3 pTL = base - rightScaled + upScaled;
        Vec3 pTR = base + rightScaled + upScaled;
        Vec3 pBR = base + rightScaled - upScaled;
        Vec3 pBL = base - rightScaled - upScaled;

        Vec3 frameCol = inst.color;

        // Frame
        emit_line(ctx, pTL, pTR, frameCol, inst.thickness, inst.intensity);
        emit_line(ctx, pTR, pBR, frameCol, inst.thickness, inst.intensity);
        emit_line(ctx, pBR, pBL, frameCol, inst.thickness, inst.intensity);
        emit_line(ctx, pBL, pTL, frameCol, inst.thickness, inst.intensity);

        // Simple "scanline" interior for flavor
        int scanLines = 12;
        for (int i = 0; i < scanLines; ++i)
        {
            float v = (scanLines > 1) ? (float)i / (float)(scanLines - 1) : 0.0f;
            float k = 1.0f - 2.0f * v; // 1..-1

            Vec3 rowOffset = upScaled * k * 0.8f;

            Vec3 a = base - rightScaled * 0.85f + rowOffset;
            Vec3 b = base + rightScaled * 0.85f + rowOffset;

            float pulse = 0.5f + 0.5f * std::sin(6.2831853f * v + t * 1.2f);

            Vec3 col = frameCol * (0.3f + 0.7f * pulse);

            emit_line(ctx,
                a, b,
                col,
                inst.thickness * 0.5f,
                inst.intensity * 0.8f);
        }
    }

    void draw(LineEmitContext& ctx,
        const FlightPath& path,
        float t) const
    {
        for (const GeoInstance& inst : instances)
        {
            PathFrame F = path_frame_at(path, inst.s);

            switch (inst.type)
            {
            case GeoType::Billboard:
                draw_billboard(ctx, F, inst, t);
                break;
            case GeoType::WireBox:
            default:
                // Not implemented yet in this example
                break;
            }
        }
    }
};

// ============================================================================
//  Universe – scene container
// ============================================================================
struct Universe
{
    FlightPath  path{};
    CameraRig   camera{};
    Tunnel      tunnel{};
    EnergyFlow  energy{};
    TunnelSurfacePainter painter{};
    GeoSet      geo{};
    std::vector<Section> sections;

    Universe()
    {
        // 1) Build a global flight path inside a 4km cube
        const int   nodeCount = 600;
        const float step = 40.0f;
        const float cubeHalf = 2000.0f;
        path.build_random_walk(nodeCount, step, cubeHalf);

        float L = path.total_length();
        if (L <= 0.0f) L = 1.0f;

        // 2) Define a few sections along the path
        //      [0, s1]   : Tunnel
        //      [s1, s2]  : Empty
        //      [s2, s3]  : Tunnel again
        float s3 = 0.90f * L;
        float s1 = 0.25f * s3;
        float s2 = 0.60f * s3;

        sections.clear();
        sections.push_back(Section{ 0.0f, s1, SectionKind::Tunnel });
        sections.push_back(Section{ s1,  s2, SectionKind::Empty });
        sections.push_back(Section{ s2,  s3, SectionKind::Tunnel });

        // 3) Tunnel shape
        tunnel.section.segments = 6;
        tunnel.section.rings = 40;
        tunnel.section.radius = 40.0f;

        // 4) Camera defaults
        camera.inside_mode = true;
        camera.fly_speed = 40.0f;

        // 5) Energy tweaks
        energy.pulse_count = 9;
        energy.pulse_speed = 28.0f;
        energy.pulse_length = 24.0f;

        // 6) External geometry demo (billboards)
        geo.build_demo(path, tunnel.section, sections);
    }

    // Helper: length of "interesting" region for camera
    float interesting_length() const
    {
        if (sections.empty())
            return path.total_length();

        const Section& last = sections.back();
        return last.s_end;
    }
};

// ============================================================================
//  Camera callback – reads Universe and positions camera
// ============================================================================
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
    FlightPath& path = uni->path;

    float totalLen = uni->interesting_length();
    if (totalLen <= 0.0f) totalLen = path.total_length();
    if (totalLen <= 0.0f) totalLen = 1.0f;

    if (cr.inside_mode)
    {
        // --- Camera flies INSIDE the path centerline ---
        float sCam = std::fmod(t * cr.fly_speed, totalLen);
        if (sCam < 0.0f) sCam += totalLen;

        float lookAheadDist = 40.0f;
        float sAhead = sCam + lookAheadDist;
        if (sAhead > totalLen) sAhead = totalLen;

        Vec3 eye = path.sample_at(sCam);
        Vec3 target = path.sample_at(sAhead);

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
        cam.up_x = up.x;      cam.up_y = up.y;      cam.up_z = up.z;

        cam.has_custom_fov = true;
        cam.fov_y_deg = cr.fov_inside;
    }
    else
    {
        // --- External orbit around mid of "interesting" region ---
        float centerS = uni->interesting_length() * 0.5f;
        Vec3  center = path.sample_at(centerS);

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
        cam.up_x = up.x;      cam.up_y = up.y;      cam.up_z = up.z;

        cam.has_custom_fov = true;
        cam.fov_y_deg = cr.fov_orbit;
    }
}

// ============================================================================
//  Line callback – draws sections + external geometry
// ============================================================================
void line_push_callback(int frame, float t, LineEmitContext& ctx)
{
    (void)frame;

    auto* uni = static_cast<Universe*>(ctx.user_ptr);
    if (!uni) return;

    FlightPath& path = uni->path;
    Tunnel& tunnel = uni->tunnel;
    EnergyFlow& energy = uni->energy;
    TunnelSurfacePainter& painter = uni->painter;

    // 1) Draw per-section content
    for (const Section& sec : uni->sections)
    {
        if (sec.s_end <= sec.s_start) continue;

        switch (sec.kind)
        {
        case SectionKind::Tunnel:
        {
            tunnel.draw_interval(ctx, path, sec.s_start, sec.s_end, t);
            energy.draw_interval(ctx, path, sec.s_start, sec.s_end, t);
            painter.paint_stripes(ctx, path, tunnel.section,
                sec.s_start, sec.s_end, t);
        } break;

        case SectionKind::Empty:
        {
            // For now: nothing – this creates a feeling of empty space
            // You could add rare rings / markers here later.
        } break;

        case SectionKind::RingField:
        {
            // Not used in this demo – reserved for future experiments.
        } break;
        }
    }

    // 2) External geometry (billboards attached to the path)
    uni->geo.draw(ctx, path, t);

    ctx.flush_now();
}

// ============================================================================
//  Entry
// ============================================================================
int main()
{
    std::cout << "example_tunnel_energy_universe_sections\n";
    std::cout << "This code is in file: " << __FILE__ << "\n";

    const std::string uniqueName = WIRE_UNIQUE_NAME(g_base_output_filepath);
    std::cout << "Video name:  " << uniqueName << "\n";
    std::cout << "Output path: " << g_base_output_filepath
        << "/" << uniqueName << ".mp4\n";

    RenderSettings settings = init_render_settings(uniqueName, 4);

    Universe universe{};
    // You can play with these:
    // universe.camera.inside_mode = false; // orbit outside
    // universe.tunnel.section.rings = 60;

    renderSequencePush(
        settings,
        camera_callback,
        line_push_callback,
        &universe // passed as user_ptr to both callbacks
    );

    VLC::play(g_base_output_filepath + "/" + uniqueName + ".mp4");
    return 0;
}
