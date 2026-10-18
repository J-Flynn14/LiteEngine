#pragma once
#include "engine_config.h"

#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <functional>
#include <cstdio>
#include <sstream>
#include <tuple>

#include "logs.h"
#include "input.h"
#include "platform.h"
#include "glfw_error_handler.h"
#include "gl_error_handler.h"
#include "timer.h"

// imgui_extention.h must not include camera.h, or this include becomes a cycle
// (camera.h includes window.h); its camera helpers are templates for that reason.
#if IMGUI_ENABLED
    #if __has_include(<imgui_impl_glfw.h>)
        #include <imgui_impl_glfw.h>
    #elif __has_include(<backends/imgui_impl_glfw.h>)
        #include <backends/imgui_impl_glfw.h>
    #else
        #error "IMGUI_ENABLED is on but imgui_impl_glfw.h was not found"
    #endif
    #include "imgui_extention.h"
#endif

#ifndef GL_TARGET_VERSION
    #error "GL_TARGET_VERSION not defined in engine_config.h"
#endif
#ifndef GLFW_TARGET_VERSION
    #error "GLFW_TARGET_VERSION not defined in engine_config.h"
#endif

//OpenGL and GLFW config structs and methdods

//OpenGL state variables
struct GLState {
    bool gl_enable_blend = false;
    int gl_blend_func_src = GL_ONE;
    int gl_blend_func_dst = GL_ONE_MINUS_SRC_ALPHA;

    bool gl_cull_face = false;
    int gl_cull_face_mode = GL_BACK;

    std::tuple<double, double> gl_depth_range = std::make_tuple(0.0, 1.0);
    bool gl_depth_test = true;
    int gl_depth_func = GL_LESS;

    bool gl_dither = true;

    bool gl_line_smooth = false;
    float gl_line_width = 1.0f;

    bool gl_polygon_smooth = false;

    bool gl_scissor_test = false;
    std::tuple<int, int, int, int> gl_scissor_box = std::make_tuple(0, 0, 0, 0);

    bool gl_stencil_test = false;
    std::tuple<int, int, uint32_t> gl_stencil_func = std::make_tuple(GL_ALWAYS, 0, 0xFFFFFFFFu);
    std::tuple<int, int, int> gl_stencil_op = std::make_tuple(GL_KEEP, GL_KEEP, GL_KEEP);

    #if GL_TARGET_VERSION >= 11
        bool gl_color_logic_op = false;
        int gl_logic_op = GL_COPY;

        bool gl_polygon_offset_fill = false;
        bool gl_polygon_offset_line = false;
        bool gl_polygon_offset_point = false;
        std::tuple<float, float> gl_polygon_offset = std::make_tuple(0.0f, 0.0f);
    #endif

    #if GL_TARGET_VERSION >= 13
        bool gl_multisample = false;
        bool gl_sample_alpha_to_coverage = false;
        bool gl_sample_alpha_to_one = false;
        bool gl_sample_coverage = false;
        std::tuple<float, bool> gl_sample_coverage_value = std::make_tuple(1.0f, false);
    #endif

    #if GL_TARGET_VERSION >= 30
        bool gl_framebuffer_srgb = false;
        bool gl_rasterizer_discard = false;
    #endif

    #if GL_TARGET_VERSION >= 31
        bool gl_primitives_restart = false;
        uint32_t gl_primitives_restart_index = 0;
    #endif

    #if GL_TARGET_VERSION >= 32
        bool gl_depth_clamp = false;
        bool gl_texture_cube_map_seamless = false;
        bool gl_program_point_size = false;
        bool gl_sample_mask = false;
        std::tuple<uint32_t, uint32_t> gl_sample_maski = std::make_tuple(0u, 0xFFFFFFFFu);
    #endif

    #if GL_TARGET_VERSION >= 40
        bool gl_sample_shading = false;
        float gl_min_sample_shading = 1.0f;
    #endif

    #if GL_TARGET_VERSION >= 43
        // Defaults on so GlErrorHandler is live from the first applied state.
        bool gl_debug_output = true;
        bool gl_debug_output_synchronous = true;
        bool gl_primitives_restart_fixed_index = false;
    #endif
};

//GLFW state variables, all set to default per documentation
struct GLFWState {

    //Window Hints
    bool glfw_window_resizable = true;
    bool glfw_window_visible = true;
    bool glfw_window_decorated = true;
    bool glfw_window_focused = true;
    #if GLFW_TARGET_VERSION >= 31
        bool glfw_window_auto_iconify = true;
        bool glfw_window_floating = false;
    #endif
    #if GLFW_TARGET_VERSION >= 32
        bool glfw_window_maximized = false;
    #endif
    #if GLFW_TARGET_VERSION >= 33
        bool glfw_window_center_cursor = true;
        bool glfw_window_transparent_framebuffer = false;
        bool glfw_window_focus_on_show = true;
        bool glfw_window_scale_to_monitor = false;
    #endif
    #if GLFW_TARGET_VERSION >= 34
        bool glfw_window_mouse_passthrough = false;
        bool glfw_window_scale_framebuffer = true;
        int glfw_window_position_x = GLFW_ANY_POSITION;
        int glfw_window_position_y = GLFW_ANY_POSITION;
    #endif

    //Framebuffer Hints
    int glfw_red_bits = GLFW_DONT_CARE;
    int glfw_green_bits = GLFW_DONT_CARE;
    int glfw_blue_bits = GLFW_DONT_CARE;
    int glfw_alpha_bits = GLFW_DONT_CARE;
    int glfw_depth_bits = GLFW_DONT_CARE;
    int glfw_stencil_bits = GLFW_DONT_CARE;
    int glfw_accum_red_bits = GLFW_DONT_CARE;
    int glfw_accum_green_bits = GLFW_DONT_CARE;
    int glfw_accum_blue_bits = GLFW_DONT_CARE;
    int glfw_accum_alpha_bits = GLFW_DONT_CARE;
    int glfw_aux_buffers = GLFW_DONT_CARE;
    bool glfw_stereo = false;
    int glfw_samples = GLFW_DONT_CARE;
    bool glfw_srgb_capable = false;
    bool glfw_doublebuffer = true;

    //Monitor Hints
    int glfw_refresh_rate = GLFW_DONT_CARE;

    //Context Hints
    int glfw_client_api = GLFW_OPENGL_API;
    int glfw_context_version_major = GL_TARGET_VERSION / 10;
    int glfw_context_version_minor = GL_TARGET_VERSION % 10;
    int glfw_context_robustness = GLFW_NO_ROBUSTNESS;
    bool glfw_opengl_debug_context = false;
    #if GL_TARGET_VERSION >= 30
        bool glfw_opengl_forward_compat = true;
    #else
        bool glfw_opengl_forward_compat = false;
    #endif
    #if GL_TARGET_VERSION >= 32
        int glfw_opengl_profile = GLFW_OPENGL_CORE_PROFILE;
    #else
        int glfw_opengl_profile = GLFW_OPENGL_ANY_PROFILE;
    #endif
    #if GLFW_TARGET_VERSION >= 31
        int glfw_context_release_behavior = GLFW_ANY_RELEASE_BEHAVIOR;
    #endif
    #if GLFW_TARGET_VERSION >= 32
        int glfw_context_creation_api = GLFW_NATIVE_CONTEXT_API;
        bool glfw_context_no_error = false;
    #endif

    //MacOS Specific Hints
    #ifdef __APPLE__
        #if GLFW_TARGET_VERSION >= 33
            bool glfw_cocoa_retina_framebuffer = true;
            bool glfw_cocoa_graphics_switching = false;
            std::string glfw_cocoa_frame_name = "";
        #endif
    #endif

    //X11 Specific Hints
    #if GLFW_TARGET_VERSION >= 33
        std::string glfw_x11_class_name = "";
        std::string glfw_x11_instance_name = "";
    #endif

    //Wayland Specific Hints
    #if GLFW_TARGET_VERSION >= 34
        std::string glfw_wayland_app_id = "";
    #endif

    //Win32 Specific Hints
    #ifdef _WIN32
        #if GLFW_TARGET_VERSION >= 34
            bool glfw_win32_keyboard_menu = false;
            bool glfw_win32_showdefault = false;
        #endif
    #endif

};

