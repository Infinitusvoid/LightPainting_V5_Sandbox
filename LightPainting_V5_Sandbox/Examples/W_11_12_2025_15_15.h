#pragma once

#include "WireUtil.h"


#include <fstream>
#include <cstdint>

#include <cctype> // for std::toupper

#include "../External_libs/tinyply-master/tinyply-master/source/tinyply.h" // adjust path if needed
#include "../External_libs/tinyply-master/tinyply-master/source/tinyply.cpp" // adjust path if needed

using namespace WireEngine;



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
