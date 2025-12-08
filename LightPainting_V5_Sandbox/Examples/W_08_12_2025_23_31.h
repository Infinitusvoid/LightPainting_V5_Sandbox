#pragma once

#include "WireUtil.h"


#include <fstream>
#include <cstdint>

#include <cctype> // for std::toupper

#include "../External_libs/tinyply-master/tinyply-master/source/tinyply.h" // adjust path if needed
#include "../External_libs/tinyply-master/tinyply-master/source/tinyply.cpp" // adjust path if needed

using namespace WireEngine;
using std::vector;

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

    // Simple tone; you can tune this
    s.exposure = 2.5f * 10.0f;
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
    s.output_dir = "frames_tunnel_world";

    // Output: unique video name
    s.output_mode = OutputMode::FFmpegVideo;
    s.ffmpeg_path = "ffmpeg";
    s.ffmpeg_output = g_base_output_filepath + "/" + baseName + ".mp4";
    s.ffmpeg_extra_args = "-c:v libx264 -preset veryfast -crf 18";

    return s;
}

// -----------------------------------------------------------------------------
// Tiny helpers
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

inline float clampf(float v, float lo, float hi)
{
    return (v < lo) ? lo : (v > hi ? hi : v);
}

// -----------------------------------------------------------------------------
// FlightPath – precomputed random walk inside a big cube
// -----------------------------------------------------------------------------
struct FlightPath
{
    vector<Vec3> nodes;

    float step_length = 40.0f; // approximate distance between samples
    float box_half = 2000.0f;  // cube is [-box_half, box_half]^3