// Applies a GLState to the current context; debug output delegates to
// GlErrorHandler so exactly one callback is ever installed.
inline void apply_gl_state(const GLState& state) noexcept {
    if (state.gl_enable_blend) {
        glEnable(GL_BLEND);
        glBlendFunc(state.gl_blend_func_src, state.gl_blend_func_dst);
    } else {glDisable(GL_BLEND);}

    if (state.gl_cull_face) {
        glEnable(GL_CULL_FACE);
        glCullFace(state.gl_cull_face_mode);
    } else {glDisable(GL_CULL_FACE);}

    glDepthRange(std::get<0>(state.gl_depth_range), std::get<1>(state.gl_depth_range));
    if (state.gl_depth_test) {
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(state.gl_depth_func);
    } else {glDisable(GL_DEPTH_TEST);}

    if (state.gl_dither) {glEnable(GL_DITHER);}
    else {glDisable(GL_DITHER);}

    if (state.gl_line_smooth) {glEnable(GL_LINE_SMOOTH);}
    else {glDisable(GL_LINE_SMOOTH);}
    glLineWidth(state.gl_line_width);

    if (state.gl_polygon_smooth) {glEnable(GL_POLYGON_SMOOTH);}
    else {glDisable(GL_POLYGON_SMOOTH);}

    if (state.gl_scissor_test) {
        glEnable(GL_SCISSOR_TEST);
        const int sx = std::get<0>(state.gl_scissor_box);
        const int sy = std::get<1>(state.gl_scissor_box);
        const int sw = std::get<2>(state.gl_scissor_box);
        const int sh = std::get<3>(state.gl_scissor_box);
        glScissor(sx, sy, sw, sh);
    } else {glDisable(GL_SCISSOR_TEST);}

    if (state.gl_stencil_test) {
        glEnable(GL_STENCIL_TEST);
        glStencilFunc(std::get<0>(state.gl_stencil_func),
                        std::get<1>(state.gl_stencil_func),
                        std::get<2>(state.gl_stencil_func));
        glStencilOp(std::get<0>(state.gl_stencil_op),
                    std::get<1>(state.gl_stencil_op),
                    std::get<2>(state.gl_stencil_op));
    } else {glDisable(GL_STENCIL_TEST);}

    #if GL_TARGET_VERSION >= 11
        if (state.gl_color_logic_op) {
            glEnable(GL_COLOR_LOGIC_OP);
            glLogicOp(state.gl_logic_op);
        } else {glDisable(GL_COLOR_LOGIC_OP);}

        if (state.gl_polygon_offset_fill) {glEnable(GL_POLYGON_OFFSET_FILL);}
        else {glDisable(GL_POLYGON_OFFSET_FILL);}
        if (state.gl_polygon_offset_line) {glEnable(GL_POLYGON_OFFSET_LINE);}
        else {glDisable(GL_POLYGON_OFFSET_LINE);}
        if (state.gl_polygon_offset_point) {glEnable(GL_POLYGON_OFFSET_POINT);}
        else {glDisable(GL_POLYGON_OFFSET_POINT);}
        glPolygonOffset(std::get<0>(state.gl_polygon_offset), std::get<1>(state.gl_polygon_offset));
    #endif

    #if GL_TARGET_VERSION >= 13
        if (state.gl_multisample) {glEnable(GL_MULTISAMPLE);}
        else {glDisable(GL_MULTISAMPLE);}

        if (state.gl_sample_alpha_to_coverage) {glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);}
        else {glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);}

        if (state.gl_sample_alpha_to_one) {glEnable(GL_SAMPLE_ALPHA_TO_ONE);}
        else {glDisable(GL_SAMPLE_ALPHA_TO_ONE);}

        if (state.gl_sample_coverage) {
            glEnable(GL_SAMPLE_COVERAGE);
            glSampleCoverage(std::get<0>(state.gl_sample_coverage_value),
                            std::get<1>(state.gl_sample_coverage_value) ? GL_TRUE : GL_FALSE);
        } else {glDisable(GL_SAMPLE_COVERAGE);}
    #endif

    #if GL_TARGET_VERSION >= 30
        if (state.gl_framebuffer_srgb) {glEnable(GL_FRAMEBUFFER_SRGB);}
        else {glDisable(GL_FRAMEBUFFER_SRGB);}

        if (state.gl_rasterizer_discard) {glEnable(GL_RASTERIZER_DISCARD);}
        else {glDisable(GL_RASTERIZER_DISCARD);}
    #endif

    #if GL_TARGET_VERSION >= 31
        if (state.gl_primitives_restart) {
            glEnable(GL_PRIMITIVE_RESTART);
            glPrimitiveRestartIndex(state.gl_primitives_restart_index);
        } else {glDisable(GL_PRIMITIVE_RESTART);}
    #endif

    #if GL_TARGET_VERSION >= 32
        if (state.gl_depth_clamp) {glEnable(GL_DEPTH_CLAMP);}
        else {glDisable(GL_DEPTH_CLAMP);}

        if (state.gl_texture_cube_map_seamless) {glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);}
        else {glDisable(GL_TEXTURE_CUBE_MAP_SEAMLESS);}

        if (state.gl_program_point_size) {glEnable(GL_PROGRAM_POINT_SIZE);}
        else {glDisable(GL_PROGRAM_POINT_SIZE);}

        if (state.gl_sample_mask) {
            glEnable(GL_SAMPLE_MASK);
            glSampleMaski(std::get<0>(state.gl_sample_maski), std::get<1>(state.gl_sample_maski));
        } else {glDisable(GL_SAMPLE_MASK);}
    #endif

    #if GL_TARGET_VERSION >= 40
        if (state.gl_sample_shading) {
            glEnable(GL_SAMPLE_SHADING);
            glMinSampleShading(state.gl_min_sample_shading);
        } else {glDisable(GL_SAMPLE_SHADING);}
    #endif

    #if GL_TARGET_VERSION >= 43
        if (state.gl_primitives_restart_fixed_index) {glEnable(GL_PRIMITIVE_RESTART_FIXED_INDEX);}
        else {glDisable(GL_PRIMITIVE_RESTART_FIXED_INDEX);}

        if (state.gl_debug_output) {
            GlErrorHandler::install();
            if (state.gl_debug_output_synchronous) {glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);}
            else {glDisable(GL_DEBUG_OUTPUT_SYNCHRONOUS);}
        } else {
            GlErrorHandler::uninstall();
        }
    #endif
}

// Applies a GLFWState as window hints for the next glfwCreateWindow call.
inline void apply_glfw_state(const GLFWState& state) noexcept {
    glfwWindowHint(GLFW_RESIZABLE, state.glfw_window_resizable);
    glfwWindowHint(GLFW_VISIBLE, state.glfw_window_visible);
    glfwWindowHint(GLFW_DECORATED, state.glfw_window_decorated);
    glfwWindowHint(GLFW_FOCUSED, state.glfw_window_focused);
    #if GLFW_TARGET_VERSION >= 31
        glfwWindowHint(GLFW_AUTO_ICONIFY, state.glfw_window_auto_iconify);
        glfwWindowHint(GLFW_FLOATING, state.glfw_window_floating);
    #endif
    #if GLFW_TARGET_VERSION >= 32
        glfwWindowHint(GLFW_MAXIMIZED, state.glfw_window_maximized);
    #endif
    #if GLFW_TARGET_VERSION >= 33
        glfwWindowHint(GLFW_CENTER_CURSOR, state.glfw_window_center_cursor);
        glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, state.glfw_window_transparent_framebuffer);
        glfwWindowHint(GLFW_FOCUS_ON_SHOW, state.glfw_window_focus_on_show);
        glfwWindowHint(GLFW_SCALE_TO_MONITOR, state.glfw_window_scale_to_monitor);
    #endif
    glfwWindowHint(GLFW_RED_BITS, state.glfw_red_bits);
    glfwWindowHint(GLFW_GREEN_BITS, state.glfw_green_bits);
    glfwWindowHint(GLFW_BLUE_BITS, state.glfw_blue_bits);
    glfwWindowHint(GLFW_ALPHA_BITS, state.glfw_alpha_bits);
    glfwWindowHint(GLFW_DEPTH_BITS, state.glfw_depth_bits);
    glfwWindowHint(GLFW_STENCIL_BITS, state.glfw_stencil_bits);
    glfwWindowHint(GLFW_ACCUM_RED_BITS, state.glfw_accum_red_bits);
    glfwWindowHint(GLFW_ACCUM_GREEN_BITS, state.glfw_accum_green_bits);
    glfwWindowHint(GLFW_ACCUM_BLUE_BITS, state.glfw_accum_blue_bits);
    glfwWindowHint(GLFW_ACCUM_ALPHA_BITS, state.glfw_accum_alpha_bits);
    glfwWindowHint(GLFW_AUX_BUFFERS, state.glfw_aux_buffers);
    glfwWindowHint(GLFW_STEREO, state.glfw_stereo);
    glfwWindowHint(GLFW_SAMPLES, state.glfw_samples);
    glfwWindowHint(GLFW_SRGB_CAPABLE, state.glfw_srgb_capable);
    glfwWindowHint(GLFW_DOUBLEBUFFER, state.glfw_doublebuffer);
    glfwWindowHint(GLFW_REFRESH_RATE, state.glfw_refresh_rate);
    glfwWindowHint(GLFW_CLIENT_API, state.glfw_client_api);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, state.glfw_context_version_major);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, state.glfw_context_version_minor);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, state.glfw_opengl_forward_compat);
    glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, state.glfw_opengl_debug_context);
    glfwWindowHint(GLFW_OPENGL_PROFILE, state.glfw_opengl_profile);
    glfwWindowHint(GLFW_CONTEXT_ROBUSTNESS, state.glfw_context_robustness);
    #if GLFW_TARGET_VERSION >= 31
        glfwWindowHint(GLFW_CONTEXT_RELEASE_BEHAVIOR, state.glfw_context_release_behavior);
    #endif
    #if GLFW_TARGET_VERSION >= 32
        glfwWindowHint(GLFW_CONTEXT_CREATION_API, state.glfw_context_creation_api);
        glfwWindowHint(GLFW_CONTEXT_NO_ERROR, state.glfw_context_no_error);
    #endif
    #if GLFW_TARGET_VERSION >= 33
        #ifdef __APPLE__
            glfwWindowHint(GLFW_COCOA_RETINA_FRAMEBUFFER, state.glfw_cocoa_retina_framebuffer);
            glfwWindowHint(GLFW_COCOA_GRAPHICS_SWITCHING, state.glfw_cocoa_graphics_switching);
            if (!state.glfw_cocoa_frame_name.empty())
                {glfwWindowHintString(GLFW_COCOA_FRAME_NAME, state.glfw_cocoa_frame_name.c_str());}
        #endif
        if (!state.glfw_x11_class_name.empty())
            {glfwWindowHintString(GLFW_X11_CLASS_NAME, state.glfw_x11_class_name.c_str());}
        if (!state.glfw_x11_instance_name.empty())
            {glfwWindowHintString(GLFW_X11_INSTANCE_NAME, state.glfw_x11_instance_name.c_str());}
    #endif
    #if GLFW_TARGET_VERSION >= 34
        glfwWindowHint(GLFW_MOUSE_PASSTHROUGH, state.glfw_window_mouse_passthrough);
        glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, state.glfw_window_scale_framebuffer);
        glfwWindowHint(GLFW_POSITION_X, state.glfw_window_position_x);
        glfwWindowHint(GLFW_POSITION_Y, state.glfw_window_position_y);
        #ifdef _WIN32
            glfwWindowHint(GLFW_WIN32_KEYBOARD_MENU, state.glfw_win32_keyboard_menu);
            glfwWindowHint(GLFW_WIN32_SHOWDEFAULT, state.glfw_win32_showdefault);
        #endif
        if (!state.glfw_wayland_app_id.empty())
            {glfwWindowHintString(GLFW_WAYLAND_APP_ID, state.glfw_wayland_app_id.c_str());}
    #endif
}

