#pragma once

#include "WireEngine_v5.h"

#include <cmath>
#include <iostream>
#include <string>
#include <cstdlib>
#include <random>
#include <ctime>
#include <filesystem>
#include <sstream>

#include "../External_libs/My/VLC/VLC.h"

// Where all videos are written (used by generate_unique_name)
const std::string g_base_output_filepath = "C:/Users/Cosmos/Desktop/output/tmp";

// -----------------------------------------------------------------------------
// Random helpers (your version)
// -----------------------------------------------------------------------------
namespace Random
{
    inline std::mt19937& get_engine()
    {
        static std::mt19937 engine{ std::random_device{}() };
        return engine;
    }

    inline float random_01()
    {
        static std::uniform_real_distribution<float> dist(0.0f, 1.0f);
        return dist(get_engine());
    }

    inline float random_signed()
    {
        static std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        return dist(get_engine());
    }
}

// -----------------------------------------------------------------------------
// Unique name generator (your code, slightly formatted)
// -----------------------------------------------------------------------------
namespace Utils
{
    inline std::string generate_unique_name()
    {
        namespace fs = std::filesystem;

        // 1) Base name from this source file path (__FILE__)
        std::string fullPath = __FILE__;
        std::string filename = fullPath;

        // Strip directory
        std::size_t pos = filename.find_last_of("/\\");
        if (pos != std::string::npos)
            filename = filename.substr(pos + 1);

        // Strip extension
        std::size_t dotPos = filename.find_last_of('.');
        if (dotPos != std::string::npos)
            filename = filename.substr(0, dotPos);

        const std::string baseName = filename;

        // 2) Where videos live
        fs::path outDir = g_base_output_filepath;

        // Ensure directory exists (safe to call even if it already exists)
        if (!fs::exists(outDir))
        {
            std::error_code ec;
            fs::create_directories(outDir, ec);
            if (ec)
            {
                std::cerr << "generate_unique_name: failed to create output dir: "
                    << outDir.string() << " (" << ec.message() << ")\n";
            }
        }

        // 3) Find first free "<baseName>_V_<n>.mp4"
        int version = 1;
        while (true)
        {
            std::ostringstream nameStream;
            nameStream << baseName << "_V_" << version;
            const std::string candidateName = nameStream.str();

            fs::path candidatePath = outDir / (candidateName + ".mp4");

            if (!fs::exists(candidatePath))
            {
                // We return just the name (without path or extension)
                return candidateName;
            }

            ++version;
        }
    }
}

using namespace WireEngine;

// -----------------------------------------------------------------------------
// Simple Vec3 helper for math (avoids bringing in glm here)
// -----------------------------------------------------------------------------
struct Vec3
{
    float x, y, z;
};

inline Vec3 make_vec3(float x, float y, float z)
{
    return Vec3{ x, y, z };
}

inline Vec3 operator+(const Vec3& a, const Vec3& b)
{
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}

inline Vec3 operator-(const Vec3& a, const Vec3& b)
{
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}

inline Vec3 operator*(const Vec3& a, float s)
{
    return { a.x * s, a.y * s, a.z * s };
}

inline Vec3 operator*(float s, const Vec3& a)
{
    return a * s;
}

inline Vec3 operator/(const Vec3& a, float s)
{
    return { a.x / s, a.y / s, a.z / s };
}

inline float dot3(const Vec3& a, const Vec3& b)
{
    return a.x * b.x + a.y * b.y + a.z * b.z;
}

inline Vec3 cross3(const Vec3& a, const Vec3& b)
{
    return {
        a.y * b.z - a.z * b.y,
        a.z * b.x - a.x * b.z,
        a.x * b.y - a.y * b.x
    };
}

inline float length3(const Vec3& v)
{
    return std::sqrt(dot3(v, v));
}

inline Vec3 normalize3(const Vec3& v)
{
    float len = length3(v);
    if (len <= 1e-6f) return { 0.0f, 0.0f, 0.0f };
    return v / len;
}


