#include "WireEngine_v5.h"

#define _CRT_SECURE_NO_WARNINGS
#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <vector>
#include <string>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <filesystem>
#include <cstring>
#include <cstdlib>
#include <cstdio>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "../External_libs/stb/image/stb_image_write.h"

namespace fs = std::filesystem;

namespace WireEngine {

    // ========================================================================
    // FFmpeg pipe helper
    // ========================================================================
    struct FFmpegPipe {
        bool  enabled = false;
        FILE* pipe = nullptr;
    };

    static bool openFFmpegPipe(FFmpegPipe& vp, const RenderSettings& settings) {
        if (settings.output_mode != OutputMode::FFmpegVideo)
            return false;

        std::string exe = settings.ffmpeg_path.empty()
            ? std::string("ffmpeg")
            : settings.ffmpeg_path;

        std::ostringstream cmd;

#if defined(_WIN32)
        if (exe.find(' ') != std::string::npos) {
            cmd << "\"" << exe << "\"";
        }
        else {
            cmd << exe;
        }
#else
        cmd << exe;
#endif

        cmd << " -y"
            << " -f rawvideo"
            << " -pixel_format rgba"
            << " -video_size " << settings.width << "x" << settings.height
            << " -framerate " << settings.fps
            << " -i - ";

        if (!settings.ffmpeg_extra_args.empty()) {
            cmd << settings.ffmpeg_extra_args << " ";
        }
        else {
            cmd << "-c:v libx264 -preset veryfast -crf 18 ";
        }

        cmd << "-pix_fmt yuv420p "
            << "\"" << settings.ffmpeg_output << "\"";

        std::string cmdStr = cmd.str();
        std::cout << "[WireEngine] FFmpeg command:\n" << cmdStr << "\n";

#if defined(_WIN32)
        vp.pipe = _popen(cmdStr.c_str(), "wb");
#else
        vp.pipe = popen(cmdStr.c_str(), "w");
#endif

        if (!vp.pipe) {
            std::cerr << "[WireEngine] Failed to start ffmpeg process.\n";
            vp.enabled = false;
            return false;
        }

        vp.enabled = true;
        return true;
    }

    static void closeFFmpegPipe(FFmpegPipe& vp) {
        if (!vp.enabled || !vp.pipe) return;
#if defined(_WIN32)
        _pclose(vp.pipe);
#else
        pclose(vp.pipe);
#endif
        vp.pipe = nullptr;
        vp.enabled = false;
    }

    static void ffmpegWriteFrame(FFmpegPipe& vp,
        const unsigned char* rgbaTopDown,
        int w, int h)
    {
        if (!vp.enabled || !vp.pipe || !rgbaTopDown) return;
        const size_t bytes = size_t(w) * h * 4;
        const size_t written = fwrite(rgbaTopDown, 1, bytes, vp.pipe);
        if (written != bytes) {
            std::cerr << "[WireEngine] FFmpeg pipe write short: "
                << written << " / " << bytes << " bytes\n";
        }
    }

    static const int YIELD_EVERY_PASSES = 6;

    // ========================================================================
    // Utils: GL helpers, FBOs, VAOs, post shaders
    // ========================================================================
    namespace Utils_ {

        // ----- Shader helpers -----
        static GLuint compileShader(GLenum type, const char* src) {
            GLuint sh = glCreateShader(type);
            glShaderSource(sh, 1, &src, nullptr);
            glCompileShader(sh);
            GLint ok = 0;
            glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                GLint len = 0;
                glGetShaderiv(sh, GL_INFO_LOG_LENGTH, &len);
                std::vector<char> log(len);
                glGetShaderInfoLog(sh, len, nullptr, log.data());
                std::cerr << "Shader compile error:\n" << log.data() << "\n";
                std::exit(EXIT_FAILURE);
            }
            return sh;
        }

        static GLuint createProgram(const char* vs, const char* fs) {
            GLuint v = compileShader(GL_VERTEX_SHADER, vs);
            GLuint f = compileShader(GL_FRAGMENT_SHADER, fs);
            GLuint p = glCreateProgram();
            glAttachShader(p, v);
            glAttachShader(p, f);
            glLinkProgram(p);
            glDeleteShader(v);
            glDeleteShader(f);
            GLint ok = 0;
            glGetProgramiv(p, GL_LINK_STATUS, &ok);
            if (!ok) {
                GLint len = 0;
                glGetProgramiv(p, GL_INFO_LOG_LENGTH, &len);
                std::vector<char> log(len);
                glGetProgramInfoLog(p, len, nullptr, log.data());
                std::cerr << "Program link error:\n" << log.data() << "\n";
                std::exit(EXIT_FAILURE);
            }
            return p;
        }

        // ----- FBO wrappers -----
        struct HDRFBO {
            GLuint fbo = 0;
            GLuint colorTex = 0;
            GLuint depthRbo = 0;
        };

        struct ColorFBO {
            GLuint fbo = 0;
            GLuint colorTex = 0;
        };

        static HDRFBO createHDRFBO(int w, int h) {
            HDRFBO o{};
            glGenFramebuffers(1, &o.fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, o.fbo);

            glGenTextures(1, &o.colorTex);
            glBindTexture(GL_TEXTURE_2D, o.colorTex);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, w, h, 0,
                GL_RGBA, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D, o.colorTex, 0);