inline void reportContextCeiling(const GLFWState& glfwState) noexcept {
    static const int candidates[] = {46, 45, 44, 43, 42, 41, 40, 33, 32, 31, 30,
                                        21, 20, 15, 14, 13, 12, 11, 10};

    for (int version : candidates) {
        if (version >= GL_TARGET_VERSION) continue;

        GLFWState probe = glfwState;
        probe.glfw_context_version_major = version / 10;
        probe.glfw_context_version_minor = version % 10;
        probe.glfw_window_visible = false;
        #if ENGINE_HAS_CONTEXT_PROFILE
            if (version < 32) probe.glfw_opengl_profile = GLFW_OPENGL_ANY_PROFILE;
        #endif
        #if ENGINE_HAS_FORWARD_COMPAT
            if (version < 30) probe.glfw_opengl_forward_compat = false;
        #endif

        glfwDefaultWindowHints();
        apply_glfw_state(probe);
        GLFWwindow* test = glfwCreateWindow(64, 64, "probe", nullptr, nullptr);
        if (!test) continue;

        glfwDestroyWindow(test);
        glfwDefaultWindowHints();
        log_warn("This driver provides at most OpenGL ", version / 10, ".", version % 10,
                    "; reconfigure with -DGL_TARGET_VERSION=", version);
        return;
    }
    glfwDefaultWindowHints();
}

//Window Structs

struct WindowSize{int width = 1280, height = 720;};
struct FramebufferSize{int width = 0, height = 0;};
struct WindowPos{int x = GLFW_DONT_CARE, y = GLFW_DONT_CARE;};
struct Color{float red = 0.0f, green = 0.0f, blue = 0.0f, alpha = 1.0f;};

#if GLFW_TARGET_VERSION >= 32
    struct WindowBounds{
        int minWidth = GLFW_DONT_CARE, minHeight = GLFW_DONT_CARE,
            maxWidth = GLFW_DONT_CARE, maxHeight = GLFW_DONT_CARE;
    };
    struct AspectRatio{int numer = GLFW_DONT_CARE, denom = GLFW_DONT_CARE;};

    // Non-owning: the images must outlive every call that uses this icon.
    struct WindowIcon{int count = 0; const GLFWimage* images = nullptr;};
#endif

//Window Manager structs

struct VideoMode {
    int width = 0, height = 0;
    int redBits = 0, greenBits = 0, blueBits = 0;
    int refreshRate = 0;
};

struct GammaRamp {
    std::vector<unsigned short> red, green, blue;

    // True when all three channels are present and the same length.
    bool isValid() const noexcept {
        return !red.empty() && red.size() == green.size() && red.size() == blue.size();
    }

    // Number of entries per channel.
    size_t size() const noexcept {return red.size();}

    // Resizes every channel, zero-filling the entries.
    void resize(size_t n) noexcept {red.assign(n, 0); green.assign(n, 0); blue.assign(n, 0);}
};

struct MonitorInfo {
    GLFWmonitor* handle = nullptr;
    std::string name;
    WindowPos pos{0, 0};
    int physicalWidthMM = 0, physicalHeightMM = 0;
    VideoMode currentMode;
    std::vector<VideoMode> modes;
    #if GLFW_TARGET_VERSION >= 33
        WindowPos workareaPos{0, 0};
        WindowSize workareaSize{0, 0};
        float contentScaleX = 1.0f, contentScaleY = 1.0f;
    #endif
};

struct WindowCallbacks {
    std::function<void(int, int)>                      onWindowPos;
    std::function<void(int, int)>                      onWindowSize;
    std::function<void(int, int)>                      onFramebufferSize;
    std::function<void()>                              onWindowClose;
    std::function<void()>                              onWindowRefresh;
    std::function<void(bool)>                          onWindowFocus;
    std::function<void(bool)>                          onWindowIconify;
    std::function<void(int, int, int, int)>            onKey;
    std::function<void(unsigned int)>                  onChar;
    std::function<void(int, int, int)>                 onMouseButton;
    std::function<void(double, double)>                onCursorPos;
    std::function<void(bool)>                          onCursorEnter;
    std::function<void(double, double)>                onScroll;
    #if GLFW_TARGET_VERSION >= 31
        std::function<void(const std::vector<std::string>&)> onDrop;
    #endif
    #if GLFW_TARGET_VERSION >= 33
        std::function<void(bool)>                      onWindowMaximise;
        std::function<void(float, float)>              onWindowContentScale;
    #endif
    std::function<void()>                              onMonitorChange;
};

struct SystemCallbacks {
    std::function<void(GLFWmonitor*, int)> onMonitor;
    #if GLFW_TARGET_VERSION >= 32
        std::function<void(int, int)>      onJoystick;
    #endif
};

class Window;

struct WindowManagerConfig{Platform::GlfwBackend requestedGlfwBackend = Platform::GlfwBackend::Any;
                           Platform::GLFWInitHints initHints = Platform::GLFWInitHints{};};

class WindowManager {
    public:
        // Initialises GLFW once and returns the shared manager; null on failure.
        static std::shared_ptr<WindowManager> createShared(WindowManagerConfig config = WindowManagerConfig{}) noexcept {
            static std::weak_ptr<WindowManager> self;
            if (auto existing = self.lock()) {
                if (config.requestedGlfwBackend != Platform::GlfwBackend::Any && config.requestedGlfwBackend != existing->backend()) {
                    log_error("GLFW already initialised; ignoring the requested backend");
                }
                return existing;
            }

            int major = 0, minor = 0, rev = 0;
            glfwGetVersion(&major, &minor, &rev);
            if (major * 10 + minor < GLFW_TARGET_VERSION) {
                log_error("GLFW runtime older than headers: ", major, ".", minor);
                return nullptr;
            }
            GLFWErrorHandler::install();

            #if GLFW_TARGET_VERSION >= 34
                const int hinted = Platform::to_glfw_platform(config.requestedGlfwBackend);
                if (hinted != GLFW_ANY_PLATFORM) {
                    if (glfwPlatformSupported(hinted)) {
                        config.initHints.glfw_platform = hinted;
                    } else {
                        log_warn("Requested backend unsupported by this GLFW build; using default");
                    }
                }
            #else
                (void)requestedGlfwBackend;
            #endif

            Platform::apply_glfw_init_hints(config.initHints);

            if (!glfwInit()) {log_error("GLFW init failed"); return nullptr;}

            std::shared_ptr<WindowManager> created(new WindowManager());
            created->_backend = Platform::detect_glfw_backend();

            created->_capabilities = Platform::detect_glfw_capabilities(created->_backend);
            created->_bugs = Platform::detect_glfw_bugs(created->_backend);
            s_current = created.get();
            glfwSetMonitorCallback(&WindowManager::cbMonitor);
            #if GLFW_TARGET_VERSION >= 32
                glfwSetJoystickCallback(&WindowManager::cbJoystick);
            #endif
            created->refreshMonitors();
            created->probeGammaSupport();
            self = created;
            return created;
        }

        // Replaces the process-wide monitor and joystick callbacks.
        void setCallbacks(SystemCallbacks callbacks) noexcept {_callbacks = std::move(callbacks);}

        // Every connected monitor. Invalidated by any monitor connect/disconnect:
        // do not hold MonitorInfo pointers across event polling.
        const std::vector<MonitorInfo>& monitors() const noexcept {return _monitors;}

        // The primary monitor, or null when none are connected.
        const MonitorInfo* primaryMonitor() const noexcept {
            return _monitors.empty() ? nullptr : &_monitors.front();
        }

        // The monitor record for a GLFW handle, or null when unknown.
        const MonitorInfo* monitorFor(GLFWmonitor* handle) const noexcept {
            if (!handle) return nullptr;
            for (const MonitorInfo& info : _monitors) {
                if (info.handle == handle) return &info;
            }
            return nullptr;
        }

        // True when this backend can read and write gamma ramps.
        bool supportsGamma() const noexcept {return _capabilities.monitor_gamma;}

        // Applies a gamma exponent to a monitor; false when unsupported.
        bool setGamma(const MonitorInfo& monitor, float exponent) noexcept {
            if (!_capabilities.monitor_gamma) return false;
            if (!monitor.handle) {log_error("setGamma called with a null monitor handle"); return false;}
            if (!std::isfinite(exponent) || exponent <= 0.0f) {
                log_error("setGamma requires a finite exponent greater than zero");
                return false;
            }
            glfwSetGamma(monitor.handle, exponent);
            return true;
        }

        // Reads a monitor's current gamma ramp; false when unsupported.
        bool getGammaRamp(const MonitorInfo& monitor, GammaRamp& out) const noexcept {
            out = GammaRamp{};
            if (!_capabilities.monitor_gamma) return false;
            if (!monitor.handle) {log_error("getGammaRamp called with a null monitor handle"); return false;}

            const GLFWgammaramp* ramp = glfwGetGammaRamp(monitor.handle);
            if (!ramp || !ramp->red || !ramp->green || !ramp->blue || ramp->size == 0) return false;

            const size_t n = static_cast<size_t>(ramp->size);
            out.red.assign(ramp->red, ramp->red + n);
            out.green.assign(ramp->green, ramp->green + n);
            out.blue.assign(ramp->blue, ramp->blue + n);
            return true;
        }

        // Uploads a gamma ramp to a monitor; Win32 requires exactly 256 entries.
        bool setGammaRamp(const MonitorInfo& monitor, const GammaRamp& ramp) noexcept {
            if (!_capabilities.monitor_gamma) return false;
            if (!monitor.handle) {log_error("setGammaRamp called with a null monitor handle"); return false;}
            if (!ramp.isValid()) {log_error("setGammaRamp given a ramp with mismatched or empty channels"); return false;}
            #if defined(_WIN32)
                if (ramp.size() != 256) {
                    log_error("Win32 requires a gamma ramp of exactly 256 entries; got ", ramp.size());
                    return false;
                }
            #endif

            GLFWgammaramp glfwRamp;
            glfwRamp.red   = const_cast<unsigned short*>(ramp.red.data());
            glfwRamp.green = const_cast<unsigned short*>(ramp.green.data());
            glfwRamp.blue  = const_cast<unsigned short*>(ramp.blue.data());
            glfwRamp.size  = static_cast<unsigned int>(ramp.size());
            glfwSetGammaRamp(monitor.handle, &glfwRamp);
            return true;
        }

