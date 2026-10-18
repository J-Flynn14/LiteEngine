cmake_minimum_required(VERSION 3.28.3)
project(LiteEngine VERSION 1.0.0 LANGUAGES C CXX)
cmake_policy(SET CMP0072 NEW)

# ---------------------------------------------------------------------------
# LiteEngine never downloads anything. Its dependencies come from the
# project being built:
#
#   * built on its own (cmake -B build):   LiteEngine/dependencies/
#   * inside a game (add_subdirectory or FetchContent with SOURCE_DIR):
#                                          <game>/dependencies/
#
# Expected layout of that folder (only what you don't already provide):
#
#   dependencies/
#     glm/glm/glm.hpp            GLM headers (0.9.9 or newer)
#     glfw/CMakeLists.txt        GLFW source tree
#     imgui/imgui.cpp            Dear ImGui, backends in imgui/ or imgui/backends/
#     stb_image/stb_image.h      or stb/stb_image.h (the stb repository)
#     glad/gl.h                  optional: falls back to LiteEngine's own glad
#
# For each dependency LiteEngine uses, in order: a target the game already
# created, the dependencies folder, then an installed package (glm, glfw).
# Anything still missing stops configuration with a message saying how to
# provide it. Override the folder with -DLITEENGINE_DEPENDENCIES_DIR=<path>.
#
# In a game:
#   add_subdirectory(external/LiteEngine)
#   target_link_libraries(MyGame PRIVATE LiteEngine::LiteEngine)
# ---------------------------------------------------------------------------

set(LITEENGINE_IS_TOP_LEVEL ${PROJECT_IS_TOP_LEVEL})

set(LITEENGINE_DEPENDENCIES_DIR "${CMAKE_SOURCE_DIR}/dependencies" CACHE PATH
    "Folder holding LiteEngine's dependencies; defaults to the top-level project's dependencies/")
set(LITEENGINE_OWN_DEPENDENCIES "${CMAKE_CURRENT_SOURCE_DIR}/dependencies")

option(LITEENGINE_BUILD_DEMO "Build the LiteEngine demo executable" ${LITEENGINE_IS_TOP_LEVEL})
option(LITEENGINE_WITH_IMGUI "Build and link Dear ImGui (sets IMGUI_ENABLED)" ON)
option(LITEENGINE_STB_IMPLEMENTATION
    "Compile stb_image's implementation; turn off if the game already defines STB_IMAGE_IMPLEMENTATION" ON)
option(LITEENGINE_ENABLE_LTO "Link-time optimisation for Release and RelWithDebInfo" ${LITEENGINE_IS_TOP_LEVEL})
option(LITEENGINE_ENABLE_PROFILING "Keep frame pointers for profilers (RelWithDebInfo)" OFF)
set(LITEENGINE_GL_TARGET_VERSION "" CACHE STRING
    "OpenGL version the engine targets, e.g. 41 or 43. Empty uses the engine_config.h default.")
set(LITEENGINE_IMGUI_TARGET "" CACHE STRING
    "Existing ImGui target to use instead of building one (must include the GLFW and OpenGL3 backends)")
set(LITEENGINE_GLAD_TARGET "" CACHE STRING
    "Existing glad target to share with the game, so the program has one GL loader")
set(LITEENGINE_IMGUI_MIN_VERSION 19280 CACHE STRING
    "Oldest IMGUI_VERSION_NUM LiteEngine is tested with; older versions only warn")

# Older presets and scripts pass these names; keep honouring them.
if(ENABLE_PROFILING)
    set(LITEENGINE_ENABLE_PROFILING ON)
endif()
if(DEFINED GL_TARGET_VERSION AND NOT GL_TARGET_VERSION STREQUAL "" AND LITEENGINE_GL_TARGET_VERSION STREQUAL "")
    set(LITEENGINE_GL_TARGET_VERSION ${GL_TARGET_VERSION})
endif()

if(LITEENGINE_IS_TOP_LEVEL)
    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

set(LITEENGINE_DEPS ${LITEENGINE_DEPENDENCIES_DIR})
message(STATUS "LiteEngine dependencies folder: ${LITEENGINE_DEPS}")

# Collects every missing dependency so one configure run reports them all.
set(LITEENGINE_MISSING "")
macro(liteengine_missing name how)
    string(APPEND LITEENGINE_MISSING "\n  * ${name}: ${how}")
endmacro()

# ---------------------------------------------------------------------------
# Warnings: applied only to LiteEngine's own executables, never to the game.
# ---------------------------------------------------------------------------