// -----------------------------------------------------------------------------
// Render settings  uses unique video name
// -----------------------------------------------------------------------------
RenderSettings init_render_settings(const std::string& baseName,
    int seconds = 4)
{
    RenderSettings settings;

    // Resolution
    settings.width = 1920 * 2;
    settings.height = 1080 * 2;

    // frames / fps
    settings.frames = 60 * seconds;
    settings.fps = 60.0f;

    // Light-painting feel
    settings.accum_passes = 64; // enough jitter, still fast-ish

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

    // Capacity hint (bands * segments + sparkles, see SceneParams below)
    settings.max_line_segments_hint = 1000 * 1000 * 4;

    // Readback & IO
    settings.use_pbo = true;
    settings.output_dir = "frames_lissajous_push"; // only used in PNG mode

    // Output: unique video name
    settings.output_mode = OutputMode::FFmpegVideo;
    settings.ffmpeg_path = "ffmpeg";
    settings.ffmpeg_output = g_base_output_filepath + "/" + baseName + ".mp4";
    settings.ffmpeg_extra_args = "-c:v libx264 -preset veryfast -crf 18";

    return settings;
}

// -----------------------------------------------------------------------------
// Scene parameters  (shared state for camera + lines)
// -----------------------------------------------------------------------------
struct SceneParams
{
    int bands = 48;            // how many colorful ribbons (coarse)
    int segmentsPerBand = 32;  // detail per ribbon (coarse)
    int sparkleCount = 1500;   // small flickering halo lines

    // Camera base values
    float camera_base_radius = 220.0f;
    float camera_radius_breath = 40.0f;

    float camera_base_height = 20.0f;
    float camera_height_breath = 15.0f;

    float camera_base_fov = 55.0f;
    float camera_fov_breath = 15.0f;

    // Per-frame offsets written by camera_callback, read by line_push_callback
    float camera_radius_offset = 0.0f;
    float camera_height_offset = 0.0f;
    float camera_fov_offset = 0.0f;

    // Generic phase you can reuse in multiple places
    float shared_phase = 0.0f;

    // NEW: camera basis & position (computed in camera_callback)
    Vec3 cam_eye{ 0.0f, 0.0f, 450.0f };
    Vec3 cam_target{ 0.0f, 0.0f,   0.0f };
    Vec3 cam_forward{ 0.0f, 0.0f,  -1.0f };
    Vec3 cam_right{ 1.0f, 0.0f,   0.0f };
    Vec3 cam_up_vec{ 0.0f, 1.0f,   0.0f };
};