        // Disables the gamma capability when the backend returns no usable ramp.
        void probeGammaSupport() noexcept {
            if (!_capabilities.monitor_gamma) return;
            const MonitorInfo* monitor = primaryMonitor();
            if (!monitor) return;

            GammaRamp ramp;
            if (!getGammaRamp(*monitor, ramp) || !ramp.isValid()) {
                _capabilities.monitor_gamma = false;
                log_warn("Gamma disabled: the backend returned no usable ramp");
                return;
            }
            unsigned long total = 0;
            for (unsigned short value : ramp.red) total += value;
            if (total == 0) {
                _capabilities.monitor_gamma = false;
                log_warn("Gamma disabled: the backend returned a degenerate ramp");
            }
        }

        // Rebuilds the monitor list; invalidates every previously returned pointer.
        void refreshMonitors() noexcept {
            _monitors.clear();
            int count = 0;
            GLFWmonitor** handles = glfwGetMonitors(&count);
            if (!handles || count <= 0) return;

            _monitors.reserve(static_cast<size_t>(count));
            for (int i = 0; i < count; ++i) {
                GLFWmonitor* handle = handles[i];
                if (!handle) continue;

                MonitorInfo info;
                info.handle = handle;

                const char* name = glfwGetMonitorName(handle);
                if (name) info.name = name;

                glfwGetMonitorPos(handle, &info.pos.x, &info.pos.y);
                glfwGetMonitorPhysicalSize(handle, &info.physicalWidthMM, &info.physicalHeightMM);

                if (const GLFWvidmode* mode = glfwGetVideoMode(handle)) {
                    info.currentMode = toVideoMode(*mode);
                }

                int modeCount = 0;
                if (const GLFWvidmode* modes = glfwGetVideoModes(handle, &modeCount)) {
                    info.modes.reserve(static_cast<size_t>(modeCount < 0 ? 0 : modeCount));
                    for (int m = 0; m < modeCount; ++m) info.modes.push_back(toVideoMode(modes[m]));
                }

                #if GLFW_TARGET_VERSION >= 33
                    glfwGetMonitorWorkarea(handle, &info.workareaPos.x, &info.workareaPos.y,
                                                   &info.workareaSize.width, &info.workareaSize.height);
                    glfwGetMonitorContentScale(handle, &info.contentScaleX, &info.contentScaleY);
                #endif

                _monitors.push_back(std::move(info));
            }
        }

        // Number of live windows registered with this manager.
        size_t windowCount() const noexcept {return _windows.size();}

        // True when every registered window has been asked to close.
        bool allWindowsClosed() const noexcept;

        // The detected windowing backend.
        Platform::GlfwBackend backend() const noexcept {return _backend;}
        // Per-backend feature availability.
        const Platform::GlfwCapabilities& capabilities() const noexcept {return _capabilities;}
        // Per-backend known GLFW quirks.
        const Platform::GLFWBugs& bugs() const noexcept {return _bugs;}

        // Processes pending events without blocking.
        static void pollEvents() noexcept {glfwPollEvents();}
        // Blocks until at least one event arrives, then processes it.
        static void waitEvents() noexcept {glfwWaitEvents();}

        #if GLFW_TARGET_VERSION >= 31
            // Wakes a thread blocked in waitEvents.
            static void postEmptyEvent() noexcept {glfwPostEmptyEvent();}
        #endif
        #if GLFW_TARGET_VERSION >= 32
            // Waits for events with a timeout in seconds.
            static void waitEventsTimeout(double timeout) noexcept {glfwWaitEventsTimeout(timeout);}
        #endif

        // Seconds since glfwInit, or since the last setSystemTime.
        static double getSystemTime() noexcept {return glfwGetTime();}
        // Rebases the GLFW clock to the given number of seconds.
        static void setSystemTime(double seconds) noexcept {glfwSetTime(seconds);}

        // The shared frame-pacing timer, ticked once per frame by the caller.
        Timer& timer() noexcept {return _timer;}

        #if GLFW_TARGET_VERSION >= 32
            // The raw timer value, for high-resolution timing.
            static uint64_t timerValue() noexcept {return glfwGetTimerValue();}
            // Ticks per second of the raw timer.
            static uint64_t timerFrequency() noexcept {return glfwGetTimerFrequency();}
        #endif

        // The GLFW version and build string.
        static std::string versionString() noexcept {
            const char* s = glfwGetVersionString();
            return s ? std::string(s) : std::string();
        }

        // The GLFW library version actually linked at runtime.
        static void runtimeVersion(int& major, int& minor, int& revision) noexcept {
            glfwGetVersion(&major, &minor, &revision);
        }

        ~WindowManager() noexcept {
            GLFWErrorHandler::uninstall();
            glfwSetMonitorCallback(nullptr);
            #if GLFW_TARGET_VERSION >= 32
                glfwSetJoystickCallback(nullptr);
            #endif
            s_current = nullptr;
            glfwTerminate();
        }

        WindowManager(const WindowManager&) = delete;
        WindowManager& operator=(const WindowManager&) = delete;
        WindowManager(WindowManager&&) = delete;
        WindowManager& operator=(WindowManager&&) = delete;
    private:
        friend class Window;

        // Defaulted without an explicit noexcept: `noexcept = default` would
        // delete the constructor outright if any member's own default ever
        // became potentially-throwing, which fails in a hard-to-read way.
        WindowManager() = default;

        // Adds a window to the manager's registry.
        void registerWindow(Window* window) noexcept {
            _windows.push_back(window);
        }

        // Removes a window from the manager's registry.
        void unregisterWindow(Window* window) noexcept {
            for (auto it = _windows.begin(); it != _windows.end(); ++it) {
                if (*it == window) {_windows.erase(it); return;}
            }
        }

        // Converts a GLFW video mode into the engine's own struct.
        static VideoMode toVideoMode(const GLFWvidmode& mode) noexcept {
            VideoMode out;
            out.width = mode.width;
            out.height = mode.height;
            out.redBits = mode.redBits;
            out.greenBits = mode.greenBits;
            out.blueBits = mode.blueBits;
            out.refreshRate = mode.refreshRate;
            return out;
        }

        // Monitor connect/disconnect callback; refreshes the monitor list.
        static void cbMonitor(GLFWmonitor* monitor, int event) noexcept;
        #if GLFW_TARGET_VERSION >= 32
            // Joystick connect/disconnect callback.
            static void cbJoystick(int jid, int event) noexcept;
        #endif

        inline static WindowManager* s_current = nullptr;

        Timer _timer;
        Platform::GlfwBackend _backend = Platform::GlfwBackend::Any;
        Platform::GlfwCapabilities _capabilities;
        Platform::GLFWBugs _bugs;
        SystemCallbacks _callbacks;
        std::vector<Window*> _windows;
        std::vector<MonitorInfo> _monitors;
};

struct WindowConfig {
    WindowSize size = {640,480};
    FramebufferSize framebufferSize;
    WindowPos pos;
    #if GLFW_TARGET_VERSION >= 32
        WindowBounds bounds;
        AspectRatio aspectRatio;
        WindowIcon icon;
    #endif
    #if GLFW_TARGET_VERSION >= 33
        float opacity = 1.0f;
    #endif
    std::string title = "Window";
    GLFWmonitor* monitor = nullptr;
    GLFWwindow* share = nullptr;
    bool vsync = true;
    uint32_t refresh_rate = 0;
    #if IMGUI_ENABLED
        bool createImGuiContext = false;
    #endif
    GLState glState = GLState{};
    GLFWState glfwState = GLFWState{};
};

struct WindowDeleter {
    // Destroys a GLFW window handle.
    void operator()(GLFWwindow* w) const noexcept {if (w) glfwDestroyWindow(w);}
};

struct FrameContext {Window& window; float dt;};
using FrameCallback = std::function<void(const FrameContext&)>;

struct FrameCallbacks {
    FrameCallback preFrame;
    FrameCallback render;
    FrameCallback preBufferSwap;
    FrameCallback postFrame;
};

// Window class wrapping a GLFWwindow*; a WindowManager must be created first.
class Window {
    public:
        // Creates a window, its GL context and its initial state; null on failure.
        // The WindowManager must outlive the returned Window and be created with
        // createShared() first. On success the new context is left current; on
        // failure the caller's previous context is restored.
        static std::unique_ptr<Window> create(std::shared_ptr<WindowManager> windowManager,
                                                     WindowConfig config = WindowConfig{}) noexcept {
            if (!windowManager) {log_error("Received null pointer WindowManager"); return nullptr;}

            #if ENGINE_HAS_MULTISAMPLE
                if (config.glState.gl_multisample && config.glfwState.glfw_samples <= 0) config.glfwState.glfw_samples = 4;
                if (config.glfwState.glfw_samples <= 0) config.glState.gl_multisample = false;
            #endif

            glfwDefaultWindowHints();
            apply_glfw_state(config.glfwState);

            const bool wantsPosition = windowManager->capabilities().window_position
                                       && config.pos.x != GLFW_DONT_CARE
                                       && config.pos.y != GLFW_DONT_CARE;
            const bool deferShow = wantsPosition && config.glfwState.glfw_window_visible;
            if (deferShow) glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);

            GLFWwindow* handle = glfwCreateWindow(config.size.width, config.size.height,
                                                  config.title.c_str(), config.monitor, config.share);
            if (!handle) {
                log_error("Failed to create GLFW window");
                if (windowManager->bugs().gdi_software_gl_context_fails) {
                    log_error("On Windows this often means no hardware OpenGL is available "
                              "(e.g. a plain Remote Desktop session): GLFW can't create a context "
                              "over the GDI software renderer. Try a physical console, an RDP GPU "
                              "policy, or a software-GL fallback build.");
                }
                reportContextCeiling(config.glfwState);
                return nullptr;
            }

            std::unique_ptr<GLFWwindow, WindowDeleter> guard(handle);

            GLFWwindow* previous = glfwGetCurrentContext();
            glfwMakeContextCurrent(handle);

            const auto fail = [&](const char* message) -> std::unique_ptr<Window> {
                log_error(message);
                glfwMakeContextCurrent(previous);
                return nullptr;
            };

            if (!Platform::load_gl_functions()) return fail("Failed loading GLAD");
            if (Platform::get_gl_version() < GL_TARGET_VERSION) return fail("glContext version below GL_TARGET_VERSION");

            #if ENGINE_HAS_DEBUG_OUTPUT
                GlErrorHandler::install();
            #endif