            glGenRenderbuffers(1, &o.depthRbo);
            glBindRenderbuffer(GL_RENDERBUFFER, o.depthRbo);
            glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h);
            glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                GL_RENDERBUFFER, o.depthRbo);

            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                std::cerr << "HDRFBO incomplete\n";
                std::exit(EXIT_FAILURE);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return o;
        }

        static ColorFBO createColorFBO(int w, int h, GLint internalFormat) {
            ColorFBO o{};
            glGenFramebuffers(1, &o.fbo);
            glBindFramebuffer(GL_FRAMEBUFFER, o.fbo);

            glGenTextures(1, &o.colorTex);
            glBindTexture(GL_TEXTURE_2D, o.colorTex);
            glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, w, h, 0,
                GL_RGBA, GL_FLOAT, nullptr);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                GL_TEXTURE_2D, o.colorTex, 0);

            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
                std::cerr << "ColorFBO incomplete\n";
                std::exit(EXIT_FAILURE);
            }
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return o;
        }

        // ----- Geometry data -----
        // We render each segment as a camera-facing ribbon quad.
        // uv.x = 0 (start) / 1 (end)
        // uv.y = 0 (side -1) / 1 (side +1)
        static const float SEGMENT_QUAD[] = {
            // pos.x, pos.y,  u, v
            -1.0f, -1.0f,   0.0f, 0.0f,  // start, side -1
             1.0f, -1.0f,   1.0f, 0.0f,  // end,   side -1
             1.0f,  1.0f,   1.0f, 1.0f,  // end,   side +1

            -1.0f, -1.0f,   0.0f, 0.0f,  // start, side -1
             1.0f,  1.0f,   1.0f, 1.0f,  // end,   side +1
            -1.0f,  1.0f,   0.0f, 1.0f   // start, side +1
        };

        static const float FSQ[] = {
            -1.0f,-1.0f, 0.0f,0.0f,
             1.0f,-1.0f, 1.0f,0.0f,
             1.0f, 1.0f, 1.0f,1.0f,
            -1.0f,-1.0f, 0.0f,0.0f,
             1.0f, 1.0f, 1.0f,1.0f,
            -1.0f, 1.0f, 0.0f,1.0f
        };

        static void makeVAO(GLuint& vao, GLuint& vbo,
            const float* data, size_t bytes)
        {
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &vbo);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)bytes, data, GL_STATIC_DRAW);

            glEnableVertexAttribArray(0);
            glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                4 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(1);
            glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                4 * sizeof(float), (void*)(2 * sizeof(float)));

            glBindVertexArray(0);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
        }

        // ----- Fullscreen VS -----
        static const char* FSQ_VS = R"GLSL(
#version 330 core
layout(location=0) in vec2 aPos;
layout(location=1) in vec2 aUV;
out vec2 vUV;
void main() {
    vUV = aUV;
    gl_Position = vec4(aPos,0.0,1.0);
}
)GLSL";

        // ----- Bright pass FS -----
        static const char* BRIGHT_FS = R"GLSL(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uHDRTex;
uniform float uExposure;
uniform float uThreshold;

vec3 tonemap(vec3 x, float e){ return 1.0 - exp(-x*e); }

void main() {
    vec3 hdr    = texture(uHDRTex, vUV).rgb;
    vec3 mapped = tonemap(hdr, uExposure);
    vec3 bright = max(mapped - vec3(uThreshold), 0.0);
    FragColor   = vec4(bright, 1.0);
}
)GLSL";

        // ----- Blur FS -----
        static const char* BLUR_FS = R"GLSL(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uTex;
uniform vec2 uTexelSize;
uniform vec2 uDirection;

void main() {
    float w0 = 0.2270270270;
    float w1 = 0.3162162162;
    float w2 = 0.0702702703;

    vec3 col = texture(uTex, vUV).rgb * w0;
    vec2 o1  = uTexelSize * uDirection * 1.0;
    vec2 o2  = uTexelSize * uDirection * 2.0;

    col += texture(uTex, vUV + o1).rgb * w1;
    col += texture(uTex, vUV - o1).rgb * w1;
    col += texture(uTex, vUV + o2).rgb * w2;
    col += texture(uTex, vUV - o2).rgb * w2;

    FragColor = vec4(col,1.0);
}
)GLSL";

        // ----- Composite FS -----
        static const char* COMPOSITE_FS = R"GLSL(
#version 330 core
in vec2 vUV;
out vec4 FragColor;
uniform sampler2D uHDRTex;
uniform sampler2D uBloomTex;
uniform float uExposure;
uniform float uBloomStrength;

vec3 tonemap(vec3 x, float e){ return 1.0 - exp(-x*e); }

void main() {
    vec3 hdr    = texture(uHDRTex,  vUV).rgb;
    vec3 bloom  = texture(uBloomTex, vUV).rgb;
    vec3 mapped = tonemap(hdr, uExposure);
    vec3 color  = mapped + uBloomStrength * bloom;
    color       = pow(color, vec3(1.0/2.2)); // gamma
    FragColor   = vec4(color,1.0);
}
)GLSL";

    } // namespace Utils_

    // ========================================================================
    // Scene shaders (thick ribbon per segment, instanced)
    // ========================================================================

    static const char* SCENE_VS = R"GLSL(
#version 330 core

layout(location=0) in vec2 aPos; // unused, kept for layout stability
layout(location=1) in vec2 aUV;  // x: along (0 start, 1 end), y: side (0 -> -1, 1 -> +1)

// Per-segment instance data
layout(location=2) in vec3  aStartPos;
layout(location=3) in vec3  aEndPos;
layout(location=4) in vec3  aStartColor;
layout(location=5) in vec3  aEndColor;
layout(location=6) in float aThickness;
layout(location=7) in float aJitter;
layout(location=8) in float aIntensity;

out vec2  vUV;
out vec3  vCol;
out float vDist;
out float vIntensity;

uniform mat4  uProj;
uniform mat4  uView;
uniform float uThicknessScale;
uniform int   uPassIndex;
uniform int   uFrameIndex;
uniform float uTime;
uniform int   uSegmentOffset;

// --- small hash helpers ---
uint hash_u(uint x){
    x ^= x >> 16u;
    x *= 0x7feb352du;
    x ^= x >> 15u;
    x *= 0x846ca68bu;
    x ^= x >> 16u;
    return x;
}
float h1(uint x){ return float(hash_u(x)) / float(0xffffffffu); }

