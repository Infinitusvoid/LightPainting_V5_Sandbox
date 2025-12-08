// WireExampleUtils.h
#pragma once

#include <filesystem>
#include <iostream>
#include <sstream>
#include <string>
#include <system_error>

namespace WireExampleUtils
{
    namespace fs = std::filesystem;

    // Strip path + extension -> base name ("W_08_12_2025_11_51" from full path)
    inline std::string base_name_from_path(const std::string& fullPath)
    {
        std::string filename = fullPath;

        // Strip directory
        std::size_t pos = filename.find_last_of("/\\");
        if (pos != std::string::npos)
            filename = filename.substr(pos + 1);

        // Strip extension
        std::size_t dotPos = filename.find_last_of('.');
        if (dotPos != std::string::npos)
            filename = filename.substr(0, dotPos);

        return filename;
    }

    // Core generator: given baseName and outputDir, find first free baseName_V_N
    inline std::string generate_unique_name(const std::string& baseName,
        const std::string& outputDir)
    {
        fs::path outDir = outputDir;

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

        int version = 1;
        for (;; ++version)
        {
            std::ostringstream nameStream;
            nameStream << baseName << "_V_" << version;
            const std::string candidateName = nameStream.str();

            fs::path candidatePath = outDir / (candidateName + ".mp4");
            if (!fs::exists(candidatePath))
            {
                return candidateName; // just the name, no path or extension
            }
        }
    }
}

// Convenience macro:
// - __FILE__ is expanded at *call site* (inside each example .h)
// - you still get per-example base names like W_08_12_2025_11_51
#define WIRE_UNIQUE_NAME(outputDirString) \
    WireExampleUtils::generate_unique_name( \
        WireExampleUtils::base_name_from_path(__FILE__), \
        (outputDirString))