// -----------------------------------------------------------------------------
// Camera callback  orbit + breathing driven via SceneParams
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

    // Shared phase  lines can reuse this so shapes + camera feel coherent
    scene->shared_phase = t * 0.6f;

    float radius = scene->camera_base_radius + scene->camera_radius_offset;
    float height = scene->camera_base_height + scene->camera_height_offset;

    const float orbitSpeed = 0.2f;
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

    // --- NEW: write camera basis into SceneParams for the push callback ---

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
// Push-style line callback  **this is the main show**
// -----------------------------------------------------------------------------
void line_push_callback(int frame, float t, LineEmitContext& ctx)
{
    (void)frame;

    auto* scene = static_cast<SceneParams*>(ctx.user_ptr);
    if (!scene) return;

    const float twoPi = 6.2831853f;

    // Camera  lines coupling: use camera breathing as a normalized 0..1 signal.
    float breathNorm = 0.0f;
    if (scene->camera_radius_breath != 0.0f)
    {
        breathNorm = scene->camera_radius_offset / scene->camera_radius_breath; // ~[-1,1]
    }
    breathNorm = 0.5f + 0.5f * breathNorm; // -> [0,1]

    float phase = scene->shared_phase;

    // Resolution: ~2M segments from two smooth tori
    const int baseMajor = (scene->bands > 0) ? scene->bands : 1;
    const int baseTube = (scene->segmentsPerBand > 0) ? scene->segmentsPerBand : 1;

    const int majorSegs = baseMajor * 16;    // 48 * 16 = 768
    const int tubeSegs = baseTube * 20;    // 32 * 20 = 640
    const int haloCount = scene->sparkleCount;

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
        // Bring floor a bit closer and make it chunky so it actually reads.
        const float floorY = -60.0f;  // a bit below torus center
        const float halfSize = 280.0f;
        const float step = 28.0f;
        const int   linesEach = static_cast<int>(halfSize / step);

        // Subtle bluish grid, but brighter & thicker than before.
        Vec3 baseGridCol = { 0.55f, 0.62f, 0.78f };
        float gridIntensity = 130.0f * 32.0f;
        float gridThickness = 0.012f;      // MUCH thicker -> less aliasing  less flicker

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
                lp.jitter = 0.0f;         // no jitter  no temporal shimmer
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

        // Vertical axis through origin (world up), so you see where "up" is.
        {
            LineParams lp{};
            lp.start_x = 0.0f;
            lp.start_y = floorY;
            lp.start_z = 0.0f;
            lp.end_x = 0.0f;
            lp.end_y = floorY + 260.0f;
            lp.end_z = 0.0f;

            Vec3 axisCol = hueToRGB(0.58f); // soft cyan
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

    ctx.flush_now(); // semantic boundary: "floor/axis layer done"

    // =========================================================================
    // 1) Two nested tori: longitudinal + meridional segments (continuous)
    // =========================================================================
    struct TorusLayer
    {
        float id;               // 0 or 1 for phase offsets
        float majorR, minorR;   // base radii
        float majorWaveAmp;     // modulation of big radius
        float minorWaveAmp;     // modulation of tube radius
        float hueOffset;        // base hue shift
        float thicknessBase;
        float thicknessVar;
        float intensityBase;
        float intensityVar;
        float jitterBase;
        float jitterVar;
        float brightnessScale;  // overall per-layer brightness
    };

    const TorusLayer layers[2] = {
        // Outer torus: big, proud, slightly thicker, bright.
        {
            0.0f,
            100.0f, 26.0f,       // majorR, minorR
            0.045f, 0.22f,       // majorWaveAmp, minorWaveAmp
            0.00f,               // hueOffset
            0.010f, 0.004f,      // thickness base / var
            110.0f, 55.0f,       // slightly toned down vs before
            0.14f, 0.08f,        // jitter base / var
            3.6f                 // brightnessScale
        },
        // Inner torus: slightly smaller and more intricate.
        {
            1.0f,
            70.0f,  18.0f,       // majorR, minorR
            0.065f, 0.28f,       // majorWaveAmp, minorWaveAmp
            0.30f,               // hueOffset
            0.007f, 0.003f,      // thickness base / var
            85.0f,  38.0f,       // intensity base / var
            0.12f, 0.06f,        // jitter base / var
            3.0f                 // brightnessScale
        }
    };

    // Time scales for waves  gently modulated by camera breathing / shared phase
    float tSlow = t * (0.25f + 0.25f * breathNorm);
    float tWave = t * 0.7f + phase * 0.45f;

    // Smooth torus position with gentle standing waves
    auto torus_pos = [&](const TorusLayer& L, float u, float v) -> Vec3
        {
            float cu = std::cos(u);
            float su = std::sin(u);
            float cv = std::cos(v);
            float sv = std::sin(v);

            float waveU = std::sin(3.0f * u + 1.2f * tWave + 0.7f * L.id);
            float waveV = std::sin(2.0f * v + 1.5f * tWave - 0.9f * L.id);
            float cross = std::sin(4.0f * u + 3.0f * v + 1.1f * tSlow);

            float breathCentered = (breathNorm - 0.5f); // [-0.5, 0.5]

            float majorMod = 1.0f + L.majorWaveAmp *
                (0.7f * waveU + 0.2f * cross + 0.15f * breathCentered);
            float minorMod = 1.0f + L.minorWaveAmp *
                (0.6f * waveV + 0.25f * cross);

            float R = L.majorR * majorMod;
            float r = L.minorR * minorMod;

            float x = (R + r * cv) * cu;
            float y = r * sv;
            float z = (R + r * cv) * su;
            return { x, y, z };
        };

    for (int layerIdx = 0; layerIdx < 2; ++layerIdx)
    {
        const TorusLayer& L = layers[layerIdx];

        for (int iu = 0; iu < majorSegs; ++iu)
        {
            float u0 = twoPi * (float)iu / (float)majorSegs;
            float u1 = twoPi * (float)(iu + 1) / (float)majorSegs;
            float uFrac = (float)iu / (float)majorSegs;

            for (int iv = 0; iv < tubeSegs; ++iv)
            {
                float v0 = twoPi * (float)iv / (float)tubeSegs;
                float v1 = twoPi * (float)(iv + 1) / (float)tubeSegs;
                float vFrac = (float)iv / (float)tubeSegs;

                // Smooth, always-positive brightness; no hard "top vs bottom".
                float stripeU = 0.5f + 0.5f * std::sin(5.0f * u0 + 0.8f * tWave + L.id * 0.7f);
                float stripeV = 0.7f + 0.3f * std::sin(2.0f * v0 - 0.6f * tSlow + L.id * 1.3f);

                float brightnessBase = 0.40f + 0.45f * stripeU; // [~0.4, ~0.85]
                float brightnessV = 0.65f + 0.35f * stripeV; // [~0.65, ~1.0]
                float brightness = brightnessBase * brightnessV;
                brightness *= L.brightnessScale * (0.75f + 0.25f * breathNorm);

                // Color varies mainly with u (around donut), plus slight v modulation
                float hue = uFrac + L.hueOffset
                    + 0.05f * std::sin(2.0f * v0 + t * 0.6f)
                    + 0.08f * (breathNorm - 0.5f);
                Vec3 col = hueToRGB(hue);

                // -----------------------------
                // A) Longitudinal segment (around major ring, u direction)
                // -----------------------------
                {
                    Vec3 p0 = torus_pos(L, u0, v0);
                    Vec3 p1 = torus_pos(L, u1, v0);

                    LineParams lp{};
                    lp.start_x = p0.x; lp.start_y = p0.y; lp.start_z = p0.z;
                    lp.end_x = p1.x; lp.end_y = p1.y; lp.end_z = p1.z;

                    lp.start_r = brightness * col.x;
                    lp.start_g = brightness * col.y;
                    lp.start_b = brightness * col.z;
                    lp.end_r = lp.start_r;
                    lp.end_g = lp.start_g;
                    lp.end_b = lp.start_b;

                    float thickWave = 0.5f + 0.5f * stripeU;
                    lp.thickness = L.thicknessBase + L.thicknessVar * thickWave;

                    lp.jitter = L.jitterBase +
                        L.jitterVar * (0.4f * stripeV + 0.6f * stripeU);

                    lp.intensity = (L.intensityBase +
                        L.intensityVar * stripeU) * (0.8f + 0.2f * breathNorm);

                    ctx.add(lp);
                }

                // -----------------------------
                // B) Meridional segment (around tube, v direction)
                // -----------------------------
                {
                    Vec3 p0 = torus_pos(L, u0, v0);
                    Vec3 p1 = torus_pos(L, u0, v1);

                    LineParams lp{};
                    lp.start_x = p0.x; lp.start_y = p0.y; lp.start_z = p0.z;
                    lp.end_x = p1.x; lp.end_y = p1.y; lp.end_z = p1.z;

                    float hue2 = uFrac + L.hueOffset + 0.10f * vFrac
                        + 0.06f * std::sin(3.0f * u0 + t * 0.4f + L.id);
                    Vec3 col2 = hueToRGB(hue2);

                    float brightness2 =
                        (0.30f + 0.55f * stripeV) *
                        L.brightnessScale * 0.7f *
                        (0.85f + 0.3f * breathNorm);

                    lp.start_r = brightness2 * col2.x;
                    lp.start_g = brightness2 * col2.y;
                    lp.start_b = brightness2 * col2.z;
                    lp.end_r = lp.start_r;
                    lp.end_g = lp.start_g;
                    lp.end_b = lp.start_b;

                    float thickWave2 = 0.5f + 0.5f * stripeV;
                    lp.thickness = (L.thicknessBase * 0.7f)
                        + (L.thicknessVar * 0.6f) * thickWave2;

                    lp.jitter = L.jitterBase * 0.8f +
                        L.jitterVar * 0.5f * stripeU;

                    lp.intensity = (L.intensityBase * 0.7f) +
                        (L.intensityVar * 0.8f) * stripeV;

                    ctx.add(lp);
                }
            }
        }
    }

    ctx.flush_now(); // semantic boundary: "torus layer done"

    // =========================================================================
    // 2) Halo: radial lines emanating from the outer torus, softly pulsing.
    // =========================================================================
    const TorusLayer& haloLayer = layers[0];

    for (int i = 0; i < haloCount; ++i)
    {
        float u = twoPi * Random::random_01();
        float v = twoPi * Random::random_01();

        Vec3 p = torus_pos(haloLayer, u, v);

        float len2 = dot3(p, p);
        if (len2 <= 1e-4f) continue;

        float invLen = 1.0f / std::sqrt(len2);
        Vec3 dir{ p.x * invLen, p.y * invLen, p.z * invLen };

        float stretch = 1.10f + 0.25f * Random::random_01();

        LineParams lp{};
        lp.start_x = p.x;
        lp.start_y = p.y;
        lp.start_z = p.z;
        lp.end_x = p.x + dir.x * haloLayer.minorR * (stretch - 1.0f);
        lp.end_y = p.y + dir.y * haloLayer.minorR * (stretch - 1.0f);
        lp.end_z = p.z + dir.z * haloLayer.minorR * (stretch - 1.0f);

        float spark = Random::random_01();
        float pulse = std::sin(t * 2.7f + spark * twoPi) * 0.5f + 0.5f;

        float base = 0.7f + 0.3f * spark;
        float bright = (0.4f + 1.0f * pulse * pulse)
            * (0.85f + 0.3f * breathNorm);

        lp.start_r = bright * base;
        lp.start_g = bright * base;
        lp.start_b = bright;
        lp.end_r = lp.start_r;
        lp.end_g = lp.start_g;
        lp.end_b = lp.start_b;

        lp.thickness = 0.0045f;
        lp.jitter = 0.23f + 0.12f * spark;
        lp.intensity = 145.0f * (0.4f + 0.6f * pulse);

        ctx.add(lp);
    }

    ctx.flush_now(); // "halo layer done"


    // =========================================================================
// 3) Billboard sculpture in the center that always faces the camera.
//    Now calmer, crisp, low-jitter pattern.
// =========================================================================
    {
        Vec3 center = make_vec3(0.0f, 0.0f, 0.0f);
        center.y += 25;

        // From center to camera (so the plane faces the camera)
        Vec3 toCam = scene->cam_eye - center;
        if (length3(toCam) < 1e-5f) toCam = make_vec3(0.0f, 0.0f, 1.0f);
        Vec3 forward = normalize3(toCam);

        // Use scene->cam_up_vec for a stable vertical, derive right & up
        Vec3 right = normalize3(cross3(scene->cam_up_vec, forward));
        if (length3(right) < 1e-5f) right = make_vec3(1.0f, 0.0f, 0.0f);
        Vec3 up = normalize3(cross3(forward, right));

        float baseSize = 55.0f;
        float pulse = 0.5f + 0.5f * std::sin(t * 1.2f + phase * 0.9f);
        float halfW = baseSize * (0.6f + 0.30f * pulse);
        float halfH = baseSize * (0.35f + 0.20f * breathNorm);

        Vec3 rightScaled = right * halfW;
        Vec3 upScaled = up * halfH;

        Vec3 pTL = center - rightScaled + upScaled;
        Vec3 pTR = center + rightScaled + upScaled;
        Vec3 pBR = center + rightScaled - upScaled;
        Vec3 pBL = center - rightScaled - upScaled;

        float billboardHue = 0.12f + 0.05f * std::sin(t * 0.8f + phase);
        Vec3 edgeCol = hueToRGB(billboardHue);
        float brightBill = 2.2f * (0.6f + 0.4f * pulse) * (0.7f + 0.3f * breathNorm);

        auto add_edge = [&](const Vec3& a, const Vec3& b)
            {
                LineParams lp{};
                lp.start_x = a.x; lp.start_y = a.y; lp.start_z = a.z;
                lp.end_x = b.x; lp.end_y = b.y; lp.end_z = b.z;

                lp.start_r = brightBill * edgeCol.x;
                lp.start_g = brightBill * edgeCol.y;
                lp.start_b = brightBill * edgeCol.z;
                lp.end_r = lp.start_r;
                lp.end_g = lp.start_g;
                lp.end_b = lp.start_b;

                // Edges: crisp, no jitter
                lp.thickness = 0.011f * 6.0f;
                lp.jitter = 0.0f;
                lp.intensity = 120.0f * 2.0f;

                ctx.add(lp);
            };

        // Rectangle outline
        add_edge(pTL, pTR);
        add_edge(pTR, pBR);
        add_edge(pBR, pBL);
        add_edge(pBL, pTL);

        // ---------------------------------------------------------------------
        // Scanlines: subtle horizontal lines inside the rectangle.
        // ---------------------------------------------------------------------
        {
            int scanLines = 32;
            for (int i = 0; i < scanLines; ++i)
            {
                float vFrac = (scanLines > 1) ? (float)i / (float)(scanLines - 1) : 0.0f;
                float k = 1.0f - 2.0f * vFrac; // 1..-1 from top to bottom
                Vec3 rowOffset = upScaled * k;

                Vec3 a = center - rightScaled + rowOffset;
                Vec3 b = center + rightScaled + rowOffset;

                float lineHue = billboardHue + 0.015f * (vFrac - 0.5f);
                Vec3 lineCol = hueToRGB(lineHue);

                // Slow, gentle modulation to avoid strobing
                float lineBright = brightBill * (0.5f + 0.4f * std::sin(t * 0.9f + vFrac * twoPi));



                LineParams lp{};
                lp.start_x = a.x; lp.start_y = a.y; lp.start_z = a.z;
                lp.end_x = b.x; lp.end_y = b.y; lp.end_z = b.z;

                lp.start_r = lineBright * lineCol.x;
                lp.start_g = lineBright * lineCol.y;
                lp.start_b = lineBright * lineCol.z;
                lp.end_r = lp.start_r * 0.8f;
                lp.end_g = lp.start_g * 0.8f;
                lp.end_b = lp.start_b * 0.8f;

                lp.thickness = 0.006f;
                lp.jitter = 0.0f;       // keep these razor-clean
                lp.intensity = 100.0f;

                ctx.add(lp);
            }
        }

        // ---------------------------------------------------------------------
        // Animated Lissajous-style figure on the billboard plane, but calmer:
        //  - fewer segments
        //  - lower intensity
        //  - almost no jitter
        // ---------------------------------------------------------------------
        {
            int   curveSegs = 260;
            float aFreq = 2.0f;   // lower frequencies = smoother motion
            float bFreq = 3.0f;

            // Slowly evolving phases  continuous, not strobey
            float phaseA = 0.7f * phase + 0.3f * t;
            float phaseB = 0.4f * phase + 0.9f * t;

            for (int i = 0; i < curveSegs; ++i)
            {
                float s0 = (float)i / (float)curveSegs;
                float s1 = (float)(i + 1) / (float)curveSegs;

                float u0 = std::sin(aFreq * twoPi * s0 + phaseA);
                float v0 = std::sin(bFreq * twoPi * s0 + phaseB);
                float u1 = std::sin(aFreq * twoPi * s1 + phaseA);
                float v1 = std::sin(bFreq * twoPi * s1 + phaseB);

                // Scale into inner area of the billboard
                float radX = 0.80f;
                float radY = 0.55f;
                u0 *= radX; v0 *= radY;
                u1 *= radX; v1 *= radY;

                Vec3 P0 = center + rightScaled * u0 + upScaled * v0;
                Vec3 P1 = center + rightScaled * u1 + upScaled * v1;

                float hueCurve = billboardHue + 0.16f * (s0 - 0.5f);
                Vec3 curveCol = hueToRGB(hueCurve);

                // Gentle glow modulation along the curve
                float glow = 0.9f + 0.6f * std::sin(twoPi * s0 + t * 0.6f + phase * 0.5f);

                LineParams lp{};
                lp.start_x = P0.x; lp.start_y = P0.y; lp.start_z = P0.z;
                lp.end_x = P1.x; lp.end_y = P1.y; lp.end_z = P1.z;

                lp.start_r = glow * curveCol.x;
                lp.start_g = glow * curveCol.y;
                lp.start_b = glow * curveCol.z;
                lp.end_r = lp.start_r * 0.9f;
                lp.end_g = lp.start_g * 0.9f;
                lp.end_b = lp.start_b * 0.9f;

                // KEY: almost no jitter, moderate intensity  much less flicker
                lp.thickness = 0.010f;
                lp.jitter = 0.002f;
                lp.intensity = 95.0f * 10.0f;

                ctx.add(lp);
            }
        }
    }

}




// -----------------------------------------------------------------------------
// Entry
// -----------------------------------------------------------------------------
int main()
{
    std::cout << "example_lissajous_push\n";
    std::cout << "This code is in file: " << __FILE__ << "\n";

    // Unique name based on file + first free version
    const std::string uniqueName = Utils::generate_unique_name();
    std::cout << "Video name: " << uniqueName << "\n";
    std::cout << "Output path: " << g_base_output_filepath
        << "/" << uniqueName << ".mp4\n";

    SceneParams scene{};
    RenderSettings settings = init_render_settings(uniqueName, 4);

    renderSequencePush(
        settings,
        camera_callback,      // orbit camera (writes into SceneParams)
        line_push_callback,   // analytic ribbons + sparkles (reads SceneParams)
        &scene                // shared state for both camera + lines
    );

    VLC::play(g_base_output_filepath + "/" + uniqueName + ".mp4");

    return 0;
}