void main() {
    vIntensity = aIntensity;

    int segIndex = uSegmentOffset + gl_InstanceID;

    // Stable per-segment / per-pass seed
    uint seed = uint(segIndex);
    seed ^= uint(uPassIndex)  * 2654435761u;
    seed ^= uint(uFrameIndex) * 2246822519u;

    float along   = clamp(aUV.x, 0.0, 1.0);           // 0..1 along segment
    float sideRaw = aUV.y * 2.0 - 1.0;               // 0->-1, 1->+1

    vec3 start   = aStartPos;
    vec3 end     = aEndPos;
    vec3 dir     = end - start;
    float segLen = max(length(dir), 1e-5);
    vec3 lineDir = dir / segLen;

    vec3 basePos = mix(start, end, along);

    // Camera basis from view matrix (columns)
    vec3 camRight   = vec3(uView[0][0], uView[1][0], uView[2][0]);
    vec3 camUp      = vec3(uView[0][1], uView[1][1], uView[2][1]);
    vec3 camForward = normalize(cross(camRight, camUp));

    // Side axis perpendicular to both forward and lineDir (for ribbon width)
    vec3 side = normalize(cross(camForward, lineDir));
    if (length(side) < 1e-4) {
        side = camRight;
    }

    // Local "up" axis (for jitter radius)
    vec3 upLocal = normalize(cross(lineDir, side));

    // Radial jitter around the segment
    float ang   = h1(seed) * 6.2831853;
    float rad01 = h1(seed ^ 0x9e3779b9u);
    float jRad  = aJitter * rad01;
    vec3 jitterOffset = (cos(ang)*side + sin(ang)*upLocal) * jRad;

    // Thickness (world units)
    float thickness = aThickness * uThicknessScale;

    vec3 offsetAcross = side * (sideRaw * thickness);

    vec3 world = basePos + jitterOffset + offsetAcross;

    vCol  = mix(aStartColor, aEndColor, along);
    vDist = length(world);
    vUV   = vec2(along, sideRaw);

    gl_Position = uProj * uView * vec4(world, 1.0);
}
)GLSL";

    static const char* SCENE_FS = R"GLSL(
#version 330 core
in vec2  vUV;
in vec3  vCol;
in float vDist;
in float vIntensity;
out vec4 FragColor;

uniform float uSoft;          // 0..1 edge softness
uniform float uEnergyPerHit;  // base contribution per segment