add_library(liteengine_warnings INTERFACE)
if(MSVC)
    target_compile_options(liteengine_warnings INTERFACE /W4 /permissive-)
    if(NOT CMAKE_CROSSCOMPILING)
        target_compile_options(liteengine_warnings INTERFACE /WX)
    endif()
else()
    target_compile_options(liteengine_warnings INTERFACE -Wall -Wextra -Wpedantic)
    if(NOT CMAKE_CROSSCOMPILING)
        target_compile_options(liteengine_warnings INTERFACE -Werror -Werror=narrowing)
    endif()
endif()

# ---------------------------------------------------------------------------
# OpenGL (system)
# ---------------------------------------------------------------------------

find_package(OpenGL REQUIRED)

# ---------------------------------------------------------------------------
# GLM: existing target, dependencies folder, installed package
# ---------------------------------------------------------------------------

if(TARGET glm::glm)
    set(LITEENGINE_GLM glm::glm)
    message(STATUS "LiteEngine glm: existing target glm::glm")
elseif(TARGET glm)
    set(LITEENGINE_GLM glm)
    message(STATUS "LiteEngine glm: existing target glm")
else()
    unset(LITEENGINE_GLM_INCLUDE)
    if(EXISTS ${LITEENGINE_DEPS}/glm/glm/glm.hpp)
        set(LITEENGINE_GLM_INCLUDE ${LITEENGINE_DEPS}/glm)
    elseif(EXISTS ${LITEENGINE_DEPS}/glm/glm.hpp)
        set(LITEENGINE_GLM_INCLUDE ${LITEENGINE_DEPS})
    endif()

    if(LITEENGINE_GLM_INCLUDE)
        add_library(liteengine_glm INTERFACE)
        target_include_directories(liteengine_glm SYSTEM INTERFACE ${LITEENGINE_GLM_INCLUDE})
        set(LITEENGINE_GLM liteengine_glm)
        message(STATUS "LiteEngine glm: ${LITEENGINE_GLM_INCLUDE}")
    else()
        find_package(glm CONFIG QUIET)
        if(TARGET glm::glm)
            set(LITEENGINE_GLM glm::glm)
            message(STATUS "LiteEngine glm: installed package ${glm_VERSION}")
        else()
            liteengine_missing("GLM 0.9.9+" "put it at ${LITEENGINE_DEPS}/glm (so glm/glm/glm.hpp exists), install it, or create a glm::glm target first")
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# GLFW: existing target, source in the dependencies folder, installed package
# ---------------------------------------------------------------------------

set(GLFW_MIN_VERSION 3.0 CACHE STRING "Oldest installed GLFW accepted; the engine guards from 3.0 up")

if(TARGET glfw)
    message(STATUS "LiteEngine glfw: existing target")
elseif(EXISTS ${LITEENGINE_DEPS}/glfw/CMakeLists.txt)
    set(GLFW_BUILD_DOCS     OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL        OFF CACHE BOOL "" FORCE)
    add_subdirectory(${LITEENGINE_DEPS}/glfw ${CMAKE_CURRENT_BINARY_DIR}/glfw)
    message(STATUS "LiteEngine glfw: ${LITEENGINE_DEPS}/glfw")
else()
    find_package(glfw3 ${GLFW_MIN_VERSION} QUIET)
    if(TARGET glfw)
        message(STATUS "LiteEngine glfw: installed package ${glfw3_VERSION}")
    else()
        liteengine_missing("GLFW ${GLFW_MIN_VERSION}+" "put its source at ${LITEENGINE_DEPS}/glfw (with its CMakeLists.txt), install it, or create a glfw target first")
    endif()
endif()

# ---------------------------------------------------------------------------
# glad: shared target, dependencies folder, then LiteEngine's own copy.
# glad is generated for the engine's GL version, so LiteEngine keeps its own
# as a fallback. Never let two copies into one program (duplicate symbols):
# a game that has its own glad passes it with LITEENGINE_GLAD_TARGET.
# ---------------------------------------------------------------------------

if(LITEENGINE_GLAD_TARGET)
    if(NOT TARGET ${LITEENGINE_GLAD_TARGET})
        message(FATAL_ERROR "LITEENGINE_GLAD_TARGET is '${LITEENGINE_GLAD_TARGET}', but no such target exists")
    endif()
    set(LITEENGINE_GLAD ${LITEENGINE_GLAD_TARGET})
    message(STATUS "LiteEngine glad: using ${LITEENGINE_GLAD_TARGET}")