            #if ENGINE_HAS_CONTEXT_PROFILE
                int profileMask = 0;
                glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profileMask);
                GlErrorHandler::clear();
                if (config.glfwState.glfw_opengl_profile == GLFW_OPENGL_CORE_PROFILE
                    && !(profileMask & GL_CONTEXT_CORE_PROFILE_BIT)) {
                    return fail("Core profile requested but context is not core profile");
                }
            #endif

            #if ENGINE_HAS_MULTISAMPLE
                if (config.glState.gl_multisample) {
                    int samples = 0;
                    glGetIntegerv(GL_SAMPLES, &samples);
                    if (samples < 1) config.glState.gl_multisample = false;
                }
            #endif

            glfwGetWindowSize(handle, &config.size.width, &config.size.height);
            glfwGetFramebufferSize(handle, &config.framebufferSize.width, &config.framebufferSize.height);

            if (wantsPosition) {
                glfwSetWindowPos(handle, config.pos.x, config.pos.y);
                glfwGetWindowPos(handle, &config.pos.x, &config.pos.y);
            }

            #if GLFW_TARGET_VERSION >= 32
                glfwSetWindowSizeLimits(handle, config.bounds.minWidth, config.bounds.minHeight,
                                                config.bounds.maxWidth, config.bounds.maxHeight);
                glfwSetWindowAspectRatio(handle, config.aspectRatio.numer, config.aspectRatio.denom);
                if (windowManager->capabilities().window_icon
                    && config.icon.images && config.icon.count != 0) {
                    glfwSetWindowIcon(handle, config.icon.count, config.icon.images);
                }
            #endif
            #if GLFW_TARGET_VERSION >= 33
                if (config.opacity < 1.0f && windowManager->capabilities().window_opacity) {
                    glfwSetWindowOpacity(handle, config.opacity);
                }
            #endif

            glfwSwapInterval(config.vsync ? 1 : 0);

            GLFWmonitor* monitor = config.monitor ? config.monitor : glfwGetPrimaryMonitor();
            if (monitor && !config.refresh_rate) {
                if (const GLFWvidmode* mode = glfwGetVideoMode(monitor)) {
                    config.refresh_rate = static_cast<uint32_t>(mode->refreshRate);
                }
            }
            glViewport(0, 0, config.framebufferSize.width, config.framebufferSize.height);
            GlErrorHandler::clear();

            apply_gl_state(config.glState);
            if (GlErrorHandler::logErrors()) return fail("GL errors while applying the initial state");

            const GLubyte* renderer = glGetString(GL_RENDERER);
            const GLubyte* version = glGetString(GL_VERSION);
            log_info("GL renderer: ", renderer ? reinterpret_cast<const char*>(renderer) : "(null)");
            log_info("GL version: ", version ? reinterpret_cast<const char*>(version) : "(null)");

            if (deferShow) glfwShowWindow(handle);
            log_info("Window initialised successfully");

            return std::unique_ptr<Window>(new Window(std::move(windowManager), guard.release(),
                                                      std::move(config), std::move(config.glState),
                                                      config.glfwState.glfw_window_visible));
        }

        ~Window() noexcept {
            #if IMGUI_ENABLED
                if (_imguiContext) {
                    if (!isContextCurrent()) glfwMakeContextCurrent(_window);
                    _imguiContext.reset();
                }
            #endif
            #if ENGINE_HAS_DEBUG_OUTPUT
                if (isContextCurrent()) GlErrorHandler::uninstall();
            #endif
            _input.detach();
            if (_windowManager) _windowManager->unregisterWindow(this);
            if (_window) {glfwSetWindowUserPointer(_window, nullptr); glfwDestroyWindow(_window);}
        }

        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        Window(Window&&) = delete;
        Window& operator=(Window&&) = delete;

        unsigned int id() const noexcept {return _id;}

        #if IMGUI_ENABLED
            // Adopts an ImGui context, replacing any already installed.
            bool installImGuiContext(std::unique_ptr<ImguiContext> context) noexcept {
                if (!context) {log_error("null context supplied to installImGuiContext"); return false;}
                _imguiContext = std::move(context);
                log_info("ImGui context installed");
                return true;
            }

            // Creates and adopts a new ImGui context for this window.
            bool autoInstallImGuiContext() noexcept {
                std::unique_ptr<ImguiContext> imguiContext = ImguiContext::create(this->getHandle());
                if (!imguiContext) {log_error("Failed to create Imgui Context"); return false;}
                else {return installImGuiContext(std::move(imguiContext));}
            }

            // Tears down the installed ImGui context; false when none is installed.
            bool destroyImGuiContext() noexcept {
                if (!_imguiContext) {log_error("null context on destroyImguiContext, install one first"); return false;}
                if (!isContextCurrent()) glfwMakeContextCurrent(_window);
                _imguiContext.reset();
                log_info("ImGui context destroyed");
                return true;
            }

            // The installed ImGui context, or null when none is installed.
            ImguiContext* imGui() noexcept {return _imguiContext.get();}

            // The installed ImGui context, or null when none is installed.
            const ImguiContext* imGui() const noexcept {return _imguiContext.get();}

            // True when this window owns a live ImGui context.
            bool hasImGui() const noexcept {return _imguiContext != nullptr;}
        #endif

        // The underlying GLFW window handle.
        GLFWwindow* getHandle() const noexcept {return _window;}
        // The window's current configuration.
        const WindowConfig& config() const noexcept {return _config;}
        // The GL state last applied to this window's context.
        const GLState& glState() const noexcept {return _glContext;}
        // The detected windowing backend.
        Platform::GlfwBackend backend() const noexcept {return _windowManager->backend();}
        // Per-backend feature availability.
        const Platform::GlfwCapabilities& capabilities() const noexcept {return _windowManager->capabilities();}
        // Per-backend known GLFW quirks.
        const Platform::GLFWBugs& bugs() const noexcept {return _windowManager->bugs();}

        // This window's input state.
        Input& input() noexcept {return _input;}
        // This window's input state.
        const Input& input() const noexcept {return _input;}
        // Replaces this window's event callbacks.
        void setCallbacks(WindowCallbacks callbacks) noexcept {_callbacks = std::move(callbacks);}

        // True until the window has been asked to close.
        bool isOpen() const noexcept {return !glfwWindowShouldClose(_window);}
        // Sets or clears the window's close flag.
        void setShouldClose(bool value) noexcept {glfwSetWindowShouldClose(_window, value ? GLFW_TRUE : GLFW_FALSE);}

        // Makes this window's context current and restores its viewport.
        void makeContextCurrent() noexcept {glfwMakeContextCurrent(_window); restoreViewport();}
        // True when this window's context is current on the calling thread.
        bool isContextCurrent() const noexcept {return glfwGetCurrentContext() == _window;}
        // Presents the back buffer.
        void swapBuffers() noexcept {glfwSwapBuffers(_window);}

        // Runs one frame with provided functions. Call it in a loop: while (isOpen())
        // Does not call pollEvents.
        void runFrame(const FrameCallbacks& funcs, float dt) {
                      const FrameContext frame{*this, dt};

            if (funcs.preFrame) funcs.preFrame(frame);

            if (isIconified()) {
                _input.endFrame();
                if (funcs.postFrame) funcs.postFrame(frame);
                return;
            }

            beginFrame();
            if (funcs.render) funcs.render(frame);
            #if IMGUI_ENABLED
                if (hasImGui()) imGui()->endFrame();
            #endif
            if (funcs.preBufferSwap) funcs.preBufferSwap(frame);
            present();
            if (funcs.postFrame) funcs.postFrame(frame);
        }
       
        // Starts a frame: makes the context current and resets the viewport,
        // which an offscreen Framebuffer::bind() in the last frame will have changed.
        // If an ImGui context is installed this also calls imGui()->beginFrame().
        void beginFrame(Color color=Color{}) noexcept {
            if (!isContextCurrent()) glfwMakeContextCurrent(_window);
            restoreViewport();
            clear(color);
            #if IMGUI_ENABLED
                if (hasImGui()) {imGui()->beginFrame();}
            #endif
        }

        //Ends a frame: presents the back buffer and clears per-frame input,
        // without calling imGui()->endFrame() if an ImGui context is installed 
        void present() noexcept {
            glfwSwapBuffers(_window);
            _input.endFrame();
        }

        // Ends a frame: presents the back buffer and clears per-frame input.
        // If an ImGui context is installed this also calls imGui()->endFrame().
        void endFrame() noexcept {
            #if IMGUI_ENABLED
                if (hasImGui()) {imGui()->endFrame();}
            #endif
            present();
        }

        // Clears the window to a colour, with a configurable buffer mask.
        void clear(Color color, GLbitfield mask = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT) noexcept {
            glClearColor(color.red, color.green, color.blue, color.alpha);
            glClear(mask);
        }

        // Resets the viewport only when a resize marked it dirty. For callers that
        // drive their own frame loop instead of beginFrame, which always resets.
        void flushViewport() noexcept {
            if (!_viewportDirty) return;
            restoreViewport();
        }

        // Sets the viewport to cover the whole window framebuffer.
        void restoreViewport() noexcept {
            glViewport(0, 0, _config.framebufferSize.width, _config.framebufferSize.height);
            _viewportDirty = false;
        }

        // Applies a new GL state to this window's context.
        void setGLState(const GLState& state) noexcept {
            if (!requireCurrentContext("setGLState")) return;
            _glContext = state;
            apply_gl_state(_glContext);
        }

        // Enables or disables vertical sync for this context.
        void setVsync(bool enabled) noexcept {
            _config.vsync = enabled;
            if (!requireCurrentContext("setVsync")) return;
            glfwSwapInterval(enabled ? 1 : 0);
        }
        // True when vertical sync is enabled.
        bool vsync() const noexcept {return _config.vsync;}
        // The monitor refresh rate recorded for this window.
        uint32_t refresh_rate() const noexcept {return _config.refresh_rate;}

        // True when the context exposes a named GL or WGL/GLX extension.
        bool isExtensionSupported(const char* extension) const noexcept {
            if (!requireCurrentContext("isExtensionSupported")) return false;
            return glfwExtensionSupported(extension) == GLFW_TRUE;
        }

        // Resolves a GL entry point through GLFW; null when unavailable.
        GLFWglproc getProcAddress(const char* procname) const noexcept {
            if (!requireCurrentContext("getProcAddress")) return nullptr;
            return glfwGetProcAddress(procname);
        }

        // Reads a GLFW window attribute.
        int getAttrib(int attrib) const noexcept {return glfwGetWindowAttrib(_window, attrib);}
        // True when the window can be resized by the user.
        bool isResizable() const noexcept {return getAttrib(GLFW_RESIZABLE);}
        // True when the window has decorations.
        bool isDecorated() const noexcept {return getAttrib(GLFW_DECORATED);}
        // True when the window has input focus.
        bool isFocused() const noexcept {return getAttrib(GLFW_FOCUSED);}

        // True when the window is iconified. Always false on backends that
        // cannot report the state (Wayland): check capabilities first.
        bool isIconified() const noexcept {return getAttrib(GLFW_ICONIFIED);}

        // True when the window is mapped and visible.
        bool isVisible() const noexcept {return getAttrib(GLFW_VISIBLE);}

        // Sets the window title.
        void setTitle(const std::string& title) noexcept {
            _config.title = title;
            glfwSetWindowTitle(_window, _config.title.c_str());
        }
        // The window title last set through this class.
        const std::string& getTitle() const noexcept {return _config.title;}

        // Re-reads the cached window and framebuffer sizes from GLFW.
        void refreshDimensions() noexcept {
            glfwGetWindowSize(_window, &_config.size.width, &_config.size.height);
            glfwGetFramebufferSize(_window, &_config.framebufferSize.width, &_config.framebufferSize.height);
            _viewportDirty = true;
        }

        // Re-reads the cached window position, where the backend supports it.
        void refreshPosition() noexcept {
            if (!capabilities().window_position) {
                Platform::glfw_warn_feature_unsupported("window position", _windowManager->backend());
                return;
            }
            glfwGetWindowPos(_window, &_config.pos.x, &_config.pos.y);
        }

        // Window width in screen coordinates.
        int w_width() const noexcept {return _config.size.width;}
        // Window height in screen coordinates.
        int w_height() const noexcept {return _config.size.height;}
        // Framebuffer width in pixels.
        int fb_width() const noexcept {return _config.framebufferSize.width;}
        // Framebuffer height in pixels.
        int fb_height() const noexcept {return _config.framebufferSize.height;}

        // Reads the live window size from GLFW.
        void getSize(int* width, int* height) const noexcept {glfwGetWindowSize(_window, width, height);}
        // Resizes the window and refreshes the cached dimensions.
        void setSize(int width, int height) noexcept {glfwSetWindowSize(_window, width, height); refreshDimensions();}
        // Reads the live framebuffer size from GLFW.
        void getFramebufferSize(int* width, int* height) const noexcept {glfwGetFramebufferSize(_window, width, height);}

        // Reads the window position, or zeroes where unsupported.
        void Pos(int* xpos, int* ypos) const noexcept {
            if (!capabilities().window_position) {
                if (xpos) *xpos = 0;
                if (ypos) *ypos = 0;
                return;
            }
            glfwGetWindowPos(_window, xpos, ypos);
        }

        // Moves the window, where the backend supports positioning.
        void setPos(int xpos, int ypos) noexcept {
            if (!capabilities().window_position) {
                Platform::glfw_warn_feature_unsupported("window position", _windowManager->backend());
                return;
            }
            glfwSetWindowPos(_window, xpos, ypos);
            refreshPosition();
        }

        // Minimises the window, where the backend supports it.
        void iconify() noexcept {
            if (!capabilities().window_iconify) {
                Platform::glfw_warn_feature_unsupported("window iconify", _windowManager->backend());
                return;
            }
            glfwIconifyWindow(_window);
        }

        // Restores the window from fullscreen, maximised or iconified state.
        void restore() noexcept {
            #if GLFW_TARGET_VERSION >= 32
                if (isFullscreen()) {setWindowed(); return;}
                const bool wasMaximised = isMaximised();
                glfwRestoreWindow(_window);
                if (wasMaximised && _savedRect.width > 0 && _savedRect.height > 0) {
                    glfwSetWindowSize(_window, _savedRect.width, _savedRect.height);
                    if (capabilities().window_position) setPos(_savedRect.x, _savedRect.y);
                }
            #else
                glfwRestoreWindow(_window);
            #endif
            refreshDimensions();
            refreshPosition();
        }

        // Maps the window.
        void show() noexcept {glfwShowWindow(_window);}
        // Unmaps the window.
        void hide() noexcept {glfwHideWindow(_window);}
        // Toggles the window between mapped and unmapped.
        void toggleVisibility() noexcept {if (isVisible()) {hide();} else {show();}}

        // The system clipboard's text contents. On X11 this needs the event
        // loop pumped, because the selection owner answers by event.
        std::string getClipboard() const noexcept {
            const char* text = glfwGetClipboardString(_window);
            return text ? std::string(text) : std::string();
        }

        // Replaces the system clipboard's text contents.
        void setClipboard(const std::string& text) noexcept {glfwSetClipboardString(_window, text.c_str());}

        // Every connected monitor; invalidated by monitor connect/disconnect.
        const std::vector<MonitorInfo>& monitors() const noexcept {return _windowManager->monitors();}
        // The primary monitor, or null when none are connected.
        const MonitorInfo* primaryMonitor() const noexcept {return _windowManager->primaryMonitor();}
        // The monitor handle this window is fullscreen on, or null.
        GLFWmonitor* getMonitorHandle() const noexcept {return glfwGetWindowMonitor(_window);}
        // True when the window is fullscreen on a monitor.
        bool isFullscreen() const noexcept {return glfwGetWindowMonitor(_window) != nullptr;}
        // The monitor record this window is fullscreen on, or null.
        const MonitorInfo* getFullscreenMonitor() const noexcept {return _windowManager->monitorFor(glfwGetWindowMonitor(_window));}

        // The monitor the window mostly overlaps, or the primary as a fallback.
        const MonitorInfo* currentMonitor() const noexcept {
            if (const MonitorInfo* fullscreen = getFullscreenMonitor()) return fullscreen;

            const MonitorInfo* primary = _windowManager->primaryMonitor();
            if (!capabilities().window_position) return primary;

            int wx = 0, wy = 0, ww = 0, wh = 0;
            glfwGetWindowPos(_window, &wx, &wy);
            glfwGetWindowSize(_window, &ww, &wh);
            if (ww <= 0 || wh <= 0) return primary;
            if (wx < POSITION_SENTINEL || wy < POSITION_SENTINEL) return primary;

            const MonitorInfo* best = primary;
            long bestArea = 0;
            for (const MonitorInfo& monitor : _windowManager->monitors()) {
                int mx = 0, my = 0, mw = 0, mh = 0;
                monitorArea(monitor, mx, my, mw, mh);
                if (mw <= 0 || mh <= 0) continue;

                const int overlapW = (wx + ww < mx + mw ? wx + ww : mx + mw) - (wx > mx ? wx : mx);
                const int overlapH = (wy + wh < my + mh ? wy + wh : my + mh) - (wy > my ? wy : my);
                if (overlapW <= 0 || overlapH <= 0) continue;

                const long area = static_cast<long>(overlapW) * static_cast<long>(overlapH);
                if (area > bestArea) {bestArea = area; best = &monitor;}
            }
            return best;
        }

        #if GLFW_TARGET_VERSION >= 31
            // True when the window is always on top.
            bool isFloating() const noexcept {return getAttrib(GLFW_FLOATING);}
            // True when fullscreen windows iconify on focus loss.
            bool isAutoIconify() const noexcept {return getAttrib(GLFW_AUTO_ICONIFY);}
            // Reads the decoration thickness on each edge.
            void getFrameSize(int* left, int* top, int* right, int* bottom) const noexcept {
                glfwGetWindowFrameSize(_window, left, top, right, bottom);
            }
        #endif

        #if GLFW_TARGET_VERSION >= 32
            // True when the window is maximised.
            bool isMaximised() const noexcept {return getAttrib(GLFW_MAXIMIZED);}

            // Maximises the window, saving the current rectangle first.
            void maximise() noexcept {
                if (!isMaximised() && !isFullscreen()) saveWindowRect();
                glfwMaximizeWindow(_window);
                refreshDimensions();
            }

            // Requests input focus, where the backend supports it.
            void focus() noexcept {
                if (!capabilities().window_focus) {
                    Platform::glfw_warn_feature_unsupported("window focus", _windowManager->backend());
                    return;
                }
                glfwFocusWindow(_window);
            }

            // Sets the window icon. The images are not copied and must outlive
            // every later use of _config.icon.
            void setIcon(int count, const GLFWimage* images) noexcept {
                if (!capabilities().window_icon) {
                    Platform::glfw_warn_feature_unsupported("window icon", _windowManager->backend());
                    return;
                }
                glfwSetWindowIcon(_window, count, images);
                _config.icon = {count, images};
            }

            // Constrains the window's resizable size range.
            void setSizeLimits(int minWidth, int minHeight, int maxWidth, int maxHeight) noexcept {
                glfwSetWindowSizeLimits(_window, minWidth, minHeight, maxWidth, maxHeight);
                _config.bounds = {minWidth, minHeight, maxWidth, maxHeight};
                refreshDimensions();
            }

            // Constrains the window's aspect ratio while resizing.
            void setAspectRatio(int numer, int denom) noexcept {
                glfwSetWindowAspectRatio(_window, numer, denom);
                _config.aspectRatio = {numer, denom};
                refreshDimensions();
            }

            // Moves the window between windowed and fullscreen on a monitor.
            void setMonitor(GLFWmonitor* monitor, int xpos, int ypos, int width, int height, int refreshRate) noexcept {
                glfwSetWindowMonitor(_window, monitor, xpos, ypos, width, height, refreshRate);
                _config.monitor = monitor;
                refreshDimensions();
                refreshPosition();
            }

            // Goes fullscreen on a monitor at its current video mode.
            void setFullscreenOnMonitor(const MonitorInfo& monitor) noexcept {setFullscreenOnMonitor(monitor, monitor.currentMode);}

            // Goes fullscreen on a monitor at an explicit video mode.
            void setFullscreenOnMonitor(const MonitorInfo& monitor, const VideoMode& mode) noexcept {
                if (!monitor.handle) {log_error("setFullscreen called with a null monitor handle"); return;}
                if (!isFullscreen()) {
                    saveWindowRect();
                    #if GLFW_TARGET_VERSION >= 33
                        _strippedDecorations = false;
                        if (bugs().decorations_persist_in_fullscreen && isDecorated()) {
                            setAttrib(GLFW_DECORATED, GLFW_FALSE);
                            _strippedDecorations = true;
                        }
                    #endif
                }
                setMonitor(monitor.handle, 0, 0, mode.width, mode.height, mode.refreshRate);
            }

            // Goes fullscreen on whichever monitor the window mostly overlaps.
            void setFullscreen() noexcept {if (const MonitorInfo* monitor = currentMonitor()) setFullscreenOnMonitor(*monitor);}

            // Returns to windowed mode at an explicit rectangle.
            void setWindowed(int xpos, int ypos, int width, int height) noexcept {
                setMonitor(nullptr, xpos, ypos, width, height, GLFW_DONT_CARE);
                restoreDecorations(xpos, ypos);
            }

            // Returns to windowed mode at the saved rectangle, or the current size
            // when nothing was ever saved (a zero rectangle is rejected by GLFW).
            void setWindowed() noexcept {
                WindowRect target = _savedRect;
                if (target.width <= 0 || target.height <= 0) {
                    target.width = w_width() > 0 ? w_width() : DEFAULT_WINDOWED_WIDTH;
                    target.height = w_height() > 0 ? w_height() : DEFAULT_WINDOWED_HEIGHT;
                }

                setMonitor(nullptr, target.x, target.y, target.width, target.height, GLFW_DONT_CARE);
                if (!isFullscreen() && (w_width() != target.width || w_height() != target.height)) {
                    glfwSetWindowSize(_window, target.width, target.height);
                    refreshDimensions();
                }
                restoreDecorations(target.x, target.y);
            }

            // Toggles between fullscreen and windowed.
            void toggleFullscreen() noexcept {if (isFullscreen()) {setWindowed();} else {setFullscreen();}}

            // The position saved before going fullscreen or maximised.
            WindowPos savedPosition() const noexcept {return {_savedRect.x, _savedRect.y};}
            // The size saved before going fullscreen or maximised.
            WindowSize savedSize() const noexcept {return {_savedRect.width, _savedRect.height};}

            // Centres the window on a monitor's work area.
            void centreOnMonitor(const MonitorInfo& monitor) noexcept {
                if (isFullscreen()) {log_error("centreOnMonitor called on a fullscreen window"); return;}
                int mx = 0, my = 0, mw = 0, mh = 0;
                monitorArea(monitor, mx, my, mw, mh);
                if (mw <= 0 || mh <= 0) return;
                setPos(mx + (mw - w_width()) / 2, my + (mh - w_height()) / 2);
            }

            // Centres the window on whichever monitor it mostly overlaps.
            void centreOnCurrentMonitor() noexcept {
                if (const MonitorInfo* monitor = currentMonitor()) centreOnMonitor(*monitor);
            }
        #endif

        #if GLFW_TARGET_VERSION >= 33
            // True when the cursor is over the window's content area.
            bool isHovered() const noexcept {return getAttrib(GLFW_HOVERED);}
            // True when the framebuffer has per-pixel transparency.
            bool isTransparentFramebuffer() const noexcept {return getAttrib(GLFW_TRANSPARENT_FRAMEBUFFER);}
            // True when showing the window also focuses it.
            bool isFocusOnShow() const noexcept {return getAttrib(GLFW_FOCUS_ON_SHOW);}
            // Sets a runtime-changeable window attribute.
            void setAttrib(int attrib, int value) noexcept {glfwSetWindowAttrib(_window, attrib, value);}
            // Adds or removes window decorations.
            void setDecorated(bool decorated) noexcept {setAttrib(GLFW_DECORATED, decorated ? GLFW_TRUE : GLFW_FALSE);}
            // Allows or forbids user resizing.
            void setResizable(bool resizable) noexcept {setAttrib(GLFW_RESIZABLE, resizable ? GLFW_TRUE : GLFW_FALSE);}

            // Reads the window's DPI content scale.
            void getContentScale(float* xScale, float* yScale) const noexcept {glfwGetWindowContentScale(_window, xScale, yScale);}
            // The window's whole-window opacity, 1 when unsupported.
            float getOpacity() const noexcept {return glfwGetWindowOpacity(_window);}

            // Sets whole-window opacity, where the backend supports it.
            void setOpacity(float opacity) noexcept {
                if (!capabilities().window_opacity) {
                    Platform::glfw_warn_feature_unsupported("window opacity", _windowManager->backend());
                    return;
                }
                glfwSetWindowOpacity(_window, opacity);
                _config.opacity = opacity;
                if (opacity < 0.999f && glfwGetWindowOpacity(_window) > 0.999f) {
                    log_warn("setOpacity had no effect: this system has no whole-window transparency");
                }
            }

            // Requests user attention. On Wayland and X11 this needs compositor or
            // window-manager support and silently does nothing without it.
            void requestAttention() noexcept {
                glfwRequestWindowAttention(_window);
            }
        #endif

        #if GLFW_TARGET_VERSION >= 34
            // True when mouse input passes through the window.
            bool isMousePassthrough() const noexcept {return getAttrib(GLFW_MOUSE_PASSTHROUGH);}
            // The window title as GLFW currently reports it.
            std::string queryTitle() const noexcept {
                const char* title = glfwGetWindowTitle(_window);
                return title ? std::string(title) : std::string();
            }
        #endif

    private:
        friend class WindowManager;

        struct WindowRect {int x = 0, y = 0, width = 0, height = 0;};

        inline static unsigned int s_nextId = 0;
        unsigned int _id = s_nextId++;

        // Remembers the current rectangle for a later return to windowed mode.
        void saveWindowRect() noexcept {
            int x = 0, y = 0;
            if (capabilities().window_position) glfwGetWindowPos(_window, &x, &y);
            _savedRect = {x, y, w_width(), w_height()};
        }

        // Puts back decorations stripped for fullscreen, re-mapping where the
        // window manager only re-reads the hint on map.
        void restoreDecorations(int xpos, int ypos) noexcept {
            #if GLFW_TARGET_VERSION >= 33
                if (!_strippedDecorations || isFullscreen()) return;
                _strippedDecorations = false;
                setAttrib(GLFW_DECORATED, GLFW_TRUE);

                if (bugs().decoration_attrib_needs_remap && isVisible() && !isIconified()) {
                    glfwHideWindow(_window);
                    glfwShowWindow(_window);
                    if (capabilities().window_position) setPos(xpos, ypos);
                }
            #else
                (void)xpos; (void)ypos;
            #endif
        }

        static constexpr int POSITION_SENTINEL = -30000;
        static constexpr int MAP_POLL_ATTEMPTS = 10;
        static constexpr double MAP_POLL_TIMEOUT = 0.01;
        static constexpr int DEFAULT_WINDOWED_WIDTH = 1280;
        static constexpr int DEFAULT_WINDOWED_HEIGHT = 720;

        Window(std::shared_ptr<WindowManager> windowManager, GLFWwindow* window,
               WindowConfig config, GLState glContext, bool expectVisible) noexcept
            : _windowManager(std::move(windowManager)),
              _window(window),
              _config(std::move(config)),
              _glContext(std::move(glContext)),
              _expectVisible(expectVisible) {
            glfwSetWindowUserPointer(_window, this);
            _input.attach(_window, _windowManager->capabilities().cursor_warp_any_mode);
            _windowManager->registerWindow(this);
            waitUntilMapped();
            installCallbacks();

            #if IMGUI_ENABLED
                if (_config.createImGuiContext) {if (!autoInstallImGuiContext()) {log_warn("Disabling imGui functionality");}}
            #endif
        }

        // Pumps events until the window maps, where state settles asynchronously.
        // Runs before callbacks install, so none can fire on a half-built window.
        void waitUntilMapped() noexcept {
            if (!_windowManager->bugs().window_state_settles_async) return;
            if (!_expectVisible) return;

            for (int i = 0; i < MAP_POLL_ATTEMPTS && !glfwGetWindowAttrib(_window, GLFW_VISIBLE); ++i) {
                #if GLFW_TARGET_VERSION >= 32
                    glfwWaitEventsTimeout(MAP_POLL_TIMEOUT);
                #else
                    glfwPollEvents();
                #endif
            }
        }

        // Logs and fails when a call needs this window's context to be current.
        bool requireCurrentContext(const char* caller) const noexcept {
            if (isContextCurrent()) return true;
            log_error(caller, " called without window in current context");
            return false;
        }

        // The usable area of a monitor: its work area where GLFW reports one.
        static void monitorArea(const MonitorInfo& monitor, int& x, int& y, int& width, int& height) noexcept {
            #if GLFW_TARGET_VERSION >= 33
                x = monitor.workareaPos.x;
                y = monitor.workareaPos.y;
                width = monitor.workareaSize.width;
                height = monitor.workareaSize.height;
            #else
                x = monitor.pos.x;
                y = monitor.pos.y;
                width = monitor.currentMode.width;
                height = monitor.currentMode.height;
            #endif
        }

        // Refreshes the cached refresh rate after a monitor change.
        void onMonitorChanged() noexcept {
            const MonitorInfo* info = _config.monitor ? _windowManager->monitorFor(_config.monitor)
                                                        : _windowManager->primaryMonitor();
            if (info) _config.refresh_rate = static_cast<uint32_t>(info->currentMode.refreshRate);
            if (_callbacks.onMonitorChange) _callbacks.onMonitorChange();
        }

        // The Window owning a GLFW handle, via its user pointer.
        static Window* from(GLFWwindow* window) noexcept {
            return static_cast<Window*>(glfwGetWindowUserPointer(window));
        }

        // Registers every GLFW callback this class forwards.
        void installCallbacks() noexcept {
            glfwSetWindowPosCallback(_window, &Window::cbWindowPos);
            glfwSetWindowSizeCallback(_window, &Window::cbWindowSize);
            glfwSetFramebufferSizeCallback(_window, &Window::cbFramebufferSize);
            glfwSetWindowCloseCallback(_window, &Window::cbWindowClose);
            glfwSetWindowRefreshCallback(_window, &Window::cbWindowRefresh);
            glfwSetWindowFocusCallback(_window, &Window::cbWindowFocus);
            glfwSetWindowIconifyCallback(_window, &Window::cbWindowIconify);

            glfwSetKeyCallback(_window, &Window::cbKey);
            glfwSetCharCallback(_window, &Window::cbChar);
            glfwSetMouseButtonCallback(_window, &Window::cbMouseButton);
            glfwSetCursorPosCallback(_window, &Window::cbCursorPos);
            glfwSetCursorEnterCallback(_window, &Window::cbCursorEnter);
            glfwSetScrollCallback(_window, &Window::cbScroll);

            #if GLFW_TARGET_VERSION >= 31
                glfwSetDropCallback(_window, &Window::cbDrop);
            #endif
            #if GLFW_TARGET_VERSION >= 33
                glfwSetWindowMaximizeCallback(_window, &Window::cbWindowMaximise);
                glfwSetWindowContentScaleCallback(_window, &Window::cbWindowContentScale);
            #endif
        }

        static void cbWindowPos(GLFWwindow* handle, int xpos, int ypos) noexcept;
        static void cbWindowSize(GLFWwindow* handle, int width, int height) noexcept;
        static void cbFramebufferSize(GLFWwindow* handle, int width, int height) noexcept;
        static void cbWindowClose(GLFWwindow* handle) noexcept;
        static void cbWindowRefresh(GLFWwindow* handle) noexcept;
        static void cbWindowFocus(GLFWwindow* handle, int focused) noexcept;
        static void cbWindowIconify(GLFWwindow* handle, int iconified) noexcept;

        static void cbKey(GLFWwindow* handle, int key, int scancode, int action, int mods) noexcept;
        static void cbChar(GLFWwindow* handle, unsigned int codepoint) noexcept;
        static void cbMouseButton(GLFWwindow* handle, int button, int action, int mods) noexcept;
        static void cbCursorPos(GLFWwindow* handle, double x, double y) noexcept;
        static void cbCursorEnter(GLFWwindow* handle, int entered) noexcept;
        static void cbScroll(GLFWwindow* handle, double xoffset, double yoffset) noexcept;

        #if GLFW_TARGET_VERSION >= 31
            static void cbDrop(GLFWwindow* handle, int count, const char** paths) noexcept;
        #endif
        #if GLFW_TARGET_VERSION >= 33
            static void cbWindowMaximise(GLFWwindow* handle, int maximized) noexcept;
            static void cbWindowContentScale(GLFWwindow* handle, float xscale, float yscale) noexcept;
        #endif

        std::shared_ptr<WindowManager> _windowManager;
        GLFWwindow* _window = nullptr;
        WindowConfig _config;
        GLState _glContext;
        WindowCallbacks _callbacks;
        Input _input;
        bool _viewportDirty = false;
        bool _expectVisible = true;
        WindowRect _savedRect;
        #if GLFW_TARGET_VERSION >= 33
            bool _strippedDecorations = false;
        #endif
        #if IMGUI_ENABLED
            std::unique_ptr<ImguiContext> _imguiContext = nullptr;
        #endif
};

