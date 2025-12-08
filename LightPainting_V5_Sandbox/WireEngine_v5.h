#pragma once
#include <functional>
#include <string>

namespace WireEngine {

    // One thick 3D segment with a color gradient.
    // Longer "lines" / polylines are just sequences of these segments
    // (you connect your points and give all segments the same thickness).
    struct LineParams {
        // World-space endpoints
        float start_x = 0.0f, start_y = 0.0f, start_z = 0.0f;
        float end_x = 0.0f, end_y = 0.0f, end_z = 0.0f;

        // Colors at start/end of the segment
        float start_r = 1.0f, start_g = 1.0f, start_b = 1.0f;
        float end_r = 1.0f, end_g = 1.0f, end_b = 1.0f;

        // Segment thickness in world units (radius of the ribbon)
        float thickness = 1.0f;

        // WORLD-SPACE jitter radius for the whole segment
        float jitter = 0.0f;

        // Brightness multiplier (1.0 = normal)
        float intensity = 1.0f;
    };

    enum class OutputMode {
        FramesPNG,   // write frame_0000.png, ...
        FFmpegVideo  // stream raw frames into ffmpeg
    };

    // Blending / depth behaviour for line rendering
    enum class LineBlendMode {
        AdditiveLightPainting, // additive, no depth test (classic light painting)
        OpaqueWithDepth        // opaque lines with depth test/write
    };

    struct CameraParams {
        // Camera position
        float eye_x = 0.0f, eye_y = 0.0f, eye_z = 450.0f;
        // Camera target
        float target_x = 0.0f, target_y = 0.0f, target_z = 0.0f;
        // Up vector
        float up_x = 0.0f, up_y = 1.0f, up_z = 0.0f;

        // If false => engine uses its default FOV (currently 60°).
        bool  has_custom_fov = false;
        float fov_y_deg = 60.0f;

        // If false => engine uses its default near/far planes (0.1 / 3000).
        bool  has_custom_clip = false;
        float near_plane = 0.1f;
        float far_plane = 3000.0f;

        void* user_ptr = nullptr;
    };

    struct RenderSettings {
        // Resolution / timing
        int   width = 1280;
        int   height = 720;
        int   frames = 60;
        float fps = 60.0f;

        // How many accumulation passes per frame (light painting jitter)
        int   accum_passes = 200;

        // Glow / bloom controls
        float exposure = 1.5f;   // overall tonemap exposure
        float bloom_threshold = 0.70f;  // how bright a pixel must be to bloom
        float bloom_strength = 1.1f;   // how much bloom is added back
        bool  bloom_enabled = true;   // master toggle for bloom

        // Line softness and energy
        float soft_edge = 0.85f;     // 0..1, softer or harder edges
        float energy_per_hit = 8.0e-5f;   // base energy scaling
        float thickness_scale = 0.7f;      // global multiplier for all thickness

        // Hint for maximum number of segments per frame.
        // This controls the size of the big GPU buffer.
        // With 4M segments and 60 bytes per segment, that's ~240 MB of VRAM.
        int   max_line_segments_hint = 4 * 1024 * 1024;

        // Readback & IO
        bool        use_pbo = true;                     // async readback
        std::string output_dir = "frames_wire_lines_glow_v3"; // PNG folder

        // Output mode
        OutputMode  output_mode = OutputMode::FramesPNG;
        std::string ffmpeg_path = "ffmpeg";   // or full path to ffmpeg.exe
        std::string ffmpeg_output = "wire.mp4";
        std::string ffmpeg_extra_args;            // appended before output

        // How to blend / depth-test lines
        LineBlendMode line_blend_mode = LineBlendMode::AdditiveLightPainting;
    };

    // -------------------------------------------------------------------------
    // Callbacks (pull-style)
    // -------------------------------------------------------------------------

    // Called once per frame to set camera.
    using CameraCallback =
        std::function<void(int frame, float t, CameraParams& out)>;

    // Called many times per frame to provide segments.
    // Return true if segmentIndex exists and 'out' is filled,
    // return false when there are no more segments for this frame.
    using LineCallback =
        std::function<bool(int frame, float t, int segmentIndex, LineParams& out)>;

    // -------------------------------------------------------------------------
    // Push-style line generation
    // -------------------------------------------------------------------------

    struct LineEmitContext {
        // User data you pass to renderSequencePush (optional).
        void* user_ptr = nullptr;

        // Functions provided by the engine for this frame.
        std::function<void(const LineParams&)> emit;
        std::function<void()>                  flush;

        // Convenience helpers so user code looks nice.
        void add(const LineParams& lp) const {
            if (emit) emit(lp);
        }
        void flush_now() const {
            if (flush) flush();
        }
    };

    // You get (frame, t, ctx) and you just call ctx.emit(lp) as many times
    // as you like. ctx.user_ptr is whatever you passed into renderSequencePush.
    using LinePushCallback =
        std::function<void(int frame, float t, LineEmitContext& ctx)>;

    // -------------------------------------------------------------------------
    // Public API
    // -------------------------------------------------------------------------

    // Main entry point: user calls this, engine does all GL + IO work.
    void renderSequence(const RenderSettings& settings,
        const CameraCallback& cameraCb,
        const LineCallback& lineCb,
        void* camera_user_ptr = nullptr);

    // Push-style variant: user pushes lines via LineEmitContext.
    // user_ptr is forwarded into LineEmitContext::user_ptr every frame.
    void renderSequencePush(const RenderSettings& settings,
        const CameraCallback& cameraCb,
        const LinePushCallback& lineCb,
        void* user_ptr = nullptr);

} // namespace WireEngine
