#pragma once

#include "WireEngine_v5.h"

// -----------------------------------------------------------------------------
// Standard library
// -----------------------------------------------------------------------------
#include <array>
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

// -----------------------------------------------------------------------------
// Third-party / single-header style libs
// -----------------------------------------------------------------------------

// Math / geometry
#include "../External_libs/glm_0_9_9_7/glm/glm/glm.hpp"
#include "../External_libs/glm_0_9_9_7/glm/glm/gtc/matrix_transform.hpp"
#include "../External_libs/glm_0_9_9_7/glm/glm/gtc/type_ptr.hpp"

// JSON – for future scene description / camera paths / presets
#include "../External_libs/nlohmann/json.h"

// stb utilities – for future examples (image export, noise, etc.)
#include "../External_libs/stb/image/stb_image_write.h"
#include "../External_libs/stb/image/stb_image.h"
#include "../External_libs/stb/perlin/stb_perlin.h"

// Optional helpers (uncomment when you add them)
// #include "../External_libs/My/Easing/Easing.h"
// #include "../External_libs/My/Color/ColorUtils.h"

// VLC wrapper you already use
#include "../External_libs/My/VLC/VLC.h"

// -----------------------------------------------------------------------------
// Global aliases / types
// -----------------------------------------------------------------------------
namespace fs = std::filesystem;
using json = nlohmann::json;
using Vec3 = glm::vec3;

// Where all videos are written (examples use this)
inline const std::string g_base_output_filepath =
"C:/Users/Cosmos/Desktop/output/tmp";

// -----------------------------------------------------------------------------
// Random helpers
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
// Vec3 helpers using GLM
// -----------------------------------------------------------------------------
inline Vec3 make_vec3(float x, float y, float z)
{
    return Vec3(x, y, z);
}

inline float dot3(const Vec3& a, const Vec3& b)
{
    return glm::dot(a, b);
}

inline Vec3 cross3(const Vec3& a, const Vec3& b)
{
    return glm::cross(a, b);
}

inline float length3(const Vec3& v)
{
    return glm::length(v);
}

inline Vec3 normalize3(const Vec3& v)
{
    float len = glm::length(v);
    if (len <= 1e-6f) return Vec3(0.0f);
    return v / len;
}

// -----------------------------------------------------------------------------
// Unique-name helper + macro (what WIRE_UNIQUE_NAME uses)
// -----------------------------------------------------------------------------
inline std::string wire_generate_unique_name(const std::string& outputDir,
    const char* sourceFilePath)
{
    std::string fullPath = sourceFilePath ? sourceFilePath : "";
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

    // Ensure directory exists
    fs::path outDir = outputDir;
    if (!fs::exists(outDir))
    {
        std::error_code ec;
        fs::create_directories(outDir, ec);
        if (ec)
        {
            std::cerr << "wire_generate_unique_name: failed to create output dir: "
                << outDir.string() << " (" << ec.message() << ")\n";
        }
    }

    // Find first free "<baseName>_V_<n>.mp4"
    int version = 1;
    while (true)
    {
        std::ostringstream nameStream;
        nameStream << baseName << "_V_" << version;
        const std::string candidateName = nameStream.str();

        fs::path candidatePath = outDir / (candidateName + ".mp4");
        if (!fs::exists(candidatePath))
        {
            return candidateName;
        }
        ++version;
    }
}

// Convenience macro so examples can just call:
//   const std::string uniqueName = WIRE_UNIQUE_NAME(g_base_output_filepath);
#define WIRE_UNIQUE_NAME(outputDir) \
    wire_generate_unique_name((outputDir), __FILE__)