// True when every registered window has been asked to close.
inline bool WindowManager::allWindowsClosed() const noexcept {
    for (const Window* window : _windows) {
        if (window->isOpen()) return false;
    }
    return true;
}

//Window callbacks

inline void Window::cbWindowPos(GLFWwindow* handle, int xpos, int ypos) noexcept {
    Window* self = from(handle);
    if (!self) return;
    self->_config.pos = {xpos, ypos};
    if (self->_callbacks.onWindowPos) self->_callbacks.onWindowPos(xpos, ypos);
}

inline void Window::cbWindowSize(GLFWwindow* handle, int width, int height) noexcept {
    Window* self = from(handle);
    if (!self) return;
    self->_config.size = {width, height};
    if (self->_callbacks.onWindowSize) self->_callbacks.onWindowSize(width, height);
}

inline void Window::cbFramebufferSize(GLFWwindow* handle, int width, int height) noexcept {
    Window* self = from(handle);
    if (!self) return;
    self->_config.framebufferSize = {width, height};
    self->_viewportDirty = true;
    if (self->_callbacks.onFramebufferSize) self->_callbacks.onFramebufferSize(width, height);
}

inline void Window::cbWindowClose(GLFWwindow* handle) noexcept {
    Window* self = from(handle);
    if (self && self->_callbacks.onWindowClose) self->_callbacks.onWindowClose();
}

