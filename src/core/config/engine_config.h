#pragma once
#include <cstdint>

#ifndef GL_TARGET_VERSION
    #define GL_TARGET_VERSION 41
#endif
#define GLFW_TARGET_VERSION (GLFW_VERSION_MAJOR * 10 + GLFW_VERSION_MINOR)

#if !defined(IMGUI_ENABLED)
    #define IMGUI_ENABLED 1
#endif

#if defined(_MSVC_LANG)
    #define ENGINE_CPLUSPLUS _MSVC_LANG
#else
    #define ENGINE_CPLUSPLUS __cplusplus
#endif
#if ENGINE_CPLUSPLUS < 201703L
    #error "C++17 or later required"
#endif

#ifndef ENGINE_GLAD_VERSION
    #if __has_include(<glad/gl.h>)
        #define ENGINE_GLAD_VERSION 2
    #elif __has_include(<glad/glad.h>)
        #define ENGINE_GLAD_VERSION 1
    #else
        #error "No glad header found; expected glad/glad.h (glad 1.x) or glad/gl.h (glad 2.x)"
    #endif
#endif

#if ENGINE_GLAD_VERSION == 2
    #include <glad/gl.h>
#else
    #include <glad/glad.h>
#endif

#ifndef GLFW_INCLUDE_NONE
    #define GLFW_INCLUDE_NONE
#endif
#if __has_include(<GLFW/glfw3.h>)
    #include <GLFW/glfw3.h>
#else
    #error "No glfw header found, expected GLFW/glfw3.h (glfw 3.0+)"
#endif

#if __has_include(<glm/detail/setup.hpp>)
    #include <glm/detail/setup.hpp>
    #define ENGINE_GLM_VERSION (GLM_VERSION_MAJOR * 10000 + GLM_VERSION_MINOR * 100 + GLM_VERSION_PATCH)
    static_assert(ENGINE_GLM_VERSION >= 909, "GLM 0.9.9 or newer is required");
    #if defined(GLM_FORCE_DEPTH_ZERO_TO_ONE) || defined(GLM_FORCE_LEFT_HANDED)
        #error "Engine math assumes GL conventions: -1..1 clip depth, right-handed"
    #endif
#else
    #error "No glm header found, expected glm/detail/setup.hpp (glm 0.9.9+)"
#endif

#define ENGINE_GLM_BUG_QUAT_ROTATE_REVERSED (ENGINE_GLM_VERSION == 10002)

#if defined(__APPLE__)
    #define ENGINE_PLATFORM_MAX_GL 41
#else
    #define ENGINE_PLATFORM_MAX_GL 46
#endif

#if GL_TARGET_VERSION > ENGINE_PLATFORM_MAX_GL
    #if defined(__APPLE__)
        #error "macOS supports OpenGL 4.1 at most; build with -DGL_TARGET_VERSION=41"
    #else
        #error "GL_TARGET_VERSION exceeds 46, the highest OpenGL version that exists"
    #endif
#endif

#ifndef GLSL_TARGET_VERSION
    #if   GL_TARGET_VERSION >= 33
        #define GLSL_TARGET_VERSION (GL_TARGET_VERSION * 10)
    #elif GL_TARGET_VERSION == 32
        #define GLSL_TARGET_VERSION 150
    #elif GL_TARGET_VERSION == 31
        #define GLSL_TARGET_VERSION 140
    #elif GL_TARGET_VERSION == 30
        #define GLSL_TARGET_VERSION 130
    #elif GL_TARGET_VERSION == 21
        #define GLSL_TARGET_VERSION 120
    #elif GL_TARGET_VERSION == 20
        #define GLSL_TARGET_VERSION 110
    #else
        #define GLSL_TARGET_VERSION 0
    #endif
#endif

#if GLSL_TARGET_VERSION == 0 && GL_TARGET_VERSION >= 20
    #error "GLSL_TARGET_VERSION could not be derived; define it explicitly"
#endif

// ---- shading ----
#define ENGINE_HAS_VERTEX_SHADER    (GL_TARGET_VERSION >= 20)
#define ENGINE_HAS_FRAGMENT_SHADER  (GL_TARGET_VERSION >= 20)
#define ENGINE_HAS_GEOMETRY_SHADER  (GL_TARGET_VERSION >= 32)
#define ENGINE_HAS_TESS_SHADER      (GL_TARGET_VERSION >= 40)
#define ENGINE_HAS_COMPUTE_SHADER   (GL_TARGET_VERSION >= 43)
#define ENGINE_HAS_UNIFORM_BLOCKS   (GL_TARGET_VERSION >= 31)
#define ENGINE_HAS_PROGRAM_BINARY   (GL_TARGET_VERSION >= 41)
#define ENGINE_HAS_SEPARATE_PROGRAM (GL_TARGET_VERSION >= 41)
#define ENGINE_HAS_PROGRAM_UNIFORM  (GL_TARGET_VERSION >= 41)
#define ENGINE_HAS_SUBROUTINES      (GL_TARGET_VERSION >= 40)
#define ENGINE_HAS_MEMORY_BARRIER   (GL_TARGET_VERSION >= 42)
#define ENGINE_HAS_SSBO             (GL_TARGET_VERSION >= 43)
#define ENGINE_HAS_PROGRAM_RESOURCE (GL_TARGET_VERSION >= 43)
#define ENGINE_HAS_SPIRV            (GL_TARGET_VERSION >= 46)
#define ENGINE_HAS_UINT_UNIFORM     (GL_TARGET_VERSION >= 30)

