#pragma once

// LiteEngine: everything in one include.
//
//   #include "LiteEngine.h"
//
// What gets included follows engine_config.h (GL_TARGET_VERSION, IMGUI_ENABLED),
// so features the target GL version lacks are left out automatically. To trim
// compile times, define any of these before including:
//
//   LITEENGINE_NO_2D               skip the 2D camera, lights and renderer
//   LITEENGINE_NO_3D               skip the 3D camera, lights and renderer
//   LITEENGINE_NO_MARCHING_CUBES   skip marching cubes and Transvoxel
//
// The individual headers ("window.h", "renderer_3D.h", ...) still work on their own.

#include "engine_config.h"

// ---- core ----
#include "logs.h"
#include "paths.h"
#include "platform.h"
#include "timer.h"
#include "input.h"
#include "window.h"
#include "app.h"
#include "buffer.h"
#include "vertex_array.h"
#include "shader.h"
#include "texture.h"
#include "framebuffer.h"
#include "camera_core.h"

#if IMGUI_ENABLED
    #include "imgui_extention.h"
#endif

// ---- rendering (needs vertex arrays, GL 3.0+) ----
#if ENGINE_HAS_VERTEX_ARRAY
    #include "mesh.h"
    #include "light_core.h"
    #include "renderer_core.h"

    #if !defined(LITEENGINE_NO_2D)
        #include "camera_2D.h"
        #include "light_2D.h"
        #include "renderer_2D.h"
    #endif

    #if !defined(LITEENGINE_NO_3D)
        #include "camera_3D.h"
        #include "light_3D.h"
        #include "renderer_3D.h"
    #endif

    #if !defined(LITEENGINE_NO_MARCHING_CUBES)
        #include "marching_cubes.h"
    #endif
#endif