void main() {
    // vUV.y in [-1, 1] is across-ribbon coordinate
    float v = vUV.y;
    float r = abs(v);

    // Soft edge falloff across width
    float inner = mix(0.60, 0.95, uSoft);
    float outer = 1.00;
    float edge  = smoothstep(inner, outer, r);
    float strip = 1.0 - edge;

    // Distance attenuation (keeps far segments dimmer)
    float atten = 1.0 / (1.0 + 0.0008 * vDist * vDist);

    vec3 col = vCol * strip * atten * uEnergyPerHit * max(vIntensity, 1.0);
    FragColor = vec4(col, 1.0);
}
)GLSL";

    // ========================================================================
    // Readback (PBO + PNG / FFmpeg) from explicit FBO
    // ========================================================================
    struct PBOReadback {
        bool   enabled = false;
        GLuint pbo[2] = { 0, 0 };
        int    curr = 0;
        int    prev = 1;
        bool   first = true;
        size_t bytes = 0;
    };

    static void initPBO(PBOReadback& rb, bool enabled, int w, int h) {
        rb.enabled = enabled;
        rb.first = true;
        rb.curr = 0;
        rb.prev = 1;
        rb.bytes = 0;

        if (!enabled) {
            rb.pbo[0] = rb.pbo[1] = 0;
            return;
        }

        rb.bytes = size_t(w) * size_t(h) * 4;
        glGenBuffers(2, rb.pbo);
        for (int i = 0; i < 2; ++i) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, rb.pbo[i]);
            glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)rb.bytes,
                nullptr, GL_STREAM_READ);
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }

    static void destroyPBO(PBOReadback& rb) {
        if (rb.enabled && rb.pbo[0]) {
            glDeleteBuffers(2, rb.pbo);
        }
        rb.pbo[0] = rb.pbo[1] = 0;
    }

    // Read pixels from a specific FBO (our offscreen LDR target).
    static void saveOrStreamBackbuffer(PBOReadback& rb,
        int frameIndex,
        int w, int h,
        const std::string& outDir,
        OutputMode outputMode,
        FFmpegPipe* ffmpeg,
        GLuint srcFBO)
    {
        if (outputMode == OutputMode::FramesPNG) {
            fs::create_directories(outDir);
        }

        glBindFramebuffer(GL_FRAMEBUFFER, srcFBO);
        glReadBuffer(GL_COLOR_ATTACHMENT0);

        const int rowBytes = w * 4;

        auto writeFrame = [&](int effectiveIndex,
            const unsigned char* flipped)
            {
                if (outputMode == OutputMode::FFmpegVideo &&
                    ffmpeg && ffmpeg->enabled)
                {
                    ffmpegWriteFrame(*ffmpeg, flipped, w, h);
                }
                else {
                    std::ostringstream oss;
                    oss << outDir << "/frame_"
                        << std::setw(4) << std::setfill('0') << effectiveIndex
                        << ".png";
                    stbi_write_png(oss.str().c_str(), w, h, 4, flipped, rowBytes);
                }
            };

        // No PBO: synchronous path
        if (!rb.enabled) {
            std::vector<unsigned char> rgba(size_t(w) * size_t(h) * 4);
            std::vector<unsigned char> flipped(size_t(w) * size_t(h) * 4);
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());

            for (int y = 0; y < h; ++y) {
                int sy = h - 1 - y;
                std::memcpy(&flipped[y * rowBytes],
                    &rgba[sy * rowBytes],
                    rowBytes);
            }

            writeFrame(frameIndex, flipped.data());
            glBindFramebuffer(GL_FRAMEBUFFER, 0);
            return;
        }

        // Async PBO path
        glBindBuffer(GL_PIXEL_PACK_BUFFER, rb.pbo[rb.curr]);
        glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, 0);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

        if (!rb.first) {
            glBindBuffer(GL_PIXEL_PACK_BUFFER, rb.pbo[rb.prev]);
            const unsigned char* src =
                (const unsigned char*)glMapBuffer(GL_PIXEL_PACK_BUFFER,
                    GL_READ_ONLY);
            if (src) {
                std::vector<unsigned char> flipped(size_t(w) * size_t(h) * 4);
                for (int y = 0; y < h; ++y) {
                    int sy = h - 1 - y;
                    std::memcpy(&flipped[y * rowBytes],
                        &src[sy * rowBytes],
                        rowBytes);
                }
                glUnmapBuffer(GL_PIXEL_PACK_BUFFER);

                // Frame index - 1 because of the PBO pipeline delay
                writeFrame(frameIndex - 1, flipped.data());
            }
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        }
        else {
            rb.first = false;
        }

        std::swap(rb.curr, rb.prev);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    static void flushLastPBOFrame(PBOReadback& rb,
        int lastFrameIndex,
        int w, int h,
        const std::string& outDir,
        OutputMode outputMode,
        FFmpegPipe* ffmpeg)
    {
        if (!rb.enabled || rb.first) return;

        const int rowBytes = w * 4;

        auto writeFrame = [&](int idx,
            const unsigned char* flipped)
            {
                if (outputMode == OutputMode::FFmpegVideo &&
                    ffmpeg && ffmpeg->enabled)
                {
                    ffmpegWriteFrame(*ffmpeg, flipped, w, h);
                }
                else {
                    fs::create_directories(outDir);
                    std::ostringstream oss;
                    oss << outDir << "/frame_"
                        << std::setw(4) << std::setfill('0') << idx << ".png";
                    stbi_write_png(oss.str().c_str(), w, h, 4, flipped, rowBytes);
                }
            };

        glBindBuffer(GL_PIXEL_PACK_BUFFER, rb.pbo[rb.prev]);
        const unsigned char* src =
            (const unsigned char*)glMapBuffer(GL_PIXEL_PACK_BUFFER,
                GL_READ_ONLY);
        if (src) {
            std::vector<unsigned char> flipped(size_t(w) * size_t(h) * 4);
            for (int y = 0; y < h; ++y) {
                int sy = h - 1 - y;
                std::memcpy(&flipped[y * rowBytes],
                    &src[sy * rowBytes],
                    rowBytes);
            }
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);

            writeFrame(lastFrameIndex, flipped.data());
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }

    // ========================================================================
    // Renderer state & uniforms
    // ========================================================================
    struct SceneUniforms {
        GLint uProj = -1;
        GLint uView = -1;
        GLint uThicknessScale = -1;
        GLint uPassIndex = -1;
        GLint uFrameIndex = -1;
        GLint uTime = -1;
        GLint uSoft = -1;
        GLint uEnergy = -1;
        GLint uSegmentOffset = -1;
    };

    struct BrightUniforms {
        GLint uHDRTex = -1;
        GLint uExposure = -1;
        GLint uThreshold = -1;
    };

    struct BlurUniforms {
        GLint uTex = -1;
        GLint uTexelSize = -1;
        GLint uDirection = -1;
    };

    struct CompositeUniforms {
        GLint uHDRTex = -1;
        GLint uBloomTex = -1;
        GLint uExposure = -1;
        GLint uBloomStrength = -1;
    };

    struct Programs {
        GLuint scene = 0;
        GLuint bright = 0;
        GLuint blur = 0;
        GLuint composite = 0;
    };

    struct Framebuffers {
        Utils_::HDRFBO   hdr;
        Utils_::ColorFBO bloomA;
        Utils_::ColorFBO bloomB;
        Utils_::ColorFBO ldr;   // final composited LDR image
    };

    struct Geometry {
        GLuint vaoSegment = 0;
        GLuint vboSegment = 0;  // base quad for segment ribbons
        GLuint vaoFSQ = 0;
        GLuint vboFSQ = 0;

        GLuint vboInstance = 0; // per-segment instance data buffer
        int    maxSegments = 0; // capacity (segments per frame)
    };

    struct Viewport {
        int width = 1280;
        int height = 720;
        int halfWidth = 640;
        int halfHeight = 360;
    };

    struct Renderer {
        Viewport      viewport;
        Programs      programs;
        Framebuffers  fbos;
        Geometry      geom;
        glm::mat4     proj;
        glm::mat4     view;
        PBOReadback   readback;
        SceneUniforms sceneU;
        BrightUniforms brightU;
        BlurUniforms   blurU;
        CompositeUniforms compU;

        float exposure;
        float bloomThreshold;
        float bloomStrength;
        bool  bloomEnabled;
        float softEdge;
        float energyPerHit;
        float thicknessScale;

        float baseFovYDeg;
        float baseNearPlane;
        float baseFarPlane;

        LineBlendMode blendMode;
    };

    // GPU layout matching LineParams
    struct LineInstanceGPU {
        float start_x, start_y, start_z;
        float end_x, end_y, end_z;
        float start_r, start_g, start_b;
        float end_r, end_g, end_b;
        float thickness;
        float jitter;
        float intensity;
    };

    // ========================================================================
    // Renderer setup / teardown
    // ========================================================================
    static void initRenderer(Renderer& r, const RenderSettings& settings) {
        r.viewport.width = settings.width;
        r.viewport.height = settings.height;
        r.viewport.halfWidth = settings.width / 2;
        r.viewport.halfHeight = settings.height / 2;

        r.exposure = settings.exposure;
        r.bloomThreshold = settings.bloom_threshold;
        r.bloomStrength = settings.bloom_strength;
        r.bloomEnabled = settings.bloom_enabled;
        r.softEdge = settings.soft_edge;
        r.energyPerHit = settings.energy_per_hit;
        r.thicknessScale = settings.thickness_scale;

        r.baseFovYDeg = 60.0f;
        r.baseNearPlane = 0.1f;
        r.baseFarPlane = 3000.0f;

        r.blendMode = settings.line_blend_mode;

        // Programs
        r.programs.scene = Utils_::createProgram(SCENE_VS, Utils_::BRIGHT_FS); // placeholder
        // Small trick: we want SCENE_VS + SCENE_FS, not BRIGHT_FS; fix:
        glDeleteProgram(r.programs.scene);
        r.programs.scene = Utils_::createProgram(SCENE_VS, SCENE_FS);
        r.programs.bright = Utils_::createProgram(Utils_::FSQ_VS, Utils_::BRIGHT_FS);
        r.programs.blur = Utils_::createProgram(Utils_::FSQ_VS, Utils_::BLUR_FS);
        r.programs.composite = Utils_::createProgram(Utils_::FSQ_VS, Utils_::COMPOSITE_FS);

        // FBOs
        r.fbos.hdr = Utils_::createHDRFBO(r.viewport.width, r.viewport.height);
        r.fbos.bloomA = Utils_::createColorFBO(r.viewport.halfWidth, r.viewport.halfHeight, GL_RGBA16F);
        r.fbos.bloomB = Utils_::createColorFBO(r.viewport.halfWidth, r.viewport.halfHeight, GL_RGBA16F);
        r.fbos.ldr = Utils_::createColorFBO(r.viewport.width, r.viewport.height, GL_RGBA16F);

        // Geometry (segment quad + fullscreen quad)
        Utils_::makeVAO(r.geom.vaoSegment, r.geom.vboSegment,
            Utils_::SEGMENT_QUAD, sizeof(Utils_::SEGMENT_QUAD));
        Utils_::makeVAO(r.geom.vaoFSQ, r.geom.vboFSQ,
            Utils_::FSQ, sizeof(Utils_::FSQ));

        // Instance buffer: big chunk of segments, reused every frame.
        r.geom.maxSegments = settings.max_line_segments_hint;
        if (r.geom.maxSegments <= 0) {
            r.geom.maxSegments = 1024 * 1024; // sane fallback
        }

        glBindVertexArray(r.geom.vaoSegment);
        glGenBuffers(1, &r.geom.vboInstance);
        glBindBuffer(GL_ARRAY_BUFFER, r.geom.vboInstance);
        glBufferData(GL_ARRAY_BUFFER,
            (GLsizeiptr)((size_t)r.geom.maxSegments *
                sizeof(LineInstanceGPU)),
            nullptr,
            GL_DYNAMIC_DRAW);

        GLsizei stride = (GLsizei)sizeof(LineInstanceGPU);
        std::size_t offset = 0;

        // aStartPos (location 2)
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, stride, (void*)offset);
        glVertexAttribDivisor(2, 1);
        offset += sizeof(float) * 3;

        // aEndPos (location 3)
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, stride, (void*)offset);
        glVertexAttribDivisor(3, 1);
        offset += sizeof(float) * 3;

        // aStartColor (location 4)
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, stride, (void*)offset);
        glVertexAttribDivisor(4, 1);
        offset += sizeof(float) * 3;

        // aEndColor (location 5)
        glEnableVertexAttribArray(5);
        glVertexAttribPointer(5, 3, GL_FLOAT, GL_FALSE, stride, (void*)offset);
        glVertexAttribDivisor(5, 1);
        offset += sizeof(float) * 3;

        // aThickness (location 6)
        glEnableVertexAttribArray(6);
        glVertexAttribPointer(6, 1, GL_FLOAT, GL_FALSE, stride, (void*)offset);
        glVertexAttribDivisor(6, 1);
        offset += sizeof(float);

        // aJitter (location 7)
        glEnableVertexAttribArray(7);
        glVertexAttribPointer(7, 1, GL_FLOAT, GL_FALSE, stride, (void*)offset);
        glVertexAttribDivisor(7, 1);
        offset += sizeof(float);

        // aIntensity (location 8)
        glEnableVertexAttribArray(8);
        glVertexAttribPointer(8, 1, GL_FLOAT, GL_FALSE, stride, (void*)offset);
        glVertexAttribDivisor(8, 1);
        offset += sizeof(float);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

        // Default camera (will be overridden each frame)
        r.proj = glm::perspective(glm::radians(r.baseFovYDeg),
            float(r.viewport.width) / float(r.viewport.height),
            r.baseNearPlane, r.baseFarPlane);
        r.view = glm::lookAt(glm::vec3(0, 0, 450),
            glm::vec3(0, 0, 0),
            glm::vec3(0, 1, 0));

        // Uniform locations
        glUseProgram(r.programs.scene);
        r.sceneU.uProj = glGetUniformLocation(r.programs.scene, "uProj");
        r.sceneU.uView = glGetUniformLocation(r.programs.scene, "uView");
        r.sceneU.uThicknessScale = glGetUniformLocation(r.programs.scene, "uThicknessScale");
        r.sceneU.uPassIndex = glGetUniformLocation(r.programs.scene, "uPassIndex");
        r.sceneU.uFrameIndex = glGetUniformLocation(r.programs.scene, "uFrameIndex");
        r.sceneU.uTime = glGetUniformLocation(r.programs.scene, "uTime");
        r.sceneU.uSoft = glGetUniformLocation(r.programs.scene, "uSoft");
        r.sceneU.uEnergy = glGetUniformLocation(r.programs.scene, "uEnergyPerHit");
        r.sceneU.uSegmentOffset = glGetUniformLocation(r.programs.scene, "uSegmentOffset");

        glUseProgram(r.programs.bright);
        r.brightU.uHDRTex = glGetUniformLocation(r.programs.bright, "uHDRTex");
        r.brightU.uExposure = glGetUniformLocation(r.programs.bright, "uExposure");
        r.brightU.uThreshold = glGetUniformLocation(r.programs.bright, "uThreshold");

        glUseProgram(r.programs.blur);
        r.blurU.uTex = glGetUniformLocation(r.programs.blur, "uTex");
        r.blurU.uTexelSize = glGetUniformLocation(r.programs.blur, "uTexelSize");
        r.blurU.uDirection = glGetUniformLocation(r.programs.blur, "uDirection");

        glUseProgram(r.programs.composite);
        r.compU.uHDRTex = glGetUniformLocation(r.programs.composite, "uHDRTex");
        r.compU.uBloomTex = glGetUniformLocation(r.programs.composite, "uBloomTex");
        r.compU.uExposure = glGetUniformLocation(r.programs.composite, "uExposure");
        r.compU.uBloomStrength = glGetUniformLocation(r.programs.composite, "uBloomStrength");

        glUseProgram(0);

        // Readback
        initPBO(r.readback, settings.use_pbo,
            r.viewport.width, r.viewport.height);
    }

    static void destroyRenderer(Renderer& r) {
        destroyPBO(r.readback);

        if (r.geom.vboInstance) glDeleteBuffers(1, &r.geom.vboInstance);
        if (r.geom.vboSegment)  glDeleteBuffers(1, &r.geom.vboSegment);
        if (r.geom.vaoSegment)  glDeleteVertexArrays(1, &r.geom.vaoSegment);
        if (r.geom.vboFSQ)      glDeleteBuffers(1, &r.geom.vboFSQ);
        if (r.geom.vaoFSQ)      glDeleteVertexArrays(1, &r.geom.vaoFSQ);

        if (r.fbos.hdr.depthRbo) glDeleteRenderbuffers(1, &r.fbos.hdr.depthRbo);
        if (r.fbos.hdr.colorTex) glDeleteTextures(1, &r.fbos.hdr.colorTex);
        if (r.fbos.hdr.fbo)      glDeleteFramebuffers(1, &r.fbos.hdr.fbo);

        if (r.fbos.ldr.colorTex) glDeleteTextures(1, &r.fbos.ldr.colorTex);
        if (r.fbos.ldr.fbo)      glDeleteFramebuffers(1, &r.fbos.ldr.fbo);

        if (r.fbos.bloomA.colorTex) glDeleteTextures(1, &r.fbos.bloomA.colorTex);
        if (r.fbos.bloomA.fbo)      glDeleteFramebuffers(1, &r.fbos.bloomA.fbo);
        if (r.fbos.bloomB.colorTex) glDeleteTextures(1, &r.fbos.bloomB.colorTex);
        if (r.fbos.bloomB.fbo)      glDeleteFramebuffers(1, &r.fbos.bloomB.fbo);

        if (r.programs.scene)     glDeleteProgram(r.programs.scene);
        if (r.programs.bright)    glDeleteProgram(r.programs.bright);
        if (r.programs.blur)      glDeleteProgram(r.programs.blur);
        if (r.programs.composite) glDeleteProgram(r.programs.composite);
    }

    // ========================================================================
    // Frame building: cache segments once per frame
    // ========================================================================
    static void buildFrameSegments(const RenderSettings& settings,
        int frameIndex, float timeSec,
        const LineCallback& getLine,
        std::vector<LineInstanceGPU>& outSegments)
    {
        outSegments.clear();
        if (!getLine) return;

        int idx = 0;
        while (true) {
            LineParams lp{};
            if (!getLine(frameIndex, timeSec, idx, lp)) {
                break;
            }
            ++idx;

            if (lp.thickness <= 0.0f) {
                continue; // skip zero-thickness segments
            }

            LineInstanceGPU s{};
            s.start_x = lp.start_x;
            s.start_y = lp.start_y;
            s.start_z = lp.start_z;
            s.end_x = lp.end_x;
            s.end_y = lp.end_y;
            s.end_z = lp.end_z;

            s.start_r = lp.start_r;
            s.start_g = lp.start_g;
            s.start_b = lp.start_b;
            s.end_r = lp.end_r;
            s.end_g = lp.end_g;
            s.end_b = lp.end_b;

            s.thickness = lp.thickness;
            s.jitter = lp.jitter;
            s.intensity = lp.intensity;

            outSegments.push_back(s);
        }

        // Optional: you could clamp to some absolute limit here if you want.
        (void)settings;
    }

    // ========================================================================
    // Rendering steps
    // ========================================================================

    // 1) Accumulate segment ribbons into HDR FBO
    static void accumulateScene(Renderer& r,
        const RenderSettings& settings,
        int frameIndex, float timeSec,
        const std::vector<LineInstanceGPU>& segments)
    {
        glBindFramebuffer(GL_FRAMEBUFFER, r.fbos.hdr.fbo);
        glViewport(0, 0, r.viewport.width, r.viewport.height);

        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        if (r.blendMode == LineBlendMode::AdditiveLightPainting) {
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            glEnable(GL_BLEND);
            glBlendFunc(GL_ONE, GL_ONE);
        }
        else {
            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glDisable(GL_BLEND);
        }

        glUseProgram(r.programs.scene);

        glUniformMatrix4fv(r.sceneU.uProj, 1, GL_FALSE, glm::value_ptr(r.proj));
        glUniformMatrix4fv(r.sceneU.uView, 1, GL_FALSE, glm::value_ptr(r.view));
        glUniform1f(r.sceneU.uThicknessScale, r.thicknessScale);
        glUniform1i(r.sceneU.uFrameIndex, frameIndex);
        glUniform1f(r.sceneU.uTime, timeSec);
        glUniform1f(r.sceneU.uSoft, r.softEdge);
        glUniform1f(r.sceneU.uEnergy, r.energyPerHit);

        glBindVertexArray(r.geom.vaoSegment);

        const size_t totalSegments = segments.size();
        if (totalSegments == 0) {
            glBindVertexArray(0);
            glDisable(GL_BLEND);
            glDisable(GL_DEPTH_TEST);
            glDepthMask(GL_FALSE);
            return;
        }

        const size_t capacity = (size_t)r.geom.maxSegments;

        const bool canUploadOnce = (totalSegments <= capacity);

        if (canUploadOnce) {
            // Upload all segments once, then reuse for all passes
            glBindBuffer(GL_ARRAY_BUFFER, r.geom.vboInstance);
            glBufferSubData(GL_ARRAY_BUFFER, 0,
                (GLsizeiptr)(totalSegments * sizeof(LineInstanceGPU)),
                segments.data());
            glBindBuffer(GL_ARRAY_BUFFER, 0);

            for (int pass = 0; pass < settings.accum_passes; ++pass) {
                glUniform1i(r.sceneU.uPassIndex, pass);
                glUniform1i(r.sceneU.uSegmentOffset, 0);

                glDrawArraysInstanced(GL_TRIANGLES, 0, 6,
                    (GLsizei)totalSegments);

                if ((pass % YIELD_EVERY_PASSES) == 0) {
                    glfwPollEvents();
                    glFlush();
                }
            }
        }
        else {
            // Streaming path: chunk segments into the fixed-size VBO
            for (int pass = 0; pass < settings.accum_passes; ++pass) {
                glUniform1i(r.sceneU.uPassIndex, pass);

                size_t offset = 0;
                while (offset < totalSegments) {
                    size_t chunk = capacity;
                    if (chunk > totalSegments - offset) {
                        chunk = totalSegments - offset;
                    }

                    glBindBuffer(GL_ARRAY_BUFFER, r.geom.vboInstance);
                    glBufferSubData(GL_ARRAY_BUFFER, 0,
                        (GLsizeiptr)(chunk * sizeof(LineInstanceGPU)),
                        segments.data() + offset);
                    glBindBuffer(GL_ARRAY_BUFFER, 0);

                    glUniform1i(r.sceneU.uSegmentOffset,
                        (int)offset); // so RNG stays stable

                    glDrawArraysInstanced(GL_TRIANGLES, 0, 6,
                        (GLsizei)chunk);

                    offset += chunk;
                }

                if ((pass % YIELD_EVERY_PASSES) == 0) {
                    glfwPollEvents();
                    glFlush();
                }
            }
        }

        glBindVertexArray(0);
        glDisable(GL_BLEND);
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
    }

    // 2) Bloom
    static void applyBloom(Renderer& r) {
        const int hw = r.viewport.halfWidth;
        const int hh = r.viewport.halfHeight;

        glBindFramebuffer(GL_FRAMEBUFFER, r.fbos.bloomA.fbo);
        glViewport(0, 0, hw, hh);
        glDisable(GL_DEPTH_TEST);
        glClearColor(0, 0, 0, 0);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(r.programs.bright);
        glBindVertexArray(r.geom.vaoFSQ);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, r.fbos.hdr.colorTex);
        glUniform1i(r.brightU.uHDRTex, 0);
        glUniform1f(r.brightU.uExposure, r.exposure);
        glUniform1f(r.brightU.uThreshold, r.bloomThreshold);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Blur H
        glBindFramebuffer(GL_FRAMEBUFFER, r.fbos.bloomB.fbo);
        glViewport(0, 0, hw, hh);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(r.programs.blur);
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, r.fbos.bloomA.colorTex);
        glUniform1i(r.blurU.uTex, 0);
        glUniform2f(r.blurU.uTexelSize, 1.0f / hw, 1.0f / hh);
        glUniform2f(r.blurU.uDirection, 1.0f, 0.0f);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        // Blur V
        glBindFramebuffer(GL_FRAMEBUFFER, r.fbos.bloomA.fbo);
        glViewport(0, 0, hw, hh);
        glClear(GL_COLOR_BUFFER_BIT);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, r.fbos.bloomB.colorTex);
        glUniform1i(r.blurU.uTex, 0);
        glUniform2f(r.blurU.uDirection, 0.0f, 1.0f);
        glDrawArrays(GL_TRIANGLES, 0, 6);

        glBindVertexArray(0);
    }

    // 3) Composite HDR + bloom into full-res LDR FBO
    static void compositeToLDR(Renderer& r) {
        glBindFramebuffer(GL_FRAMEBUFFER, r.fbos.ldr.fbo);
        glViewport(0, 0, r.viewport.width, r.viewport.height);
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);

        glUseProgram(r.programs.composite);
        glBindVertexArray(r.geom.vaoFSQ);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, r.fbos.hdr.colorTex);
        glUniform1i(r.compU.uHDRTex, 0);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, r.fbos.bloomA.colorTex);
        glUniform1i(r.compU.uBloomTex, 1);

        glUniform1f(r.compU.uExposure, r.exposure);
        glUniform1f(r.compU.uBloomStrength,
            r.bloomEnabled ? r.bloomStrength : 0.0f);

        glDrawArrays(GL_TRIANGLES, 0, 6);
        glBindVertexArray(0);
    }

    static void renderFrame(Renderer& r,
        const RenderSettings& settings,
        FFmpegPipe* ffmpeg,
        int frameIndex,
        float timeSec,
        const LineCallback& lineCb)
    {
        static std::vector<LineInstanceGPU> frameSegments;
        frameSegments.clear();

        buildFrameSegments(settings, frameIndex, timeSec,
            lineCb, frameSegments);

        accumulateScene(r, settings, frameIndex, timeSec, frameSegments);

        if (r.bloomEnabled) {
            applyBloom(r);
        }

        compositeToLDR(r);

        // Read back from full-res LDR FBO
        saveOrStreamBackbuffer(r.readback,
            frameIndex,
            r.viewport.width,
            r.viewport.height,
            settings.output_dir,
            settings.output_mode,
            ffmpeg,
            r.fbos.ldr.fbo);
    }

    // ========================================================================
    // Public API: renderSequence
    // ========================================================================
    void renderSequence(const RenderSettings& settings,
        const CameraCallback& cameraCb,
        const LineCallback& lineCb,
        void* camera_user_ptr
    )
    {
        if (!glfwInit()) {
            std::cerr << "[WireEngine] GLFW init failed\n";
            return;
        }

        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);

        GLFWwindow* win = glfwCreateWindow(settings.width, settings.height,
            "WireEngine_Offscreen", nullptr, nullptr);
        if (!win) {
            std::cerr << "[WireEngine] Window create failed\n";
            glfwTerminate();
            return;
        }

        glfwMakeContextCurrent(win);
        glewExperimental = GL_TRUE;
        if (glewInit() != GLEW_OK) {
            std::cerr << "[WireEngine] GLEW init failed\n";
            glfwDestroyWindow(win);
            glfwTerminate();
            return;
        }

        glViewport(0, 0, settings.width, settings.height);

        Renderer renderer;
        initRenderer(renderer, settings);

        FFmpegPipe ffmpeg{};
        if (settings.output_mode == OutputMode::FFmpegVideo) {
            if (!openFFmpegPipe(ffmpeg, settings)) {
                std::cerr << "[WireEngine] FFmpeg mode requested but pipe failed; "
                    << "falling back to PNG frames.\n";
                ffmpeg.enabled = false;
            }
        }

        for (int f = 0; f < settings.frames; ++f) {
            float t = (settings.fps > 0.0f) ? (float(f) / settings.fps) : float(f);

            CameraParams cam{};
            cam.user_ptr = camera_user_ptr;

            if (cameraCb) {
                cameraCb(f, t, cam);
            }

            glm::vec3 eye = glm::vec3(cam.eye_x, cam.eye_y, cam.eye_z);
            glm::vec3 target = glm::vec3(cam.target_x, cam.target_y, cam.target_z);
            glm::vec3 up = glm::vec3(cam.up_x, cam.up_y, cam.up_z);

            renderer.view = glm::lookAt(eye, target, up);

            float fovY = renderer.baseFovYDeg;
            float nearP = renderer.baseNearPlane;
            float farP = renderer.baseFarPlane;

            if (cam.has_custom_fov)   fovY = cam.fov_y_deg;
            if (cam.has_custom_clip) { nearP = cam.near_plane; farP = cam.far_plane; }

            if (fovY <= 0.0f)          fovY = renderer.baseFovYDeg;
            if (nearP <= 0.0f)         nearP = renderer.baseNearPlane;
            if (farP <= nearP + 1e-4f) farP = renderer.baseFarPlane;

            float aspect = float(renderer.viewport.width) /
                float(renderer.viewport.height);

            renderer.proj = glm::perspective(glm::radians(fovY),
                aspect,
                nearP,
                farP);

            renderFrame(renderer,
                settings,
                ffmpeg.enabled ? &ffmpeg : nullptr,
                f,
                t,
                lineCb);

            glfwPollEvents();
        }

        if (settings.use_pbo) {
            flushLastPBOFrame(renderer.readback,
                settings.frames - 1,
                renderer.viewport.width,
                renderer.viewport.height,
                settings.output_dir,
                settings.output_mode,
                ffmpeg.enabled ? &ffmpeg : nullptr);
        }

        if (ffmpeg.enabled) {
            closeFFmpegPipe(ffmpeg);
        }

        destroyRenderer(renderer);
        glfwDestroyWindow(win);
        glfwTerminate();
    }

    // ========================================================================
   // New: push-style wrapper around the existing pull-style API
   // ========================================================================
    void renderSequencePush(const RenderSettings& settings,
        const CameraCallback& cameraCb,
        const LinePushCallback& pushCb,
        void* user_ptr)
    {
        // If no push-callback, just render nothing.
        if (!pushCb) {
            LineCallback empty;
            renderSequence(settings, cameraCb, empty, user_ptr);
            return;
        }

        // State that lives for the whole duration of renderSequencePush.
        struct PushState {
            const RenderSettings* settings = nullptr;  // currently unused
            LinePushCallback      pushCb;
            void* user_ptr = nullptr;

            int   cachedFrame = -1;
            float cachedTime = -1.0f;

            // Lines generated for the current frame by the push-callback.
            std::vector<LineParams> cachedLines;
        };

        PushState state;
        state.settings = &settings;
        state.pushCb = pushCb;
        state.user_ptr = user_ptr;

        // Adapter: turns the push-style generator into the old pull-style
        // LineCallback. The engine will keep calling this with segmentIndex
        // 0,1,2,... until we return false.
        LineCallback adapter =
            [&state](int frame, float t, int segmentIndex, LineParams& out) -> bool
            {
                // If this is a new frame, (re)build the list of lines by calling
                // the push-style callback once.
                if (frame != state.cachedFrame) {
                    state.cachedFrame = frame;
                    state.cachedTime = t;
                    state.cachedLines.clear();

                    LineEmitContext ctx;
                    ctx.user_ptr = state.user_ptr;

                    // When the user calls ctx.emit(lp), we just append lp
                    // into cachedLines.
                    ctx.emit = [&state](const LineParams& lp) {
                        state.cachedLines.push_back(lp);
                        };

                    // For now, flush is a no-op; the engine renders once per frame
                    // after it has all segments anyway. In the future this could
                    // be used to define "batches" or streaming.
                    ctx.flush = []() {};

                    // Let the user generate all lines for this frame:
                    if (state.pushCb) {
                        state.pushCb(frame, t, ctx);
                    }
                }

                // Serve the requested segmentIndex out of the cached list.
                if (segmentIndex < 0 ||
                    segmentIndex >= static_cast<int>(state.cachedLines.size())) {
                    return false; // no more segments
                }

                out = state.cachedLines[static_cast<std::size_t>(segmentIndex)];
                return true;
            };

        // Reuse the existing engine implementation.
        renderSequence(settings, cameraCb, adapter, user_ptr);
    }


} // namespace WireEngine
