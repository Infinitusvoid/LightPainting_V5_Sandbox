#pragma once

#include "WireUtil.h"


#include <fstream>
#include <cstdint>

#include <cctype> // for std::toupper

#include "../External_libs/tinyply-master/tinyply-master/source/tinyply.h" // adjust path if needed
#include "../External_libs/tinyply-master/tinyply-master/source/tinyply.cpp" // adjust path if needed

#include <numbers>

using namespace WireEngine;

RenderSettings init_render_settings(const std::string base_name, int seconds = 4)
{
    RenderSettings settings;

    {
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
        settings.ffmpeg_output = g_base_output_filepath + "/" + base_name + ".mp4";
        settings.ffmpeg_extra_args = "-c:v libx264 -preset veryfast -crf 18";
    }

    return settings;
}

struct Ground_grid
{
    void draw(LineEmitContext& ctx)
    {
        LineParams line;

        for (int i = -100; i < 100; i++)
        {
            {
                line.start_x = 1.0f * i;
                line.start_y = 0.0f;
                line.start_z = -100.0f;

                line.end_x = 1.0f * i;
                line.end_y = 0.0f;
                line.end_z = 100.0f;

                line.thickness = 0.01f;

                line.jitter = 0.0;

                ctx.add(line);
            }

            {
                line.start_x = -100.0f;
                line.start_y = 0.0f;
                line.start_z = 1.0f * i;

                line.end_x = 100.0f;
                line.end_y = 0.0f;
                line.end_z = 1.0f * i;

                line.thickness = 0.01f;

                line.jitter = 0.0;

                ctx.add(line);
            }

        }
    }
};

struct Circular_system
{
    void draw(LineEmitContext& ctx)
    {
        LineParams line;

        int num_of_steps = 1000;
        float step_size = (1.0f / static_cast<float>(num_of_steps)) * (std::numbers::pi * 2.0f);

        for (int j = 0; j < 10; j++)
        {
            for (int i = 0; i < num_of_steps; i++)
            {
                {
                    float radius = 1.0 + 1.0f * j;

                    {

                        float x_0 = radius * std::sinf(i * step_size);
                        float y_0 = 0.0f;
                        float z_0 = radius * std::cosf(i * step_size);

                        line.start_x = x_0;
                        line.start_y = y_0;
                        line.start_z = z_0;

                    }



                    {
                        float x_1 = radius * std::sinf((i + 1) * step_size);
                        float y_1 = 0.0f;
                        float z_1 = radius * std::cosf((i + 1) * step_size);

                        line.end_x = x_1;
                        line.end_y = y_1;
                        line.end_z = z_1;
                    }

                    line.thickness = 0.01f;

                    line.jitter = 0.0;

                    ctx.add(line);
                }

                for (int n = 0; n < 200; n++)
                {
                    float radius = 1.0 + 1.0f * j + Random::random_signed() * 0.017f;
                    float vertical_offset = Random::random_signed() * 0.2f;

                    float angle_0 = i * step_size;
                    float angle_1 = (i + 1) * step_size;

                    {

                        float x_0 = radius * std::sinf(angle_0);
                        float y_0 = vertical_offset;
                        float z_0 = radius * std::cosf(angle_0);

                        line.start_x = x_0;
                        line.start_y = y_0;
                        line.start_z = z_0;

                    }



                    {
                        float x_1 = radius * std::sinf(angle_1);
                        float y_1 = vertical_offset;
                        float z_1 = radius * std::cosf(angle_1);

                        line.end_x = x_1;
                        line.end_y = y_1;
                        line.end_z = z_1;
                    }



                    {
                        line.thickness = 0.001f;
                        line.jitter = 0.0;

                        line.start_r = abs(std::sinf(angle_0 * 10.0));
                        line.start_g = abs(std::sinf(angle_0 * 32.0));
                        line.start_b = abs(std::sinf(angle_0 * 20.0));

                        line.end_r = abs(std::sinf(angle_1 * 10.0));
                        line.end_g = abs(std::sinf(angle_1 * 32.0));
                        line.end_b = abs(std::sinf(angle_1 * 20.0));

                        line.intensity = 10.0;
                    }


                    ctx.add(line);
                }

            }
        }
    }
};

// -----------------------------------------------------------------------------
// Axis gizmo – RGB axes centered at the origin
// -----------------------------------------------------------------------------
struct Axis
{
    float length = 100.0f;   // how far axes extend
    float thickness = 0.1f;    // line thickness

    void draw(LineEmitContext& ctx) const
    {
        LineParams line{};
        line.thickness = thickness;
        line.jitter = 0.0f;
        line.intensity = 1.0f;

        // X axis (red)
        line.start_x = 0.0f;  line.start_y = 0.0f;   line.start_z = 0.0f;
        line.end_x = length; line.end_y = 0.0f;   line.end_z = 0.0f;
        line.start_r = line.end_r = 1.0f;
        line.start_g = line.end_g = 0.0f;
        line.start_b = line.end_b = 0.0f;
        ctx.add(line);

        // Y axis (green)
        line.start_x = 0.0f;   line.start_y = 0.0f;   line.start_z = 0.0f;
        line.end_x = 0.0f;   line.end_y = length; line.end_z = 0.0f;
        line.start_r = line.end_r = 0.0f;
        line.start_g = line.end_g = 1.0f;
        line.start_b = line.end_b = 0.0f;
        ctx.add(line);

        // Z axis (blue)
        line.start_x = 0.0f;   line.start_y = 0.0f;   line.start_z = 0.0f;
        line.end_x = 0.0f;   line.end_y = 0.0f;   line.end_z = length;
        line.start_r = line.end_r = 0.0f;
        line.start_g = line.end_g = 0.0f;
        line.start_b = line.end_b = 1.0f;
        ctx.add(line);
    }
};