// ---- buffers / vertex ----
#define ENGINE_HAS_VBO              (GL_TARGET_VERSION >= 15)
#define ENGINE_HAS_VERTEX_ARRAY     (GL_TARGET_VERSION >= 30)
#define ENGINE_HAS_INTEGER_ATTRIB   (GL_TARGET_VERSION >= 30)
#define ENGINE_HAS_MAP_BUFFER_RANGE (GL_TARGET_VERSION >= 30)
#define ENGINE_HAS_BIND_BUFFER_BASE (GL_TARGET_VERSION >= 30)
#define ENGINE_HAS_TRANSFORM_FEEDBACK (GL_TARGET_VERSION >= 30)
#define ENGINE_HAS_COPY_BUFFER      (GL_TARGET_VERSION >= 31)
#define ENGINE_HAS_TEXTURE_BUFFER   (GL_TARGET_VERSION >= 31)
#define ENGINE_HAS_INSTANCED_DRAW   (GL_TARGET_VERSION >= 31)
#define ENGINE_HAS_INSTANCING       (GL_TARGET_VERSION >= 33)
#define ENGINE_HAS_ATOMIC_COUNTERS  (GL_TARGET_VERSION >= 42)
#define ENGINE_HAS_SEPARATE_ATTRIB  (GL_TARGET_VERSION >= 43)
#define ENGINE_HAS_BUFFER_STORAGE   (GL_TARGET_VERSION >= 44)
#define ENGINE_HAS_DSA              (GL_TARGET_VERSION >= 45)

// ---- drawing ----
#define ENGINE_HAS_DRAW_INDIRECT    (GL_TARGET_VERSION >= 40)
#define ENGINE_HAS_INVALIDATE       (GL_TARGET_VERSION >= 43)
#define ENGINE_HAS_MULTI_DRAW       (GL_TARGET_VERSION >= 14)
#define ENGINE_HAS_BASE_VERTEX      (GL_TARGET_VERSION >= 32)
#define ENGINE_HAS_DOUBLE_ATTRIB    (GL_TARGET_VERSION >= 41)
#define ENGINE_HAS_CLEAR_BUFFER     (GL_TARGET_VERSION >= 43)

// ---- textures ----
#define ENGINE_HAS_MULTITEXTURE     (GL_TARGET_VERSION >= 13)
#define ENGINE_HAS_MIPMAP           (GL_TARGET_VERSION >= 30)
#define ENGINE_HAS_TEXTURE_RG       (GL_TARGET_VERSION >= 30)
#define ENGINE_HAS_COMPARE_REF      (GL_TARGET_VERSION >= 30)
#define ENGINE_HAS_TEXTURE_STORAGE  (GL_TARGET_VERSION >= 42)
#define ENGINE_HAS_ANISOTROPY       (GL_TARGET_VERSION >= 46)
#define ENGINE_HAS_TEXTURE_MULTISAMPLE (GL_TARGET_VERSION >= 32)
#define ENGINE_HAS_TEXTURE_ARRAY    (GL_TARGET_VERSION >= 30)
#define ENGINE_HAS_TEXTURE_SWIZZLE  (GL_TARGET_VERSION >= 33)
#define ENGINE_HAS_COMPRESSED_RGTC  (GL_TARGET_VERSION >= 30)
#define ENGINE_HAS_COMPRESSED_BPTC  (GL_TARGET_VERSION >= 42)
#define ENGINE_HAS_COMPRESSED_ETC2  (GL_TARGET_VERSION >= 43)

// ---- framebuffers ----
#define ENGINE_HAS_FRAMEBUFFER      (GL_TARGET_VERSION >= 30)
#define ENGINE_HAS_BLIT_FRAMEBUFFER (GL_TARGET_VERSION >= 30)
#define ENGINE_HAS_MULTISAMPLE_RB   (GL_TARGET_VERSION >= 30)
#define ENGINE_HAS_LAYERED_ATTACH   (GL_TARGET_VERSION >= 32)
#define ENGINE_HAS_FB_PARAMETER     (GL_TARGET_VERSION >= 43)
#define ENGINE_HAS_FRAMEBUFFER_DSA  (GL_TARGET_VERSION >= 45)

// ---- context / GL state ----
#define ENGINE_HAS_MULTISAMPLE      (GL_TARGET_VERSION >= 13)
#define ENGINE_HAS_FORWARD_COMPAT   (GL_TARGET_VERSION >= 30)
#define ENGINE_HAS_CONTEXT_PROFILE  (GL_TARGET_VERSION >= 32)
#define ENGINE_HAS_DEBUG_OUTPUT     (GL_TARGET_VERSION >= 43)

#ifndef GLFW_TRUE
    #define GLFW_TRUE 1
#endif
#ifndef GLFW_FALSE
    #define GLFW_FALSE 0
#endif
#ifndef GLFW_DONT_CARE
    #define GLFW_DONT_CARE (-1)
#endif