inline void Window::cbWindowRefresh(GLFWwindow* handle) noexcept {
    Window* self = from(handle);
    if (self && self->_callbacks.onWindowRefresh) self->_callbacks.onWindowRefresh();
}

inline void Window::cbWindowFocus(GLFWwindow* handle, int focused) noexcept {
    Window* self = from(handle);
    if (!self) return;
    #if IMGUI_ENABLED
        if (self->hasImGui() && ImguiTools::backend_ready()) {ImGui_ImplGlfw_WindowFocusCallback(handle, focused);}
    #endif
    self->_input.onFocus(focused == GLFW_TRUE);
    if (self->_callbacks.onWindowFocus) self->_callbacks.onWindowFocus(focused == GLFW_TRUE);
}

inline void Window::cbWindowIconify(GLFWwindow* handle, int iconified) noexcept {
    Window* self = from(handle);
    if (self && self->_callbacks.onWindowIconify) self->_callbacks.onWindowIconify(iconified == GLFW_TRUE);
}

inline void Window::cbKey(GLFWwindow* handle, int key, int scancode, int action, int mods) noexcept {
    Window* self = from(handle);
    if (!self) return;
    #if IMGUI_ENABLED
        if (self->hasImGui() && ImguiTools::backend_ready()) {ImGui_ImplGlfw_KeyCallback(handle, key, scancode, action, mods);}
    #endif
    self->_input.onKey(key, action, mods);
    if (self->_callbacks.onKey) self->_callbacks.onKey(key, scancode, action, mods);
}