else()
    unset(LITEENGINE_GLAD_INCLUDE)
    unset(LITEENGINE_GLAD_ROOT)
    foreach(root ${LITEENGINE_DEPS} ${LITEENGINE_OWN_DEPENDENCIES})
        foreach(candidate ${root} ${root}/glad/include ${root}/include)
            if(EXISTS ${candidate}/glad/gl.h OR EXISTS ${candidate}/glad/glad.h)
                set(LITEENGINE_GLAD_INCLUDE ${candidate})
                set(LITEENGINE_GLAD_ROOT ${root})
                break()
            endif()
        endforeach()
        if(LITEENGINE_GLAD_INCLUDE)
            break()
        endif()
    endforeach()

    if(NOT LITEENGINE_GLAD_INCLUDE)
        liteengine_missing("glad" "put glad/gl.h (glad 2) or glad/glad.h (glad 1) under ${LITEENGINE_DEPS}, or set LITEENGINE_GLAD_TARGET")
    else()
        if(EXISTS ${LITEENGINE_GLAD_INCLUDE}/glad/gl.h)
            set(LITEENGINE_GLAD_API 2)
            set(LITEENGINE_GLAD_HEADER ${LITEENGINE_GLAD_INCLUDE}/glad/gl.h)
            set(LITEENGINE_GLAD_HEADER_NAME "glad/gl.h")
            set(LITEENGINE_GLAD_IMPL_MACRO GLAD_GL_IMPLEMENTATION)
        else()
            set(LITEENGINE_GLAD_API 1)
            set(LITEENGINE_GLAD_HEADER ${LITEENGINE_GLAD_INCLUDE}/glad/glad.h)
            set(LITEENGINE_GLAD_HEADER_NAME "glad/glad.h")
            set(LITEENGINE_GLAD_IMPL_MACRO GLAD_IMPLEMENTATION)
        endif()

        file(GLOB_RECURSE LITEENGINE_GLAD_SOURCES CONFIGURE_DEPENDS
            ${LITEENGINE_GLAD_ROOT}/glad/*.c
            ${LITEENGINE_GLAD_ROOT}/src/*.c)
        file(STRINGS ${LITEENGINE_GLAD_HEADER} LITEENGINE_GLAD_IMPL_GUARD
             REGEX "${LITEENGINE_GLAD_IMPL_MACRO}" LIMIT_COUNT 1)

        if(NOT LITEENGINE_GLAD_SOURCES)
            if(NOT LITEENGINE_GLAD_IMPL_GUARD)
                message(FATAL_ERROR
                    "Found ${LITEENGINE_GLAD_HEADER} but no glad source, and the header has no "
                    "${LITEENGINE_GLAD_IMPL_MACRO} block. Re-generate glad, or place its .c under "
                    "${LITEENGINE_GLAD_ROOT}/glad/ or ${LITEENGINE_GLAD_ROOT}/src/.")
            endif()
            set(LITEENGINE_GLAD_IMPL ${CMAKE_CURRENT_BINARY_DIR}/liteengine_glad_impl.c)
            file(CONFIGURE OUTPUT ${LITEENGINE_GLAD_IMPL}
                CONTENT "#define ${LITEENGINE_GLAD_IMPL_MACRO}\n#include <${LITEENGINE_GLAD_HEADER_NAME}>\n")
            set(LITEENGINE_GLAD_SOURCES ${LITEENGINE_GLAD_IMPL})
        endif()

        add_library(liteengine_glad STATIC ${LITEENGINE_GLAD_SOURCES})
        target_include_directories(liteengine_glad SYSTEM PUBLIC ${LITEENGINE_GLAD_INCLUDE})
        target_compile_definitions(liteengine_glad PUBLIC ENGINE_GLAD_VERSION=${LITEENGINE_GLAD_API})
        set_target_properties(liteengine_glad PROPERTIES POSITION_INDEPENDENT_CODE ON)
        add_library(LiteEngine::glad ALIAS liteengine_glad)
        set(LITEENGINE_GLAD liteengine_glad)

        if(LITEENGINE_GLAD_ROOT STREQUAL LITEENGINE_DEPS)
            message(STATUS "LiteEngine glad ${LITEENGINE_GLAD_API}.x: ${LITEENGINE_GLAD_INCLUDE}")
        else()
            message(STATUS "LiteEngine glad ${LITEENGINE_GLAD_API}.x: LiteEngine's own copy")
        endif()
    endif()
endif()

# ---------------------------------------------------------------------------
# stb_image: the engine includes <stb_image/stb_image.h>. Accepts the
# dependencies/stb_image/ layout directly, or the stb repository
# (dependencies/stb/stb_image.h) through a generated include shim.
# ---------------------------------------------------------------------------

if(EXISTS ${LITEENGINE_DEPS}/stb_image/stb_image.h)
    set(LITEENGINE_STB_INCLUDE ${LITEENGINE_DEPS})
    set(LITEENGINE_STB_HEADER ${LITEENGINE_DEPS}/stb_image/stb_image.h)
elseif(EXISTS ${LITEENGINE_DEPS}/stb/stb_image.h)
    set(LITEENGINE_STB_INCLUDE ${CMAKE_CURRENT_BINARY_DIR}/liteengine_stb_shim)
    set(LITEENGINE_STB_HEADER ${LITEENGINE_DEPS}/stb/stb_image.h)
    file(CONFIGURE OUTPUT ${LITEENGINE_STB_INCLUDE}/stb_image/stb_image.h
        CONTENT "#pragma once\n#include \"${LITEENGINE_STB_HEADER}\"\n")
else()
    unset(LITEENGINE_STB_HEADER)
    liteengine_missing("stb_image" "put stb_image.h at ${LITEENGINE_DEPS}/stb_image/stb_image.h (or the stb repository at ${LITEENGINE_DEPS}/stb)")
endif()

if(LITEENGINE_STB_HEADER)
    if(LITEENGINE_STB_IMPLEMENTATION)
        set(LITEENGINE_STB_IMPL ${CMAKE_CURRENT_BINARY_DIR}/liteengine_stb_image_impl.c)
        file(CONFIGURE OUTPUT ${LITEENGINE_STB_IMPL}
            CONTENT "#define STB_IMAGE_IMPLEMENTATION\n#include <stb_image/stb_image.h>\n")
        add_library(liteengine_stb_image STATIC ${LITEENGINE_STB_IMPL})
        target_include_directories(liteengine_stb_image SYSTEM PUBLIC ${LITEENGINE_STB_INCLUDE})
        set_target_properties(liteengine_stb_image PROPERTIES POSITION_INDEPENDENT_CODE ON)
    else()
        add_library(liteengine_stb_image INTERFACE)
        target_include_directories(liteengine_stb_image SYSTEM INTERFACE ${LITEENGINE_STB_INCLUDE})
    endif()
    add_library(LiteEngine::stb_image ALIAS liteengine_stb_image)
    message(STATUS "LiteEngine stb_image: ${LITEENGINE_STB_HEADER}")
endif()

# ---------------------------------------------------------------------------
# Dear ImGui: existing target, or built from the dependencies folder with the
# GLFW and OpenGL3 backends (in imgui/ or imgui/backends/).
# ---------------------------------------------------------------------------

if(LITEENGINE_WITH_IMGUI)
    if(LITEENGINE_IMGUI_TARGET)
        if(NOT TARGET ${LITEENGINE_IMGUI_TARGET})
            message(FATAL_ERROR "LITEENGINE_IMGUI_TARGET is '${LITEENGINE_IMGUI_TARGET}', but no such target exists")
        endif()
        set(LITEENGINE_IMGUI ${LITEENGINE_IMGUI_TARGET})
        message(STATUS "LiteEngine imgui: using ${LITEENGINE_IMGUI_TARGET}")
    elseif(EXISTS ${LITEENGINE_DEPS}/imgui/imgui.cpp)
        set(imgui_root ${LITEENGINE_DEPS}/imgui)
        if(EXISTS ${imgui_root}/imgui_impl_glfw.cpp)
            set(imgui_backends ${imgui_root})
        elseif(EXISTS ${imgui_root}/backends/imgui_impl_glfw.cpp)
            set(imgui_backends ${imgui_root}/backends)
        else()
            unset(imgui_backends)
            liteengine_missing("ImGui backends" "imgui_impl_glfw.cpp and imgui_impl_opengl3.cpp were not found in ${imgui_root} or ${imgui_root}/backends")
        endif()

        file(STRINGS ${imgui_root}/imgui.h imgui_version_line REGEX "#define IMGUI_VERSION_NUM" LIMIT_COUNT 1)
        string(REGEX MATCH "[0-9]+" imgui_version "${imgui_version_line}")
        if(imgui_version AND imgui_version LESS LITEENGINE_IMGUI_MIN_VERSION)
            message(WARNING "LiteEngine is tested with ImGui ${LITEENGINE_IMGUI_MIN_VERSION}+, found ${imgui_version} in ${imgui_root}")
        endif()

        if(imgui_backends)
            add_library(liteengine_imgui STATIC
                ${imgui_root}/imgui.cpp
                ${imgui_root}/imgui_draw.cpp
                ${imgui_root}/imgui_widgets.cpp
                ${imgui_root}/imgui_tables.cpp
                ${imgui_root}/imgui_demo.cpp
                ${imgui_backends}/imgui_impl_glfw.cpp
                ${imgui_backends}/imgui_impl_opengl3.cpp)
            target_include_directories(liteengine_imgui SYSTEM PUBLIC ${imgui_root} ${imgui_backends})
            if(TARGET glfw)
                target_link_libraries(liteengine_imgui PUBLIC glfw)
            endif()
            target_compile_features(liteengine_imgui PUBLIC cxx_std_11)
            set_target_properties(liteengine_imgui PROPERTIES POSITION_INDEPENDENT_CODE ON)
            add_library(LiteEngine::imgui ALIAS liteengine_imgui)
            set(LITEENGINE_IMGUI liteengine_imgui)
            message(STATUS "LiteEngine imgui ${imgui_version}: ${imgui_root}")
        endif()
    else()
        liteengine_missing("Dear ImGui" "put it at ${LITEENGINE_DEPS}/imgui, set LITEENGINE_IMGUI_TARGET, or configure with -DLITEENGINE_WITH_IMGUI=OFF")
    endif()
endif()

# ---------------------------------------------------------------------------
# Stop here, listing everything that is missing at once.
# ---------------------------------------------------------------------------

if(LITEENGINE_MISSING)
    message(FATAL_ERROR
        "LiteEngine is missing dependencies (it does not download them):${LITEENGINE_MISSING}\n"
        "Dependencies folder searched: ${LITEENGINE_DEPS}\n"
        "Use a different folder with -DLITEENGINE_DEPENDENCIES_DIR=<path>.")
endif()

# ---------------------------------------------------------------------------
# Engine targets (header-only)
# ---------------------------------------------------------------------------

set(LITEENGINE_SRC ${CMAKE_CURRENT_SOURCE_DIR}/src)

# core: window, app, timer, input, shaders, buffers, textures, cameras.
# window.h includes imgui_extention.h when IMGUI_ENABLED, so core carries both.
add_library(liteengine_core INTERFACE)
target_include_directories(liteengine_core INTERFACE
    ${LITEENGINE_SRC}/core
    ${LITEENGINE_SRC}/core/error_handling
    ${LITEENGINE_SRC}/core/camera
    ${LITEENGINE_SRC}/imgui_extention)
target_link_libraries(liteengine_core INTERFACE
    ${LITEENGINE_GLM} ${LITEENGINE_GLAD} glfw liteengine_stb_image OpenGL::GL)
target_compile_features(liteengine_core INTERFACE cxx_std_17)
target_compile_definitions(liteengine_core INTERFACE GLFW_INCLUDE_NONE)

if(LITEENGINE_WITH_IMGUI)
    target_link_libraries(liteengine_core INTERFACE ${LITEENGINE_IMGUI})
    target_compile_definitions(liteengine_core INTERFACE IMGUI_ENABLED=1)
else()
    target_compile_definitions(liteengine_core INTERFACE IMGUI_ENABLED=0)
endif()

if(WIN32)
    target_compile_definitions(liteengine_core INTERFACE _WIN32_WINNT=0x0601)
endif()

if(NOT LITEENGINE_GL_TARGET_VERSION STREQUAL "")
    target_compile_definitions(liteengine_core INTERFACE GL_TARGET_VERSION=${LITEENGINE_GL_TARGET_VERSION})
    message(STATUS "LiteEngine GL target version: ${LITEENGINE_GL_TARGET_VERSION}")
else()
    message(STATUS "LiteEngine GL target version: engine_config.h default")
endif()

if(LITEENGINE_ENABLE_PROFILING AND NOT MSVC)
    target_compile_options(liteengine_core INTERFACE -fno-omit-frame-pointer)
endif()

add_library(LiteEngine::core ALIAS liteengine_core)

# imgui_extention: kept as its own target for existing build files; core carries it.
add_library(liteengine_imgui_extention INTERFACE)
target_link_libraries(liteengine_imgui_extention INTERFACE liteengine_core)
add_library(LiteEngine::imgui_extention ALIAS liteengine_imgui_extention)

# rendering: 2D and 3D renderers, lights, meshes, marching cubes
add_library(liteengine_rendering INTERFACE)
target_include_directories(liteengine_rendering INTERFACE
    ${LITEENGINE_SRC}/rendering
    ${LITEENGINE_SRC}/rendering/meshes
    ${LITEENGINE_SRC}/rendering/lights)
target_link_libraries(liteengine_rendering INTERFACE liteengine_core)
add_library(LiteEngine::rendering ALIAS liteengine_rendering)

# LiteEngine::LiteEngine: everything. This is the one most games want.
add_library(liteengine INTERFACE)
target_include_directories(liteengine INTERFACE ${LITEENGINE_SRC})   # LiteEngine.h
target_link_libraries(liteengine INTERFACE liteengine_core liteengine_rendering)
add_library(LiteEngine::LiteEngine ALIAS liteengine)

# ---------------------------------------------------------------------------
# Runtime data: copies a folder (shaders/, assets/) next to an executable.
# Games can call this too: liteengine_stage_runtime_directory(MyGame assets)
# ---------------------------------------------------------------------------

function(liteengine_stage_runtime_directory target dirname)
    cmake_parse_arguments(STAGE "" "SOURCE_DIR" "" ${ARGN})
    if(STAGE_SOURCE_DIR)
        set(source_dir ${STAGE_SOURCE_DIR})
    else()
        set(source_dir ${CMAKE_CURRENT_SOURCE_DIR}/${dirname})
    endif()

    if(NOT IS_DIRECTORY ${source_dir})
        message(STATUS "runtime data: ${dirname}/ not present for ${target}, skipping")
        return()
    endif()

    file(GLOB_RECURSE staged_files CONFIGURE_DEPENDS ${source_dir}/*)
    list(FILTER staged_files EXCLUDE REGEX "Zone\\.Identifier$")
    if(NOT staged_files)
        message(STATUS "runtime data: ${dirname}/ has no files to copy")
        return()
    endif()

    set(commands "")
    foreach(staged_file ${staged_files})
        file(RELATIVE_PATH relative ${source_dir} ${staged_file})
        list(APPEND commands
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                    ${staged_file}
                    $<TARGET_FILE_DIR:${target}>/${dirname}/${relative})
    endforeach()

    list(LENGTH staged_files staged_count)
    add_custom_target(${target}_stage_${dirname} ALL
        ${commands}
        COMMENT "Staging ${staged_count} file(s) from ${dirname}/ for ${target}"
        VERBATIM)
    add_dependencies(${target} ${target}_stage_${dirname})
    message(STATUS "runtime data: ${dirname}/ for ${target} (${staged_count} file(s))")
endfunction()

# ---------------------------------------------------------------------------
# Demo: only built when LiteEngine is the top-level project (or asked for)
# ---------------------------------------------------------------------------

if(LITEENGINE_BUILD_DEMO)
    add_executable(LiteEngineDemo ${LITEENGINE_SRC}/main.cpp)
    target_include_directories(LiteEngineDemo PRIVATE ${LITEENGINE_SRC})
    target_link_libraries(LiteEngineDemo PRIVATE LiteEngine::LiteEngine liteengine_warnings)
    set_target_properties(LiteEngineDemo PROPERTIES
        OUTPUT_NAME LiteEngine
        CXX_EXTENSIONS OFF)

    if(MINGW)
        target_link_options(LiteEngineDemo PRIVATE -static -static-libgcc -static-libstdc++)
    endif()

    if(LITEENGINE_ENABLE_LTO)
        include(CheckIPOSupported)
        check_ipo_supported(RESULT liteengine_lto_supported OUTPUT liteengine_lto_error)
        if(liteengine_lto_supported)
            set_target_properties(LiteEngineDemo PROPERTIES
                INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE
                INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
            message(STATUS "LiteEngine demo: LTO enabled for Release builds")
        else()
            message(WARNING "LTO not supported: ${liteengine_lto_error}")
        endif()
    endif()

    if(LITEENGINE_ENABLE_PROFILING AND NOT CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
        message(WARNING "LITEENGINE_ENABLE_PROFILING is intended for RelWithDebInfo; current build type: ${CMAKE_BUILD_TYPE}")
    endif()

    liteengine_stage_runtime_directory(LiteEngineDemo shaders)
    liteengine_stage_runtime_directory(LiteEngineDemo assets)
endif()