    void build_random_walk(int nodeCount,
        float stepLen,
        float cubeHalf)
    {
        nodes.clear();
        if (nodeCount < 2) nodeCount = 2;

        step_length = stepLen;
        box_half = cubeHalf;

        Vec3 pos = make_vec3(0.0f, 0.0f, -cubeHalf * 0.25f);
        Vec3 dir = make_vec3(0.0f, 0.0f, 1.0f);

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
                    float push = (0.3f + 0.9f * t) * (-sign);

                    if (axis == 0)      boundaryPush.x += push;
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
    float fly_speed = 40.0f;   // units per second along the tunnel
    float fov_inside = 75.0f;
    float cam_back_offset = 20.0f;   // how far back from centerline we sit
    float look_ahead_dist = 80.0f;   // how far ahead we look along the path

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
    int   rings = 80;      // how many cross-sections
    float radius = 40.0f;

    float length_used = 0.0f; // path length actually used

    void bind_path(const FlightPath* p)
    {
        path = p;
        if (!path)
        {
            length_used = 0.0f;
            return;
        }
        length_used = path->total_length();
        if (length_used < 0.0f) length_used = 0.0f;
    }

    float total_length() const
    {
        return length_used;
    }

    float s_for_ring(int ringIdx) const
    {
        if (rings <= 1) return 0.0f;

        if (ringIdx < 0) ringIdx = 0;
        if (ringIdx > rings - 1) ringIdx = rings - 1;

        float t = (float)ringIdx / (float)(rings - 1);
        return t * length_used;
    }

    Vec3 center_for_ring(int ringIdx) const
    {
        if (!path)
            return make_vec3(0.0f, 0.0f, (float)ringIdx * 40.0f);

        float s = s_for_ring(ringIdx);
        return path->sample_at(s);
    }

    Vec3 center_along(float s) const
    {
        if (!path)
            return make_vec3(0.0f, 0.0f, s);

        if (s < 0.0f)        s = 0.0f;
        if (s > length_used) s = length_used;
        return path->sample_at(s);
    }

    Vec3 tangent_along(float s) const
    {
        float L = total_length();
        if (L <= 0.0f) return make_vec3(0.0f, 0.0f, 1.0f);

        float eps = L / (float)(rings * 2);
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

    Vec3 ring_vertex(int ringIdx, int segIdx) const
    {
        const float twoPi = 6.2831853f;

        Vec3 center = center_for_ring(ringIdx);
        float R = radius_for_ring(ringIdx);

        float angleOffset = twoPi * 0.5f / (float)segments;
        float a = twoPi * (float)segIdx / (float)segments + angleOffset;

        float x = std::cos(a) * R;
        float y = std::sin(a) * R;

        return make_vec3(center.x + x, center.y + y, center.z);
    }
};

// -----------------------------------------------------------------------------
// PathFrame (position + basis) – reused by multiple systems
// -----------------------------------------------------------------------------
struct PathFrame
{
    Vec3 pos;
    Vec3 forward;
    Vec3 right;
    Vec3 up;
};

inline PathFrame make_path_frame(const TunnelSection& sec, float s)
{
    Vec3 pos = sec.center_along(s);
    Vec3 forward = sec.tangent_along(s);

    Vec3 worldUp = make_vec3(0.0f, 1.0f, 0.0f);
    Vec3 right = cross3(forward, worldUp);
    float rLen = length3(right);
    if (rLen < 1.0e-4f)
        right = make_vec3(1.0f, 0.0f, 0.0f);
    else
        right = right * (1.0f / rLen);

    Vec3 up = normalize3(cross3(right, forward));

    PathFrame f{};
    f.pos = pos;
    f.forward = forward;
    f.right = right;
    f.up = up;
    return f;
}

// -----------------------------------------------------------------------------
// Tunnel – draws the tube geometry along TunnelSection
// -----------------------------------------------------------------------------
struct Tunnel
{
    TunnelSection section{};

    // Colors
    Vec3 frameColor = make_vec3(0.25f, 0.55f, 1.6f) * 2.0f;   // ring frames
    Vec3 barColor = make_vec3(1.6f, 0.4f, 1.6f) * 2.0f;     // longitudinal bars
    Vec3 coreColor = make_vec3(1.4f, 1.2f, 1.8f) * 2.0f;     // central core

    bool draw_core = true;

    void draw_range(LineEmitContext& ctx, float t,
        float s_start, float s_end) const
    {
        (void)t;

        const int rings = section.rings;
        const int segments = section.segments;
        float L = section.total_length();
        if (rings < 2 || segments < 3 || L <= 0.0f)
            return;

        if (s_end <= s_start) return;

        float s_lo = (s_start < 0.0f) ? 0.0f : s_start;
        float s_hi = (s_end > L) ? L : s_end;

        if (s_hi <= s_lo) return;

        float invLenLocal = 1.0f / (s_hi - s_lo);

        // 1) Ring frames
        for (int r = 0; r < rings; ++r)
        {
            float s = section.s_for_ring(r);
            if (s < s_lo || s > s_hi) continue;

            float localFrac = (s - s_lo) * invLenLocal; // 0..1 within section
            float fade = 0.4f + 0.6f * (1.0f - localFrac);

            for (int sIdx = 0; sIdx < segments; ++sIdx)
            {
                int sn = (sIdx + 1) % segments;

                Vec3 a = section.ring_vertex(r, sIdx);
                Vec3 b = section.ring_vertex(r, sn);

                emit_line(ctx,
                    a, b,
                    frameColor * fade,
                    0.32f,
                    110.0f);
            }
        }

        // 2) Longitudinal bars
        for (int r = 0; r < rings - 1; ++r)
        {
            float s0 = section.s_for_ring(r);
            float s1 = section.s_for_ring(r + 1);

            if ((s0 < s_lo && s1 < s_lo) ||
                (s0 > s_hi && s1 > s_hi))
                continue;

            float sMid = 0.5f * (s0 + s1);
            float localFrac = (sMid - s_lo) * invLenLocal;
            if (localFrac < 0.0f) localFrac = 0.0f;
            if (localFrac > 1.0f) localFrac = 1.0f;
            float fade = 0.5f + 0.5f * (1.0f - localFrac);

            for (int sIdx = 0; sIdx < segments; ++sIdx)
            {
                Vec3 a = section.ring_vertex(r, sIdx);
                Vec3 b = section.ring_vertex(r + 1, sIdx);

                emit_line(ctx,
                    a, b,
                    barColor * fade,
                    0.36f,
                    130.0f);
            }
        }

        // 3) Core line
        if (draw_core)
        {
            int coreSegs = rings * 3;
            for (int i = 0; i < coreSegs - 1; ++i)
            {
                float u0 = (float)i / (float)(coreSegs - 1);
                float u1 = (float)(i + 1) / (float)(coreSegs - 1);

                float s0 = u0 * L;
                float s1 = u1 * L;

                if ((s0 < s_lo && s1 < s_lo) ||
                    (s0 > s_hi && s1 > s_hi))
                    continue;

                if (s0 < s_lo) s0 = s_lo;
                if (s1 > s_hi) s1 = s_hi;

                Vec3 c0 = section.center_along(s0);
                Vec3 c1 = section.center_along(s1);

                float localFrac = (s0 - s_lo) * invLenLocal;
                if (localFrac < 0.0f) localFrac = 0.0f;
                if (localFrac > 1.0f) localFrac = 1.0f;
                float pulse = 0.7f + 0.3f * std::sin(6.2831853f * localFrac + t * 1.3f);

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
// EnergyFlow – pulses traveling along centerline
// -----------------------------------------------------------------------------
struct EnergyFlow
{
    int   pulse_count = 7;
    float pulse_speed = 25.0f;
    float pulse_length = 18.0f;
    float thickness = 0.75f;
    float base_intensity = 260.0f;

    Vec3 baseColor = make_vec3(2.0f, 1.8f, 0.6f);

    void draw_range(LineEmitContext& ctx,
        const TunnelSection& sec,
        float s_start, float s_end,
        float t) const
    {
        float L = sec.total_length();
        if (L <= 0.0f) return;

        if (s_end <= s_start) return;

        float s_lo = (s_start < 0.0f) ? 0.0f : s_start;
        float s_hi = (s_end > L) ? L : s_end;
        if (s_hi <= s_lo) return;

        for (int i = 0; i < pulse_count; ++i)
        {
            float phase = (float)i / (float)pulse_count;

            float u = std::fmod(t * (pulse_speed / L) + phase, 1.0f);
            if (u < 0.0f) u += 1.0f;

            float s_center = u * L;

            float halfLen = 0.5f * pulse_length;
            float s0 = s_center - halfLen;
            float s1 = s_center + halfLen;

            if (s1 < s_lo || s0 > s_hi)
                continue;

            if (s0 < s_lo) s0 = s_lo;
            if (s1 > s_hi) s1 = s_hi;

            Vec3 p0 = sec.center_along(s0);
            Vec3 p1 = sec.center_along(s1);

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
// TunnelSurfacePainter – graffiti / strokes on the walls
// -----------------------------------------------------------------------------
struct TunnelSurfacePainter
{
    int tiles_u = 12;
    int tiles_v = 24;

    void draw_range(LineEmitContext& ctx,
        const TunnelSection& sec,
        float s_start, float s_end,
        float t) const
    {
        int   rings = sec.rings;
        int   segs = sec.segments;
        float L = sec.total_length();
        if (rings < 2 || segs < 3 || L <= 0.0f) return;
        if (s_end <= s_start) return;

        float s_lo = (s_start < 0.0f) ? 0.0f : s_start;
        float s_hi = (s_end > L) ? L : s_end;
        if (s_hi <= s_lo) return;

        for (int r = 0; r < rings; ++r)
        {
            float s = sec.s_for_ring(r);
            if (s < s_lo || s > s_hi) continue;

            float v = (float)r / (float)(rings - 1);

            // Only some rings get strokes
            if (((r / 2) % 4) != 0) continue;

            for (int sIdx = 0; sIdx < segs; ++sIdx)
            {
                float u = (float)sIdx / (float)segs;

                // Sparse pattern
                if (((sIdx + r) % 5) != 0) continue;

                Vec3 base = sec.ring_vertex(r, sIdx);
                Vec3 center = sec.center_for_ring(r);
                Vec3 radial = base - center;
                float len = length3(radial);
                if (len < 1.0e-4f)
                    radial = make_vec3(0.0f, 1.0f, 0.0f);
                else
                    radial = radial * (1.0f / len);

                float innerOffset = -6.0f;
                float strokeLen = 14.0f;

                Vec3 p0 = base + radial * innerOffset;
                Vec3 p1 = p0 + radial * strokeLen;

                float glow = 0.6f + 0.4f * std::sin(6.2831853f * (u + v) + t * 1.5f);
                Vec3 col = make_vec3(0.8f, 1.5f, 1.9f) * glow;

                emit_line(ctx, p0, p1, col, 0.16f, 140.0f);
            }
        }
    }
};

// -----------------------------------------------------------------------------
// GeoSet – generic external geometry attached to the path
// -----------------------------------------------------------------------------
enum class GeoType
{
    Billboard,
    WireBox
};

struct GeoInstance
{
    float s = 0.0f;    // distance along path
    Vec3  localPos{};  // in path frame (right, up, forward)
    float scale = 1.0f;
    Vec3  color{};
    GeoType type = GeoType::Billboard;
};

struct GeoSet
{
    vector<GeoInstance> instances;

    void clear()
    {
        instances.clear();
    }

    void build_example(const TunnelSection& sec)
    {
        instances.clear();

        float L = sec.total_length();
        if (L <= 0.0f) return;

        int count = 12;
        for (int i = 0; i < count; ++i)
        {
            float u = (float)(i + 1) / (float)(count + 1);
            float s = u * L;

            GeoInstance inst{};
            inst.s = s;
            inst.type = (Random::random_01() < 0.5f) ? GeoType::Billboard : GeoType::WireBox;

            float side = (Random::random_01() < 0.5f) ? -1.0f : 1.0f;
            float outward = sec.radius + 20.0f + Random::random_01() * 40.0f;

            inst.localPos = make_vec3(side * outward,
                Random::random_signed() * 20.0f,
                Random::random_signed() * 10.0f);

            inst.scale = 25.0f + 25.0f * Random::random_01();

            float k = 0.6f + 0.4f * Random::random_01();
            inst.color = make_vec3(0.4f * k, 0.7f * k, 1.3f * k);

            instances.push_back(inst);
        }
    }

    void draw_range(LineEmitContext& ctx,
        const TunnelSection& sec,
        float s_start, float s_end) const
    {
        if (instances.empty()) return;

        float L = sec.total_length();
        if (L <= 0.0f) return;

        float s_lo = (s_start < 0.0f) ? 0.0f : s_start;
        float s_hi = (s_end > L) ? L : s_end;
        if (s_hi <= s_lo) return;

        for (const GeoInstance& inst : instances)
        {
            if (inst.s < s_lo || inst.s > s_hi) continue;

            PathFrame frame = make_path_frame(sec, inst.s);

            Vec3 anchor =
                frame.pos +
                frame.right * inst.localPos.x +
                frame.up * inst.localPos.y +
                frame.forward * inst.localPos.z;

            if (inst.type == GeoType::Billboard)
            {
                float halfW = inst.scale;
                float halfH = inst.scale * 0.6f;

                Vec3 rightScaled = frame.right * halfW;
                Vec3 upScaled = frame.up * halfH;

                Vec3 pTL = anchor - rightScaled + upScaled;
                Vec3 pTR = anchor + rightScaled + upScaled;
                Vec3 pBR = anchor + rightScaled - upScaled;
                Vec3 pBL = anchor - rightScaled - upScaled;

                float thick = 0.25f;
                float inten = 120.0f; // slightly dimmer to let text stand out

                emit_line(ctx, pTL, pTR, inst.color, thick, inten);
                emit_line(ctx, pTR, pBR, inst.color, thick, inten);
                emit_line(ctx, pBR, pBL, inst.color, thick, inten);
                emit_line(ctx, pBL, pTL, inst.color, thick, inten);

                // A little inner X
                emit_line(ctx, pTL, pBR, inst.color * 0.8f, thick * 0.7f, inten * 0.8f);
                emit_line(ctx, pTR, pBL, inst.color * 0.8f, thick * 0.7f, inten * 0.8f);
            }
            else // WireBox
            {
                float h = inst.scale * 0.5f;

                Vec3 ex = frame.right * h;
                Vec3 ey = frame.up * h;
                Vec3 ez = frame.forward * h;

                Vec3 c000 = anchor - ex - ey - ez;
                Vec3 c001 = anchor - ex - ey + ez;
                Vec3 c010 = anchor - ex + ey - ez;
                Vec3 c011 = anchor - ex + ey + ez;
                Vec3 c100 = anchor + ex - ey - ez;
                Vec3 c101 = anchor + ex - ey + ez;
                Vec3 c110 = anchor + ex + ey - ez;
                Vec3 c111 = anchor + ex + ey + ez;

                float thick = 0.18f;
                float inten = 100.0f;
                Vec3 col = inst.color * 0.9f;

                auto edge = [&](const Vec3& a, const Vec3& b)
                    {
                        emit_line(ctx, a, b, col, thick, inten);
                    };

                // 12 edges
                edge(c000, c001); edge(c001, c011);
                edge(c011, c010); edge(c010, c000);

                edge(c100, c101); edge(c101, c111);
                edge(c111, c110); edge(c110, c100);

                edge(c000, c100); edge(c001, c101);
                edge(c010, c110); edge(c011, c111);
            }
        }
    }
};

// -----------------------------------------------------------------------------
// Simple 3x5 line font + TextOnWires
// -----------------------------------------------------------------------------
struct FontGlyph
{
    const char* rows[5]; // each row is 3 chars: '.' or '#'
};

inline const FontGlyph* get_font_glyph(char c)
{
    unsigned char uc = (unsigned char)c;
    c = (char)std::toupper(uc);

    // 3x5 stroke font for A–Z
    static const FontGlyph GL_A = { { ".#.", "#.#", "###", "#.#", "#.#" } };
    static const FontGlyph GL_B = { { "##.", "#.#", "##.", "#.#", "##." } };
    static const FontGlyph GL_C = { { ".##", "#..", "#..", "#..", ".##" } };
    static const FontGlyph GL_D = { { "##.", "#.#", "#.#", "#.#", "##." } };
    static const FontGlyph GL_E = { { "###", "#..", "##.", "#..", "###" } };
    static const FontGlyph GL_F = { { "###", "#..", "##.", "#..", "#.." } };
    static const FontGlyph GL_G = { { ".##", "#..", "#.#", "#.#", ".##" } };
    static const FontGlyph GL_H = { { "#.#", "#.#", "###", "#.#", "#.#" } };
    static const FontGlyph GL_I = { { "###", ".#.", ".#.", ".#.", "###" } };
    static const FontGlyph GL_J = { { "..#", "..#", "..#", "#.#", ".#." } };
    static const FontGlyph GL_K = { { "#.#", "#.#", "##.", "#.#", "#.#" } };
    static const FontGlyph GL_L = { { "#..", "#..", "#..", "#..", "###" } };
    static const FontGlyph GL_M = { { "#.#", "###", "###", "#.#", "#.#" } };
    static const FontGlyph GL_N = { { "#.#", "##.", "##.", "#.#", "#.#" } };
    static const FontGlyph GL_O = { { ".#.", "#.#", "#.#", "#.#", ".#." } };
    static const FontGlyph GL_P = { { "##.", "#.#", "##.", "#..", "#.." } };
    static const FontGlyph GL_Q = { { ".#.", "#.#", "#.#", ".#.", "..#" } };
    static const FontGlyph GL_R = { { "##.", "#.#", "##.", "#.#", "#.#" } };
    static const FontGlyph GL_S = { { ".##", "#..", ".#.", "..#", "##." } };
    static const FontGlyph GL_T = { { "###", ".#.", ".#.", ".#.", ".#." } };
    static const FontGlyph GL_U = { { "#.#", "#.#", "#.#", "#.#", ".#." } };
    static const FontGlyph GL_V = { { "#.#", "#.#", "#.#", "#.#", ".#." } };
    static const FontGlyph GL_W = { { "#.#", "#.#", "###", "###", "#.#" } };
    static const FontGlyph GL_X = { { "#.#", "#.#", ".#.", "#.#", "#.#" } };
    static const FontGlyph GL_Y = { { "#.#", "#.#", ".#.", ".#.", ".#." } };
    static const FontGlyph GL_Z = { { "###", "..#", ".#.", "#..", "###" } };

    switch (c)
    {
    case 'A': return &GL_A;
    case 'B': return &GL_B;
    case 'C': return &GL_C;
    case 'D': return &GL_D;
    case 'E': return &GL_E;
    case 'F': return &GL_F;
    case 'G': return &GL_G;
    case 'H': return &GL_H;
    case 'I': return &GL_I;
    case 'J': return &GL_J;
    case 'K': return &GL_K;
    case 'L': return &GL_L;
    case 'M': return &GL_M;
    case 'N': return &GL_N;
    case 'O': return &GL_O;
    case 'P': return &GL_P;
    case 'Q': return &GL_Q;
    case 'R': return &GL_R;
    case 'S': return &GL_S;
    case 'T': return &GL_T;
    case 'U': return &GL_U;
    case 'V': return &GL_V;
    case 'W': return &GL_W;
    case 'X': return &GL_X;
    case 'Y': return &GL_Y;
    case 'Z': return &GL_Z;
    default:  return nullptr;
    }
}

struct TextLabel
{
    float       s = 0.0f;          // position along path
    std::string text;              // message
    float       size = 4.0f;       // scale of glyph cells
    Vec3        color = make_vec3(2.0f, 1.8f, 2.1f);
    // Now interpreted in *camera space* (right, up, depth along camDir)
    Vec3        offset = make_vec3(0.0f, 0.0f, 0.0f);
};

struct TextOnWires
{
    vector<TextLabel> labels;

    void build_example(const TunnelSection& sec)
    {
        labels.clear();
        float L = sec.total_length();
        if (L <= 0.0f) return;

        // DEBUG / HERO LABEL – big, inside the tunnel, early
        {
            TextLabel lbl{};
            lbl.s = 0.15f * L;              // in first Tunnel section [0, 0.25L]
            lbl.text = "HELLO COSMOS";
            lbl.size = 4.5f;
            lbl.color = make_vec3(2.6f, 2.3f, 2.9f);
            // hover above the center line (in camera-space units)
            lbl.offset = make_vec3(0.0f, sec.radius * 0.25f, 0.0f);
            labels.push_back(lbl);
        }

        // Label 1 – side of tunnel, still large
        {
            TextLabel lbl{};
            lbl.s = 0.12f * L;              // also in first tunnel segment
            lbl.text = "WIRE ENGINE";
            lbl.size = 3.5f;
            lbl.color = make_vec3(2.2f, 2.0f, 2.5f);
            // slightly to the right and up
            lbl.offset = make_vec3(sec.radius * 0.5f, sec.radius * 0.15f, 0.0f);
            labels.push_back(lbl);
        }

        // Label 2 – mid tunnel
        {
            TextLabel lbl{};
            lbl.s = 0.55f * L;              // in third Tunnel section [0.45L, 0.70L]
            lbl.text = "COSMOS TUNNEL";
            lbl.size = 3.5f;
            lbl.color = make_vec3(1.9f, 2.2f, 2.4f);
            // to the left, mid-height
            lbl.offset = make_vec3(-sec.radius * 0.6f, sec.radius * 0.1f, 0.0f);
            labels.push_back(lbl);
        }

        // Label 3 – near the end, overhead
        {
            TextLabel lbl{};
            // IMPORTANT: make sure it's in the last Tunnel section [0.90L, L]
            lbl.s = 0.93f * L;
            lbl.text = "LIGHT PAINTING";
            lbl.size = 3.5f;
            lbl.color = make_vec3(2.5f, 2.0f, 2.1f);
            lbl.offset = make_vec3(0.0f, sec.radius * 0.4f, 0.0f);
            labels.push_back(lbl);
        }
    }

    // NEW: camera-facing text – uses camera basis instead of path basis
    void draw_range(LineEmitContext& ctx,
        const TunnelSection& sec,
        float s_start, float s_end,
        float t,
        const Vec3& camDir,
        const Vec3& camRight,
        const Vec3& camUp) const
    {
        (void)t;

        if (labels.empty()) return;
        float L = sec.total_length();
        if (L <= 0.0f) return;

        float s_lo = (s_start < 0.0f) ? 0.0f : s_start;
        float s_hi = (s_end > L) ? L : s_end;
        if (s_hi <= s_lo) return;

        // Bigger glyph cells so letters read more clearly
        const float cellBase = 1.5f;
        const float glyphWCells = 3.0f;
        const float glyphHCells = 5.0f;
        const float gapCells = 1.0f;
        const float advanceCells = glyphWCells + gapCells;

        for (const TextLabel& lab : labels)
        {
            if (lab.s < s_lo || lab.s > s_hi) continue;
            if (lab.text.empty()) continue;

            // Place label on the path center, then move in camera-space
            Vec3 base = sec.center_along(lab.s);
            base += camRight * lab.offset.x;
            base += camUp * lab.offset.y;
            base += camDir * lab.offset.z; // depth, currently zero

            int   n = (int)lab.text.size();
            float totalWidthCells = (float)n * advanceCells - gapCells;
            float totalHeightCells = glyphHCells;

            float halfW = 0.5f * totalWidthCells * cellBase * lab.size;
            float halfH = 0.5f * totalHeightCells * cellBase * lab.size;

            // Center text around base in the (camRight, camUp) plane
            Vec3 origin = base - camRight * halfW - camUp * halfH;

            for (int idx = 0; idx < n; ++idx)
            {
                char c = lab.text[(size_t)idx];
                if (c == ' ') continue;

                const FontGlyph* glyph = get_font_glyph(c);
                if (!glyph) continue;

                float charOffsetCells = (float)idx * advanceCells;

                for (int row = 0; row < 5; ++row)
                {
                    for (int col = 0; col < 3; ++col)
                    {
                        char pixel = glyph->rows[row][col];
                        if (pixel != '#') continue;

                        float x0 = (charOffsetCells + (float)col) * cellBase * lab.size;
                        float x1 = (charOffsetCells + (float)col + 1.0f) * cellBase * lab.size;
                        float y0 = ((float)row) * cellBase * lab.size;
                        float y1 = ((float)row + 1.0f) * cellBase * lab.size;

                        Vec3 p00 = origin + camRight * x0 + camUp * y0;
                        Vec3 p10 = origin + camRight * x1 + camUp * y0;
                        Vec3 p11 = origin + camRight * x1 + camUp * y1;
                        Vec3 p01 = origin + camRight * x0 + camUp * y1;

                        float flicker = 0.75f + 0.25f *
                            std::sin(t * 2.0f + 0.7f * (float)(row + col + idx));

                        Vec3 colr = lab.color * flicker;
                        float thick = 0.30f * lab.size;          // thick strokes
                        float inten = 2000.0f * flicker;         // very bright

                        emit_line(ctx, p00, p10, colr, thick, inten);
                        emit_line(ctx, p10, p11, colr, thick, inten);
                        emit_line(ctx, p11, p01, colr, thick, inten);
                        emit_line(ctx, p01, p00, colr, thick, inten);
                    }
                }
            }
        }
    }
};

// -----------------------------------------------------------------------------
// Sections + effect system
// -----------------------------------------------------------------------------
enum class SectionKind
{
    Tunnel,
    Empty,
    RingField
};

struct Section
{
    float      s_start = 0.0f;
    float      s_end = 0.0f;
    SectionKind kind = SectionKind::Tunnel;
};

struct SectionContext
{
    const FlightPath* path = nullptr;
    const TunnelSection* tunnelSec = nullptr;
    const Section* section = nullptr;
};

typedef void (*EffectFn)(LineEmitContext&,
    const SectionContext&,
    float t,
    void* user);

struct Effect
{
    EffectFn fn = nullptr;
    void* user = nullptr;
};

// Forward declaration for effects
struct Universe;

void effect_tunnel_geometry(LineEmitContext&, const SectionContext&, float, void*);
void effect_tunnel_surface(LineEmitContext&, const SectionContext&, float, void*);
void effect_energy(LineEmitContext&, const SectionContext&, float, void*);
void effect_geo(LineEmitContext&, const SectionContext&, float, void*);
void effect_ring_field(LineEmitContext&, const SectionContext&, float, void*);
void effect_world_box(LineEmitContext&, const SectionContext&, float, void*);
void effect_tunnel_text(LineEmitContext&, const SectionContext&, float, void*);

// -----------------------------------------------------------------------------
// Universe – world container
// -----------------------------------------------------------------------------
struct Universe
{
    FlightPath           path{};
    CameraRig            camera{};
    Tunnel               tunnel{};
    EnergyFlow           energy{};
    TunnelSurfacePainter surfacePainter{};
    GeoSet               geo{};
    TextOnWires          text{};   // text on wires

    vector<Section>      sections;

    vector<Effect>       tunnelEffects;
    vector<Effect>       emptyEffects;
    vector<Effect>       ringFieldEffects;
    vector<Effect>       worldEffects;  // always-on (e.g. world box)

    Universe()
    {
        // 1) Build global path inside 4km cube
        const int   nodeCount = 700;
        const float step = 40.0f;
        const float cubeHalf = 2000.0f;
        path.build_random_walk(nodeCount, step, cubeHalf);

        // 2) Tunnel section bound to the full path
        tunnel.section.segments = 6;
        tunnel.section.rings = 80;
        tunnel.section.radius = 40.0f;
        tunnel.section.bind_path(&path);

        // 3) Camera
        camera.inside_mode = true;
        camera.fly_speed = 40.0f;
        camera.fov_inside = 75.0f;
        camera.cam_back_offset = 20.0f;
        camera.look_ahead_dist = 80.0f;

        // 4) Energy + surface painter tweaks
        energy.pulse_count = 9;
        surfacePainter.tiles_u = 12;
        surfacePainter.tiles_v = 24;

        // 5) Geo objects outside tunnel
        geo.build_example(tunnel.section);

        // 6) Text labels on wires
        text.build_example(tunnel.section);

        // 7) Sections along the path
        float L = tunnel.section.total_length();
        if (L <= 0.0f)
        {
            Section s{};
            s.s_start = 0.0f;
            s.s_end = 1.0f;
            s.kind = SectionKind::Tunnel;
            sections.push_back(s);
        }
        else
        {
            float a = 0.0f;
            float b = 0.25f * L;
            float c = 0.45f * L;
            float d = 0.70f * L;
            float e = 0.90f * L;
            float f = L;

            Section s1{ a, b, SectionKind::Tunnel };
            Section s2{ b, c, SectionKind::Empty };
            Section s3{ c, d, SectionKind::Tunnel };
            Section s4{ d, e, SectionKind::RingField };
            Section s5{ e, f, SectionKind::Tunnel };

            sections.push_back(s1);
            sections.push_back(s2);
            sections.push_back(s3);
            sections.push_back(s4);
            sections.push_back(s5);
        }

        // 8) Register effects
        // Tunnel sections
        {
            Effect e1; e1.fn = effect_tunnel_geometry; e1.user = this;
            Effect e2; e2.fn = effect_tunnel_surface;  e2.user = this;
            Effect e3; e3.fn = effect_energy;          e3.user = this;
            Effect e4; e4.fn = effect_geo;             e4.user = this;
            Effect e5; e5.fn = effect_tunnel_text;     e5.user = this;

            tunnelEffects.push_back(e1);
            tunnelEffects.push_back(e2);
            tunnelEffects.push_back(e3);
            tunnelEffects.push_back(e4);
            tunnelEffects.push_back(e5);
        }

        // Empty sections – mainly external geo
        {
            Effect e; e.fn = effect_geo; e.user = this;
            emptyEffects.push_back(e);
        }

        // RingField sections – floating rings + geo
        {
            Effect e1; e1.fn = effect_ring_field; e1.user = this;
            Effect e2; e2.fn = effect_geo;        e2.user = this;

            ringFieldEffects.push_back(e1);
            ringFieldEffects.push_back(e2);
        }

        // World-wide effects (box)
        {
            Effect e; e.fn = effect_world_box; e.user = this;
            worldEffects.push_back(e);
        }
    }
};

// -----------------------------------------------------------------------------
// Camera callback – uses Universe + TunnelSection
// -----------------------------------------------------------------------------
void camera_callback(int frame, float t, CameraParams& cam)
{
    (void)frame;

    auto* uni = static_cast<Universe*>(cam.user_ptr);

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
        float totalLen = sec.total_length();
        if (totalLen <= 0.0f) totalLen = 1.0f;

        // Distance along the tunnel
        float sCenter = std::fmod(t * cr.fly_speed, totalLen);
        if (sCenter < 0.0f) sCenter += totalLen;

        // Build local frame at the camera's center position
        PathFrame frameLocal = make_path_frame(sec, sCenter);

        // Eye is slightly behind center along -forward (prevents "wall collisions")
        Vec3 eye = frameLocal.pos - frameLocal.forward * cr.cam_back_offset;

        // Look-ahead point along the path
        float sAhead = sCenter + cr.look_ahead_dist;
        if (sAhead > totalLen) sAhead = totalLen;
        Vec3 target = sec.center_along(sAhead);

        Vec3 up = frameLocal.up;

        cam.eye_x = eye.x;    cam.eye_y = eye.y;    cam.eye_z = eye.z;
        cam.target_x = target.x; cam.target_y = target.y; cam.target_z = target.z;
        cam.up_x = up.x;     cam.up_y = up.y;     cam.up_z = up.z;

        cam.has_custom_fov = true;
        cam.fov_y_deg = cr.fov_inside;
    }
    else
    {
        const float twoPi = 6.2831853f;

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
// Line callback – uses section-based effects
// -----------------------------------------------------------------------------
void line_push_callback(int frame, float t, LineEmitContext& ctx)
{
    (void)frame;

    auto* uni = static_cast<Universe*>(ctx.user_ptr);
    if (!uni) return;

    SectionContext baseCtx{};
    baseCtx.path = &uni->path;
    baseCtx.tunnelSec = &uni->tunnel.section;
    baseCtx.section = nullptr;

    // 1) World-wide effects (e.g. world box)
    for (const Effect& e : uni->worldEffects)
    {
        if (e.fn)
            e.fn(ctx, baseCtx, t, e.user);
    }

    // 2) Per-section effects along the path
    for (const Section& sec : uni->sections)
    {
        SectionContext sctx{};
        sctx.path = &uni->path;
        sctx.tunnelSec = &uni->tunnel.section;
        sctx.section = &sec;

        const vector<Effect>* list = nullptr;
        switch (sec.kind)
        {
        case SectionKind::Tunnel:
            list = &uni->tunnelEffects;
            break;
        case SectionKind::Empty:
            list = &uni->emptyEffects;
            break;
        case SectionKind::RingField:
            list = &uni->ringFieldEffects;
            break;
        }

        if (!list) continue;

        for (const Effect& e : *list)
        {
            if (e.fn)
                e.fn(ctx, sctx, t, e.user);
        }
    }

    ctx.flush_now();
}

// -----------------------------------------------------------------------------
// EFFECT IMPLEMENTATIONS
// -----------------------------------------------------------------------------
void effect_tunnel_geometry(LineEmitContext& ctx,
    const SectionContext& sctx,
    float t,
    void* user)
{
    Universe* uni = static_cast<Universe*>(user);
    if (!uni || !sctx.section) return;

    float s0 = sctx.section->s_start;
    float s1 = sctx.section->s_end;

    uni->tunnel.draw_range(ctx, t, s0, s1);
}

void effect_tunnel_surface(LineEmitContext& ctx,
    const SectionContext& sctx,
    float t,
    void* user)
{
    Universe* uni = static_cast<Universe*>(user);
    if (!uni || !sctx.section || !sctx.tunnelSec) return;

    float s0 = sctx.section->s_start;
    float s1 = sctx.section->s_end;

    uni->surfacePainter.draw_range(ctx, *sctx.tunnelSec, s0, s1, t);
}

void effect_energy(LineEmitContext& ctx,
    const SectionContext& sctx,
    float t,
    void* user)
{
    Universe* uni = static_cast<Universe*>(user);
    if (!uni || !sctx.section || !sctx.tunnelSec) return;

    float s0 = sctx.section->s_start;
    float s1 = sctx.section->s_end;

    uni->energy.draw_range(ctx, *sctx.tunnelSec, s0, s1, t);
}

void effect_geo(LineEmitContext& ctx,
    const SectionContext& sctx,
    float /*t*/,
    void* user)
{
    Universe* uni = static_cast<Universe*>(user);
    if (!uni || !sctx.section || !sctx.tunnelSec) return;

    float s0 = sctx.section->s_start;
    float s1 = sctx.section->s_end;

    uni->geo.draw_range(ctx, *sctx.tunnelSec, s0, s1);
}

void effect_ring_field(LineEmitContext& ctx,
    const SectionContext& sctx,
    float t,
    void* user)
{
    Universe* uni = static_cast<Universe*>(user);
    if (!uni || !sctx.section || !sctx.tunnelSec) return;

    const TunnelSection& sec = *sctx.tunnelSec;

    float s0 = sctx.section->s_start;
    float s1 = sctx.section->s_end;

    float L = sec.total_length();
    if (L <= 0.0f) return;

    float s_lo = (s0 < 0.0f) ? 0.0f : s0;
    float s_hi = (s1 > L) ? L : s1;
    if (s_hi <= s_lo) return;

    int         ringCount = 10;
    const float twoPi = 6.2831853f;

    for (int i = 0; i < ringCount; ++i)
    {
        float u = (float)i / (float)(ringCount - 1);
        float s = s_lo + u * (s_hi - s_lo);

        PathFrame frame = make_path_frame(sec, s);

        float bigR = sec.radius * (3.0f + 0.8f * std::sin(t * 0.4f + u * 4.0f));
        int   segments = 40;

        Vec3 baseCol = make_vec3(0.35f, 0.7f, 1.6f);
        float pulse = 0.6f + 0.4f * std::sin(t * 0.7f + u * 6.0f);
        Vec3 col = baseCol * (1.5f * pulse);

        float thick = 0.25f;
        float inten = 140.0f;

        Vec3 prev{};
        bool hasPrev = false;

        for (int k = 0; k <= segments; ++k)
        {
            float v = (float)k / (float)segments;
            float ang = twoPi * v;

            Vec3 offset =
                frame.right * (bigR * std::cos(ang)) +
                frame.up * (bigR * std::sin(ang));

            Vec3 p = frame.pos + offset;

            if (hasPrev)
            {
                emit_line(ctx, prev, p, col, thick, inten);
            }
            prev = p;
            hasPrev = true;
        }
    }
}

void effect_world_box(LineEmitContext& ctx,
    const SectionContext& sctx,
    float /*t*/,
    void* user)
{
    Universe* uni = static_cast<Universe*>(user);
    if (!uni || !sctx.path) return;

    float h = uni->path.box_half;
    if (h <= 0.0f) return;

    Vec3 c000 = make_vec3(-h, -h, -h);
    Vec3 c001 = make_vec3(-h, -h, h);
    Vec3 c010 = make_vec3(-h, h, -h);
    Vec3 c011 = make_vec3(-h, h, h);
    Vec3 c100 = make_vec3(h, -h, -h);
    Vec3 c101 = make_vec3(h, -h, h);
    Vec3 c110 = make_vec3(h, h, -h);
    Vec3 c111 = make_vec3(h, h, h);

    Vec3  col = make_vec3(0.18f, 0.26f, 0.5f);
    float thick = 0.10f;
    float inten = 50.0f;   // dimmer so tunnel & text dominate

    auto edge = [&](const Vec3& a, const Vec3& b)
        {
            emit_line(ctx, a, b, col, thick, inten);
        };

    // 12 edges
    edge(c000, c001); edge(c001, c011);
    edge(c011, c010); edge(c010, c000);

    edge(c100, c101); edge(c101, c111);
    edge(c111, c110); edge(c110, c100);

    edge(c000, c100); edge(c001, c101);
    edge(c010, c110); edge(c011, c111);

    // A few vertical "pillars" inside
    int pillarCount = 6;
    for (int i = 0; i < pillarCount; ++i)
    {
        float u = (float)i / (float)(pillarCount - 1);
        float x = -h + 2.0f * h * u;
        float z = (i % 2 == 0) ? -h * 0.6f : h * 0.6f;

        Vec3 p0 = make_vec3(x, -h, z);
        Vec3 p1 = make_vec3(x, h, z);

        emit_line(ctx, p0, p1, col * 0.8f, thick * 0.8f, inten * 0.7f);
    }
}

// Camera-facing text effect
void effect_tunnel_text(LineEmitContext& ctx,
    const SectionContext& sctx,
    float t,
    void* user)
{
    Universe* uni = static_cast<Universe*>(user);
    if (!uni || !sctx.section || !sctx.tunnelSec) return;

    TunnelSection& sec = uni->tunnel.section;
    CameraRig& cr = uni->camera;

    float totalLen = sec.total_length();
    if (totalLen <= 0.0f) totalLen = 1.0f;

    // Compute camera basis similar to camera_callback
    Vec3 camPos{};
    Vec3 camDir{};
    Vec3 camRight{};
    Vec3 camUp{};

    if (cr.inside_mode)
    {
        float sCenter = std::fmod(t * cr.fly_speed, totalLen);
        if (sCenter < 0.0f) sCenter += totalLen;

        PathFrame frameLocal = make_path_frame(sec, sCenter);

        camPos = frameLocal.pos - frameLocal.forward * cr.cam_back_offset;

        float sAhead = sCenter + cr.look_ahead_dist;
        if (sAhead > totalLen) sAhead = totalLen;
        Vec3 target = sec.center_along(sAhead);

        camDir = normalize3(target - camPos);
    }
    else
    {
        const float twoPi = 6.2831853f;

        float centerS = sec.total_length() * 0.5f;
        Vec3  center = sec.center_along(centerS);

        float angle = t * cr.orbit_speed * twoPi;

        float ox = std::cos(angle) * cr.orbit_radius;
        float oz = std::sin(angle) * cr.orbit_radius;

        camPos = make_vec3(center.x + ox,
            center.y + cr.orbit_height,
            center.z + oz);

        Vec3 target = center;
        camDir = normalize3(target - camPos);
    }

    // Build camera right/up from camDir
    {
        Vec3 worldUp = make_vec3(0.0f, 1.0f, 0.0f);
        camRight = cross3(camDir, worldUp);
        float rLen = length3(camRight);
        if (rLen < 1.0e-4f)
            camRight = make_vec3(1.0f, 0.0f, 0.0f);
        else
            camRight = camRight * (1.0f / rLen);

        camUp = normalize3(cross3(camRight, camDir));
    }

    float s0 = sctx.section->s_start;
    float s1 = sctx.section->s_end;

    uni->text.draw_range(ctx, *sctx.tunnelSec, s0, s1, t,
        camDir, camRight, camUp);
}

// -----------------------------------------------------------------------------
// Debug export: collect tunnel + text lines into a simple array
// -----------------------------------------------------------------------------

struct ExportLine
{
    Vec3 a;
    Vec3 b;
    Vec3 color;
};

// Simple float->u8 color conversion with a bit of exposure
inline std::uint8_t to_u8_color(float c, float exposure = 0.6f)
{
    float v = c * exposure * 255.0f;
    if (v < 0.0f)  v = 0.0f;
    if (v > 255.0f) v = 255.0f;
    return static_cast<std::uint8_t>(v);
}

// Collect static tunnel geometry (rings + bars + core) for one Tunnel section
void collect_tunnel_section_lines(const Universe& uni,
    const Section& secDef,
    std::vector<ExportLine>& out)
{
    if (secDef.kind != SectionKind::Tunnel) return;

    const TunnelSection& sec = uni.tunnel.section;
    float L = sec.total_length();
    if (L <= 0.0f) return;

    const int rings = sec.rings;
    const int segments = sec.segments;
    if (rings < 2 || segments < 3) return;

    float s_start = secDef.s_start;
    float s_end = secDef.s_end;

    float s_lo = (s_start < 0.0f) ? 0.0f : s_start;
    float s_hi = (s_end > L) ? L : s_end;
    if (s_hi <= s_lo) return;

    float invLenLocal = 1.0f / (s_hi - s_lo);

    // 1) Ring frames
    for (int r = 0; r < rings; ++r)
    {
        float s = sec.s_for_ring(r);
        if (s < s_lo || s > s_hi) continue;

        float localFrac = (s - s_lo) * invLenLocal; // 0..1
        float fade = 0.4f + 0.6f * (1.0f - localFrac);
        Vec3 col = uni.tunnel.frameColor * fade;

        for (int sIdx = 0; sIdx < segments; ++sIdx)
        {
            int sn = (sIdx + 1) % segments;

            Vec3 a = sec.ring_vertex(r, sIdx);
            Vec3 b = sec.ring_vertex(r, sn);

            out.push_back(ExportLine{ a, b, col });
        }
    }

    // 2) Longitudinal bars
    for (int r = 0; r < rings - 1; ++r)
    {
        float s0 = sec.s_for_ring(r);
        float s1 = sec.s_for_ring(r + 1);

        if ((s0 < s_lo && s1 < s_lo) ||
            (s0 > s_hi && s1 > s_hi))
            continue;

        float sMid = 0.5f * (s0 + s1);
        float localFrac = (sMid - s_lo) * invLenLocal;
        if (localFrac < 0.0f) localFrac = 0.0f;
        if (localFrac > 1.0f) localFrac = 1.0f;

        float fade = 0.5f + 0.5f * (1.0f - localFrac);
        Vec3 col = uni.tunnel.barColor * fade;

        for (int sIdx = 0; sIdx < segments; ++sIdx)
        {
            Vec3 a = sec.ring_vertex(r, sIdx);
            Vec3 b = sec.ring_vertex(r + 1, sIdx);

            out.push_back(ExportLine{ a, b, col });
        }
    }

    // 3) Core line (static, no time-based pulse)
    if (uni.tunnel.draw_core)
    {
        int coreSegs = sec.rings * 3;
        for (int i = 0; i < coreSegs - 1; ++i)
        {
            float u0 = (float)i / (float)(coreSegs - 1);
            float u1 = (float)(i + 1) / (float)(coreSegs - 1);

            float s0 = u0 * L;
            float s1 = u1 * L;

            if ((s0 < s_lo && s1 < s_lo) ||
                (s0 > s_hi && s1 > s_hi))
                continue;

            if (s0 < s_lo) s0 = s_lo;
            if (s1 > s_hi) s1 = s_hi;

            Vec3 c0 = sec.center_along(s0);
            Vec3 c1 = sec.center_along(s1);

            Vec3 col = uni.tunnel.coreColor; // no animation, just base color
            out.push_back(ExportLine{ c0, c1, col });
        }
    }
}

// Collect all text labels as line segments (no flicker, static colors)
void collect_text_lines(const Universe& uni,
    std::vector<ExportLine>& out)
{
    const TunnelSection& sec = uni.tunnel.section;
    const TextOnWires& text = uni.text;

    float L = sec.total_length();
    if (L <= 0.0f) return;
    if (text.labels.empty()) return;

    // Must match the constants used in TextOnWires::draw_range
    const float cellBase = 1.5f;
    const float glyphWCells = 3.0f;
    const float glyphHCells = 5.0f;
    const float gapCells = 1.0f;
    const float advanceCells = glyphWCells + gapCells;

    for (const TextLabel& lab : text.labels)
    {
        if (lab.text.empty()) continue;

        PathFrame frame = make_path_frame(sec, lab.s);

        Vec3 base =
            frame.pos +
            frame.right * lab.offset.x +
            frame.up * lab.offset.y +
            frame.forward * lab.offset.z;

        int   n = (int)lab.text.size();
        float totalWidthCells = (float)n * advanceCells - gapCells;
        float totalHeightCells = glyphHCells;

        float halfW = 0.5f * totalWidthCells * cellBase * lab.size;
        float halfH = 0.5f * totalHeightCells * cellBase * lab.size;

        // Center text around base in the (right, up) plane
        Vec3 origin = base - frame.right * halfW - frame.up * halfH;

        for (int idx = 0; idx < n; ++idx)
        {
            char c = lab.text[(size_t)idx];
            if (c == ' ') continue;

            const FontGlyph* glyph = get_font_glyph(c);
            if (!glyph) continue;

            float charOffsetCells = (float)idx * advanceCells;

            for (int row = 0; row < 5; ++row)
            {
                for (int col = 0; col < 3; ++col)
                {
                    char pixel = glyph->rows[row][col];
                    if (pixel != '#') continue;

                    float x0 = (charOffsetCells + (float)col) * cellBase * lab.size;
                    float x1 = (charOffsetCells + (float)col + 1.0f) * cellBase * lab.size;
                    float y0 = ((float)row) * cellBase * lab.size;
                    float y1 = ((float)row + 1.0f) * cellBase * lab.size;

                    Vec3 p00 = origin + frame.right * x0 + frame.up * y0;
                    Vec3 p10 = origin + frame.right * x1 + frame.up * y0;
                    Vec3 p11 = origin + frame.right * x1 + frame.up * y1;
                    Vec3 p01 = origin + frame.right * x0 + frame.up * y1;

                    Vec3 colr = lab.color; // no time-based flicker

                    out.push_back(ExportLine{ p00, p10, colr });
                    out.push_back(ExportLine{ p10, p11, colr });
                    out.push_back(ExportLine{ p11, p01, colr });
                    out.push_back(ExportLine{ p01, p00, colr });
                }
            }
        }
    }
}

// Aggregate: tunnel geometry for all Tunnel sections + text labels
void collect_all_tunnel_debug_lines(const Universe& uni,
    std::vector<ExportLine>& out)
{
    out.clear();

    // 1) Tunnel geometry per Tunnel section
    for (const Section& s : uni.sections)
    {
        if (s.kind == SectionKind::Tunnel)
        {
            collect_tunnel_section_lines(uni, s, out);
        }
    }

    // 2) Text labels along the path
    collect_text_lines(uni, out);

    std::cout << "Debug collect: " << out.size()
        << " line segments for tunnel + text\n";
}

// -----------------------------------------------------------------------------
// Export collected lines as a PLY (vertices + edges)
// -----------------------------------------------------------------------------
void export_tunnel_debug_ply(const Universe& uni,
    const std::string& baseName)
{
    std::vector<ExportLine> lines;
    collect_all_tunnel_debug_lines(uni, lines);

    if (lines.empty())
    {
        std::cout << "No tunnel lines to export for PLY.\n";
        return;
    }

    std::vector<float>    vertices; // x,y,z
    std::vector<std::uint8_t> colors;   // r,g,b
    std::vector<std::uint32_t> edges;   // vertex indices, [v0,v1, v2,v3, ...]

    vertices.reserve(lines.size() * 2 * 3);
    colors.reserve(lines.size() * 2 * 3);
    edges.reserve(lines.size() * 2);

    std::uint32_t currentVertexIndex = 0;

    for (const ExportLine& l : lines)
    {
        // Map color to [0,255], reused for both endpoints
        std::uint8_t r = to_u8_color(l.color.x);
        std::uint8_t g = to_u8_color(l.color.y);
        std::uint8_t b = to_u8_color(l.color.z);

        // Vertex 0
        vertices.push_back(l.a.x);
        vertices.push_back(l.a.y);
        vertices.push_back(l.a.z);
        colors.push_back(r);
        colors.push_back(g);
        colors.push_back(b);
        std::uint32_t idx0 = currentVertexIndex++;

        // Vertex 1
        vertices.push_back(l.b.x);
        vertices.push_back(l.b.y);
        vertices.push_back(l.b.z);
        colors.push_back(r);
        colors.push_back(g);
        colors.push_back(b);
        std::uint32_t idx1 = currentVertexIndex++;

        // Edge uses the two vertex indices
        edges.push_back(idx0);
        edges.push_back(idx1);
    }

    const std::string plyPath =
        g_base_output_filepath + "/" + baseName + "_tunnel_debug.ply";

    try
    {
        std::filebuf fb;
        if (!fb.open(plyPath, std::ios::out | std::ios::binary))
        {
            std::cerr << "Failed to open PLY file for writing: "
                << plyPath << "\n";
            return;
        }

        std::ostream outstream(&fb);

        tinyply::PlyFile ply;

        const size_t vertexCount = vertices.size() / 3;
        const size_t edgeCount = edges.size() / 2;

        // Vertex positions
        ply.add_properties_to_element("vertex",
            { "x", "y", "z" },
            tinyply::Type::FLOAT32,
            vertexCount,
            reinterpret_cast<std::uint8_t*>(vertices.data()),
            tinyply::Type::INVALID, 0);

        // Vertex colors
        ply.add_properties_to_element("vertex",
            { "red", "green", "blue" },
            tinyply::Type::UINT8,
            vertexCount,
            reinterpret_cast<std::uint8_t*>(colors.data()),
            tinyply::Type::INVALID, 0);

        // Edges: each element has properties vertex1, vertex2 (INT32)
        ply.add_properties_to_element("edge",
            { "vertex1", "vertex2" },
            tinyply::Type::INT32,
            edgeCount,
            reinterpret_cast<std::uint8_t*>(edges.data()),
            tinyply::Type::INVALID, 0);

        ply.get_comments().push_back("WireEngine tunnel + text debug export");

        ply.write(outstream, true);

        std::cout << "Wrote tunnel debug PLY: " << plyPath
            << "\n  vertices: " << vertexCount
            << "\n  edges:    " << edgeCount << "\n";
    }
    catch (const std::exception& e)
    {
        std::cerr << "Exception while writing PLY: " << e.what() << "\n";
    }
}


int main()
{
    std::cout << "example_tunnel_world_sections_text\n";
    std::cout << "This code is in file: " << __FILE__ << "\n";

    const std::string uniqueName = WIRE_UNIQUE_NAME(g_base_output_filepath);
    std::cout << "Video name: " << uniqueName << "\n";
    std::cout << "Output path: " << g_base_output_filepath
        << "/" << uniqueName << ".mp4\n";

    RenderSettings settings = init_render_settings(uniqueName, 1);

    Universe universe{};

    // <<< NEW: export static tunnel + text geometry for Blender debug
    export_tunnel_debug_ply(universe, uniqueName);
    // >>>

    renderSequencePush(
        settings,
        camera_callback,
        line_push_callback,
        &universe
    );

    VLC::play(g_base_output_filepath + "/" + uniqueName + ".mp4");
    return 0;
}