inline void Window::cbChar(GLFWwindow* handle, unsigned int codepoint) noexcept {
    Window* self = from(handle);
    if (!self) return;
    #if IMGUI_ENABLED
        if (self->hasImGui() && ImguiTools::backend_ready()) {ImGui_ImplGlfw_CharCallback(handle, codepoint);}
    #endif
    self->_input.onChar(codepoint);
    if (self->_callbacks.onChar) self->_callbacks.onChar(codepoint);
}

inline void Window::cbMouseButton(GLFWwindow* handle, int button, int action, int mods) noexcept {
    Window* self = from(handle);
    if (!self) return;
    #if IMGUI_ENABLED
        if (self->hasImGui() && ImguiTools::backend_ready()) {ImGui_ImplGlfw_MouseButtonCallback(handle, button, action, mods);}
    #endif
    self->_input.onMouseButton(button, action, mods);
    if (self->_callbacks.onMouseButton) self->_callbacks.onMouseButton(button, action, mods);
}

inline void Window::cbCursorPos(GLFWwindow* handle, double x, double y) noexcept {
    Window* self = from(handle);
    if (!self) return;
    #if IMGUI_ENABLED
        if (self->hasImGui() && ImguiTools::backend_ready()) {ImGui_ImplGlfw_CursorPosCallback(handle, x, y);}
    #endif
    self->_input.onCursorPos(x, y);
    if (self->_callbacks.onCursorPos) self->_callbacks.onCursorPos(x, y);
}

inline void Window::cbCursorEnter(GLFWwindow* handle, int entered) noexcept {
    Window* self = from(handle);
    if (!self) return;
    #if IMGUI_ENABLED
        if (self->hasImGui() && ImguiTools::backend_ready()) {ImGui_ImplGlfw_CursorEnterCallback(handle, entered);}
    #endif
    self->_input.onCursorEnter(entered == GLFW_TRUE);
    if (self->_callbacks.onCursorEnter) self->_callbacks.onCursorEnter(entered == GLFW_TRUE);
}

inline void Window::cbScroll(GLFWwindow* handle, double xoffset, double yoffset) noexcept {
    Window* self = from(handle);
    if (!self) return;
    #if IMGUI_ENABLED
        if (self->hasImGui() && ImguiTools::backend_ready()) {ImGui_ImplGlfw_ScrollCallback(handle, xoffset, yoffset);}
    #endif
    self->_input.onScroll(xoffset, yoffset);
    if (self->_callbacks.onScroll) self->_callbacks.onScroll(xoffset, yoffset);
}

#if GLFW_TARGET_VERSION >= 31
    inline void Window::cbDrop(GLFWwindow* handle, int count, const char** paths) noexcept {
        Window* self = from(handle);
        if (!self) return;
        std::vector<std::string> dropped;
        dropped.reserve(static_cast<size_t>(count < 0 ? 0 : count));
        for (int i = 0; i < count; ++i) {if (paths[i]) dropped.emplace_back(paths[i]);}
        if (self->_callbacks.onDrop) self->_callbacks.onDrop(dropped);
        self->_input.onDrop(std::move(dropped));
    }
#endif

#if GLFW_TARGET_VERSION >= 33
    inline void Window::cbWindowMaximise(GLFWwindow* handle, int maximized) noexcept {
        Window* self = from(handle);
        if (self && self->_callbacks.onWindowMaximise) self->_callbacks.onWindowMaximise(maximized == GLFW_TRUE);
    }

    inline void Window::cbWindowContentScale(GLFWwindow* handle, float xscale, float yscale) noexcept {
        Window* self = from(handle);
        if (self && self->_callbacks.onWindowContentScale) self->_callbacks.onWindowContentScale(xscale, yscale);
    }
#endif

inline void WindowManager::cbMonitor(GLFWmonitor* monitor, int event) noexcept {
    #if IMGUI_ENABLED
        // Installed by createShared, so this can fire before any ImGui context
        // exists; the backend dereferences its own state unconditionally.
        if (ImguiTools::backend_ready()) ImGui_ImplGlfw_MonitorCallback(monitor, event);
    #endif
    if (!s_current) return;

    if (event == GLFW_DISCONNECTED) {
        for (Window* window : s_current->_windows) {
            if (window->_config.monitor == monitor) window->_config.monitor = nullptr;
        }
    }
    s_current->refreshMonitors();
    if (s_current->_callbacks.onMonitor) s_current->_callbacks.onMonitor(monitor, event);
    for (Window* window : s_current->_windows) window->onMonitorChanged();
}

#if GLFW_TARGET_VERSION >= 32
    inline void WindowManager::cbJoystick(int jid, int event) noexcept {
        if (!s_current) return;
        if (s_current->_callbacks.onJoystick) s_current->_callbacks.onJoystick(jid, event);
    }
#endif