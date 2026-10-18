#pragma once

// LiteEngine: everything in one include.
//
//   #include "LiteEngine/LiteEngine.h"
//
// What gets included follows engine_config.h (GL_TARGET_VERSION, IMGUI_ENABLED),
// so features the target GL version lacks are left out automatically. To trim
// compile times, define any of these before including:
//
//   LITEENGINE_NO_2D               skip the 2D camera, lights and renderer
//   LITEENGINE_NO_3D               skip the 3D camera, lights and renderer
//
// The individual headers ("LiteEngine/window.h", "LiteEngine/renderer_3D.h", ...)
// still work on their own.

#include "core/engine_config.h"

// ---- core ----
#include "core/logs.h"
#include "core/paths.h"
#include "core/platform.h"
#include "core/timer.h"
#include "core/input.h"
#include "core/window.h"
#include "core/app.h"
#include "core/buffer.h"
#include "core/vertex_array.h"
#include "core/shader.h"
#include "core/texture.h"
#include "core/framebuffer.h"
#include "core/camera/camera_core.h"

#if IMGUI_ENABLED
    #include "imgui_extention/imgui_extention.h"
#endif

// ---- rendering (needs vertex arrays, GL 3.0+) ----
#if ENGINE_HAS_VERTEX_ARRAY
    #include "rendering/meshes/mesh.h"
    #include "rendering/lights/light_core.h"
    #include "rendering/renderer_core.h"

    #if !defined(LITEENGINE_NO_2D)
        #include "core/camera/camera_2D.h"
        #include "rendering/lights/light_2D.h"
        #include "rendering/renderer_2D.h"
    #endif

    #if !defined(LITEENGINE_NO_3D)
        #include "core/camera/camera_3D.h"
        #include "rendering/lights/light_3D.h"
        #include "rendering/renderer_3D.h"
    #endif
#endif