// -----------------------------------------------------------------------------
// Unit_box – a simple wireframe cube, centered at the origin
//   - Used as a reference "size unit" in the scene.
//   - By default it's 1×1×1, from -0.5 to +0.5 on each axis.
// -----------------------------------------------------------------------------
struct Unit_box
{
    float edge_length = 1.0f;   // total size of the cube
    float thickness = 0.01f;  // line thickness
    float intensity = 10.0f;   // brightness of the lines

    void draw(LineEmitContext& ctx) const
    {
        LineParams line{};
        line.thickness = thickness;
        line.jitter = 0.0f;
        line.intensity = intensity;

        // Soft white-ish color so it doesn't dominate your neon stuff
        line.start_r = line.end_r = 0.9f;
        line.start_g = line.end_g = 0.9f;
        line.start_b = line.end_b = 0.9f;

        const float h = edge_length * 0.5f; // half-edge

        // 8 corners of the cube
        struct P { float x, y, z; };
        P v[8] = {
            { -h, -h, -h }, // 0
            {  h, -h, -h }, // 1
            {  h,  h, -h }, // 2
            { -h,  h, -h }, // 3
            { -h, -h,  h }, // 4
            {  h, -h,  h }, // 5
            {  h,  h,  h }, // 6
            { -h,  h,  h }  // 7
        };

        auto emit_edge = [&](int a, int b)
            {
                line.start_x = v[a].x; line.start_y = v[a].y; line.start_z = v[a].z;
                line.end_x = v[b].x; line.end_y = v[b].y; line.end_z = v[b].z;
                ctx.add(line);
            };

        // Bottom square (z = -h)
        emit_edge(0, 1);
        emit_edge(1, 2);
        emit_edge(2, 3);
        emit_edge(3, 0);

        // Top square (z = +h)
        emit_edge(4, 5);
        emit_edge(5, 6);
        emit_edge(6, 7);
        emit_edge(7, 4);

        // Vertical edges
        emit_edge(0, 4);
        emit_edge(1, 5);
        emit_edge(2, 6);
        emit_edge(3, 7);
    }
};


// -----------------------------------------------------------------------------
// Universe – "orbiting around the sculpture" example
//   - Ground grid in XZ
//   - Circular ring system around the origin
//   - XYZ axis gizmo
// -----------------------------------------------------------------------------
struct Universe
{
    Ground_grid     ground_grid;
    Circular_system circular_system;
    Axis            axis;
    Unit_box unit_box;

    // later you can add params here (e.g. ring radius, grid size, color modes)

    Universe() = default;

    // Time-aware draw so you *can* animate later if you want,
    // even if right now it only calls static draws.
    void draw(LineEmitContext& ctx, int frame, float t)
    {
        (void)frame;
        (void)t;

        // Ground reference plane
        ground_grid.draw(ctx);

        // Main circular system around the origin
        circular_system.draw(ctx);

        // XYZ gizmo to show orientation
        axis.draw(ctx);

        unit_box.draw(ctx);
    }
};



void camera_callback(int frame, float t, CameraParams& cam)
{
    (void)frame;

    {
        auto* scene = static_cast<Universe*>(cam.user_ptr);
        if (!scene) return;

        const float twoPi = 6.2831853f;

        // Orbit parameters
        float orbitRadius = 10.0f;
        float orbitHeight = 2.0f;
        float orbitSpeed = 0.25f;     // revolutions per second-ish

        float angle = t * orbitSpeed * twoPi;

        // Orbit in XZ around tunnel center
        cam.eye_x = std::cos(angle) * orbitRadius;
        cam.eye_y = orbitHeight;
        cam.eye_z = std::sin(angle) * orbitRadius;

        // Look at the tunnel center
        cam.target_x = 0.0f;
        cam.target_y = 0.0f;
        cam.target_z = 0.0;

        cam.up_x = 0.0f;
        cam.up_y = 1.0f;
        cam.up_z = 0.0f;

        cam.has_custom_fov = true;
        cam.fov_y_deg = 60.0f;
    }
}




void line_push_callback(int frame, float t, LineEmitContext& ctx)
{
    Universe* universe = static_cast<Universe*>(ctx.user_ptr);
    if (!universe) return;

    universe->draw(ctx, frame, t);
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

    renderSequencePush(
        settings,
        camera_callback,
        line_push_callback,
        &universe
    );

    VLC::play(g_base_output_filepath + "/" + uniqueName + ".mp4");
    return 0;
}

// TODO :

// we orbit around the unit cube this is the starting point
// than we build next cube 
// than we make different sides of cube 
// we scale the cube 
