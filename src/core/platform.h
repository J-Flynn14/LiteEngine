#pragma once

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include <tuple>

#include "engine_config.h"
#include "error_handling/gl_error_handler.h"
#include "logs.h"

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX 1
    #endif
    #include <windows.h>
    #undef near
    #undef far
#elif defined(__linux__)
    #include <limits.h>
    #include <pthread.h>
    #include <unistd.h>
#elif defined(__APPLE__)
    #include <mach-o/dyld.h>
    #include <pthread.h>
#endif

namespace Platform {

    // The running context's GL version as major*10+minor, or 0 if unknown.
    inline int get_gl_version() noexcept {
        int major = 0, minor = 0;
        #if GL_TARGET_VERSION >= 30
            glGetIntegerv(GL_MAJOR_VERSION, &major);
            glGetIntegerv(GL_MINOR_VERSION, &minor);
            while (glGetError() != GL_NO_ERROR) {}
            if (major > 0) return major * 10 + minor;
        #endif
        const GLubyte* raw = glGetString(GL_VERSION);
        if (!raw) return 0;
        if (std::sscanf(reinterpret_cast<const char*>(raw), "%d.%d", &major, &minor) != 2) return 0;
        return major * 10 + minor;
    }

    // The "#version NNN" directive matching GLSL_TARGET_VERSION, without a newline.
    inline const char* get_glsl_version() noexcept {
        static const std::string version = [] {
            std::ostringstream oss;
            oss << "#version " << GLSL_TARGET_VERSION;
            return oss.str();
        }();
        return version.c_str();
    }

    // The same directive as an owned string, for concatenating shader source.
    inline std::string get_glsl_version_str() noexcept {return std::string(get_glsl_version());}

    // Loads GL entry points through glad; needs a current context. Idempotent.
    inline bool load_gl_functions() noexcept {
        static bool loaded = false;
        if (loaded) return true;
        #if ENGINE_GLAD_VERSION == 2
            loaded = gladLoadGL(reinterpret_cast<GLADloadfunc>(glfwGetProcAddress)) != 0;
        #else
            loaded = gladLoadGLLoader(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)) != 0;
        #endif
        return loaded;
    }

    enum class GlfwBackend {Any, Win32, Cocoa, Wayland, X11, Null};

    // Human-readable name of a backend, for logs.
    inline const char* GlfwBackendName(GlfwBackend backend) noexcept {
        switch (backend) {
            case GlfwBackend::Win32:   return "Win32";
            case GlfwBackend::Cocoa:   return "Cocoa";
            case GlfwBackend::Wayland: return "Wayland";
            case GlfwBackend::X11:     return "X11";
            case GlfwBackend::Null:    return "Null";
            case GlfwBackend::Any:     return "Any";
            default:               return "Unknown";
        }
    }

    struct GlfwCapabilities {
        bool window_position = true;   // glfwSet/GetWindowPos (Wayland: no protocol -> false)
        bool window_icon = true;       // glfwSetWindowIcon (Wayland/Cocoa: unsupported -> false)
        bool window_focus = true;      // glfwFocusWindow; Wayland: best-effort via xdg-activation,
                                       // the compositor may deny or ignore the request
        bool window_opacity = true;    // glfwSetWindowOpacity (Wayland: FEATURE_UNAVAILABLE -> false)
        bool window_iconify = true;    // glfwIconifyWindow request works (false only on headless Null)
        bool window_iconify_state = true; // GLFW_ICONIFIED is queryable (Wayland: unknowable -> false)
        bool monitor_gamma = true;     // glfwGet/SetGammaRamp (Wayland: no protocol -> false)
        bool raw_mouse_motion = true;  // runtime-probed via glfwRawMouseMotionSupported (Cocoa: unimplemented)
        bool cursor_warp_any_mode = true;  // glfwSetCursorPos in normal mode (Wayland: only in CURSOR_DISABLED)
    };

    struct GLFWBugs {
        // Wayland (notably WSLg): decorations stay drawn after entering fullscreen, so
        // setFullscreen strips GLFW_DECORATED before the transition and restores it after.
        bool decorations_persist_in_fullscreen = false;

        // X11: re-enabling GLFW_DECORATED updates the attribute but the WM only re-reads
        // _MOTIF_WM_HINTS on map, so restoring decorations needs a hide/show cycle.
        bool decoration_attrib_needs_remap = false;

        // X11: window state settles asynchronously after creation/show; immediate queries
        // can return stale values, so wait for the relevant callback instead of polling.
        bool window_state_settles_async = false;

        // Win32: context creation fails over GDI software OpenGL (e.g. plain Remote
        // Desktop with no GPU driver), so a clear diagnostic beats a bare failure.
        bool gdi_software_gl_context_fails = false;

        // Win32: the modal move/resize loop blocks glfwPollEvents, freezing rendering
        // while the user drags; redraw from the window-refresh callback to compensate.
        bool modal_loop_blocks_event_polling = false;

        // Win32: with GLFW_AUTO_ICONIFY (the default), fullscreen windows minimise on
        // alt-tab / focus loss, which surprises borderless-fullscreen setups.
        bool fullscreen_auto_iconifies_on_focus_loss = false;

        // Cocoa: all window and event calls must run on the main thread; calling
        // glfwPollEvents or window functions from another thread aborts.
        bool requires_main_thread_events = false;

        // Cocoa: window content may not appear until events are pumped after show,
        // so poll events once before relying on the first frame being visible.
        bool first_frame_needs_event_poll = false;

        // Cocoa: OpenGL is deprecated by the OS (since 10.14); expect console warnings
        // and a 4.1 ceiling, with no vendor fixes forthcoming.
        bool opengl_deprecated_by_os = false;

        // Cocoa: moving a window between Retina and non-Retina monitors changes the
        // framebuffer size without a window-size event; handle the fb-size callback.
        bool framebuffer_resizes_on_monitor_change = false;

        // Wayland: buffer swaps can block indefinitely while the surface is hidden or
        // fully occluded (frame-callback driven); do not assume swap returns promptly.
        bool swap_blocks_while_hidden = false;

        // Wayland: server-side decorations need the compositor's protocol (GNOME lacks
        // it); without libdecor GLFW draws only a minimal fallback frame.
        bool needs_libdecor_for_decorations = false;

        // Wayland: the iconified state is unknowable -- requests work but GLFW_ICONIFIED
        // stays false and the iconify callback never fires.
        bool iconified_state_unknowable = false;

        // X11: clipboard reads are answered by the selection owner via events, so they
        // need the event loop pumped and can stall if the owner is unresponsive.
        bool clipboard_needs_event_pump = false;
    };

    // Logs a one-line warning that a feature is unsupported on a backend.
    inline void glfw_warn_feature_unsupported(const std::string& feature, GlfwBackend backend) noexcept {
        log_warn(feature, " is not supported on ", GlfwBackendName(backend));
    }

    // Known per-backend GLFW quirks the engine works around or must document.
    inline GLFWBugs detect_glfw_bugs(GlfwBackend backend) noexcept {
        GLFWBugs bugs;
        switch (backend) {
            case GlfwBackend::Wayland:
                bugs.decorations_persist_in_fullscreen = true;
                bugs.swap_blocks_while_hidden = true;
                bugs.needs_libdecor_for_decorations = true;
                bugs.iconified_state_unknowable = true;
                break;
            case GlfwBackend::X11:
                bugs.decoration_attrib_needs_remap = true;
                bugs.window_state_settles_async = true;
                bugs.clipboard_needs_event_pump = true;
                break;
            case GlfwBackend::Win32:
                bugs.gdi_software_gl_context_fails = true;
                bugs.modal_loop_blocks_event_polling = true;
                bugs.fullscreen_auto_iconifies_on_focus_loss = true;
                break;
            case GlfwBackend::Cocoa:
                bugs.requires_main_thread_events = true;
                bugs.first_frame_needs_event_poll = true;
                bugs.opengl_deprecated_by_os = true;
                bugs.framebuffer_resizes_on_monitor_change = true;
                break;
            default:
                break;
        }
        return bugs;
    }

    // The running windowing backend; before GLFW 3.4 Linux is resolved from the
    // session environment, since 3.3's experimental Wayland has no glfwGetPlatform.
    inline GlfwBackend detect_glfw_backend() noexcept {
        #if GLFW_TARGET_VERSION >= 34
            switch (glfwGetPlatform()) {
                case GLFW_PLATFORM_WIN32:   return GlfwBackend::Win32;
                case GLFW_PLATFORM_COCOA:   return GlfwBackend::Cocoa;
                case GLFW_PLATFORM_WAYLAND: return GlfwBackend::Wayland;
                case GLFW_PLATFORM_X11:     return GlfwBackend::X11;
                case GLFW_PLATFORM_NULL:    return GlfwBackend::Null;
                default:                    return GlfwBackend::Any;
            }
        #elif defined(_WIN32)
            return GlfwBackend::Win32;
        #elif defined(__APPLE__)
            return GlfwBackend::Cocoa;
        #else
            const char* waylandDisplay = std::getenv("WAYLAND_DISPLAY");
            const char* sessionType = std::getenv("XDG_SESSION_TYPE");
            if ((waylandDisplay && *waylandDisplay)
                || (sessionType && std::strcmp(sessionType, "wayland") == 0)) {
                return GlfwBackend::Wayland;
            }
            return GlfwBackend::X11;
        #endif
    }

    #if GLFW_TARGET_VERSION >= 34
        // Maps a GlfwBackend to the matching GLFW_PLATFORM_* init-hint value.
        inline int to_glfw_platform(GlfwBackend backend) noexcept {
            switch (backend) {
                case GlfwBackend::Win32:   return GLFW_PLATFORM_WIN32;
                case GlfwBackend::Cocoa:   return GLFW_PLATFORM_COCOA;
                case GlfwBackend::Wayland: return GLFW_PLATFORM_WAYLAND;
                case GlfwBackend::X11:     return GLFW_PLATFORM_X11;
                case GlfwBackend::Null:    return GLFW_PLATFORM_NULL;
                default:               return GLFW_ANY_PLATFORM;
            }
        }
    #endif

    // Per-backend feature availability, refined by runtime probes where possible.
    inline GlfwCapabilities detect_glfw_capabilities(GlfwBackend backend) noexcept {
        GlfwCapabilities caps;
        switch (backend) {
            // Wayland has no protocol for position, iconify state, gamma or opacity;
            // icons need xdg-toplevel-icon-v1, and warping needs disabled-cursor mode.
            case GlfwBackend::Wayland:
                caps.window_position = false;
                caps.window_icon = false;
                caps.window_iconify_state = false;
                caps.window_opacity = false;
                caps.monitor_gamma = false;
                caps.cursor_warp_any_mode = false;
                break;
            case GlfwBackend::Null:
                // Headless: nothing to drive.
                caps.window_position = false;
                caps.window_icon = false;
                caps.window_focus = false;
                caps.window_opacity = false;
                caps.window_iconify = false;
                caps.window_iconify_state = false;
                caps.monitor_gamma = false;
                caps.cursor_warp_any_mode = false;
                break;
            case GlfwBackend::Cocoa:
                // macOS windows have no icon (the dock uses the bundle icon); raw mouse
                // motion is not implemented on macOS.
                caps.window_icon = false;
                caps.raw_mouse_motion = false;
                break;
            default:
                break;
        }

        #if GLFW_TARGET_VERSION >= 33
            // Even where the platform implements it, the running system may not: probe.
            if (caps.raw_mouse_motion) caps.raw_mouse_motion = glfwRawMouseMotionSupported() == GLFW_TRUE;
        #else
            caps.raw_mouse_motion = false;
        #endif

        return caps;
    }

    struct GLFWInitHints {
        #if GLFW_TARGET_VERSION >= 33
            bool glfw_joystick_hat_buttons = true;
            #ifdef __APPLE__
                bool glfw_cocoa_chdir_resources = true;
                bool glfw_cocoa_menubar = true;
            #endif
        #endif
        #if GLFW_TARGET_VERSION >= 34
            int glfw_platform = GLFW_ANY_PLATFORM;
            int glfw_angle_platform_type = GLFW_ANGLE_PLATFORM_TYPE_NONE;
            int glfw_wayland_libdecor = GLFW_WAYLAND_PREFER_LIBDECOR;
            bool glfw_x11_xcb_vulkan_surface = true;
        #endif
    };

    // Applies init hints; must run before glfwInit.
    inline void apply_glfw_init_hints(const GLFWInitHints& hints) noexcept {
        #if GLFW_TARGET_VERSION >= 33
            glfwInitHint(GLFW_JOYSTICK_HAT_BUTTONS, hints.glfw_joystick_hat_buttons ? GLFW_TRUE : GLFW_FALSE);
            #ifdef __APPLE__
                glfwInitHint(GLFW_COCOA_CHDIR_RESOURCES, hints.glfw_cocoa_chdir_resources ? GLFW_TRUE : GLFW_FALSE);
                glfwInitHint(GLFW_COCOA_MENUBAR, hints.glfw_cocoa_menubar ? GLFW_TRUE : GLFW_FALSE);
            #endif
        #endif
        #if GLFW_TARGET_VERSION >= 34
            if (hints.glfw_platform != GLFW_ANY_PLATFORM && glfwPlatformSupported(hints.glfw_platform)) {
                glfwInitHint(GLFW_PLATFORM, hints.glfw_platform);
            }
            glfwInitHint(GLFW_ANGLE_PLATFORM_TYPE, hints.glfw_angle_platform_type);
            glfwInitHint(GLFW_WAYLAND_LIBDECOR, hints.glfw_wayland_libdecor);
            glfwInitHint(GLFW_X11_XCB_VULKAN_SURFACE, hints.glfw_x11_xcb_vulkan_surface ? GLFW_TRUE : GLFW_FALSE);
        #endif
        #if GLFW_TARGET_VERSION < 33
            (void)hints;
        #endif
    }

    // Current working directory, or "." if it cannot be read.
    inline std::filesystem::path safe_current_path() noexcept {
        std::error_code ec;
        const std::filesystem::path path = std::filesystem::current_path(ec);
        return ec ? std::filesystem::path(".") : path;
    }

    // Directory containing the running executable, per platform. Every fallback
    // goes through safe_current_path: the throwing overload would terminate here.
    inline std::filesystem::path compute_exe_dir() noexcept {
        #if defined(_WIN32)
            std::vector<wchar_t> buffer(MAX_PATH);
            for (;;) {
                const DWORD written = GetModuleFileNameW(nullptr, buffer.data(),
                                                         static_cast<DWORD>(buffer.size()));
                if (written == 0) return safe_current_path();
                if (written < buffer.size()) {
                    return std::filesystem::path(std::wstring(buffer.data(), written)).parent_path();
                }
                buffer.resize(buffer.size() * 2);
            }
        #elif defined(__linux__)
            char buffer[PATH_MAX];
            const ssize_t written = ::readlink("/proc/self/exe", buffer, sizeof(buffer));
            if (written <= 0) return safe_current_path();
            return std::filesystem::path(std::string(buffer, static_cast<size_t>(written))).parent_path();
        #elif defined(__APPLE__)
            uint32_t size = 0;
            _NSGetExecutablePath(nullptr, &size);
            std::vector<char> buffer(size);
            if (_NSGetExecutablePath(buffer.data(), &size) != 0) return safe_current_path();

            std::error_code ec;
            const std::filesystem::path resolved =
                std::filesystem::canonical(std::filesystem::path(buffer.data()), ec);
            if (ec) return std::filesystem::path(buffer.data()).parent_path();
            return resolved.parent_path();
        #else
            return safe_current_path();
        #endif
    }

    // Names the current thread for debuggers/profilers, per platform. Names longer
    // than the platform limit are truncated rather than dropped.
    inline void nameCurrentThread(const char* name) noexcept {
        if (!name || !*name) return;

        #if defined(_WIN32)
            typedef HRESULT(WINAPI* SetThreadDescriptionFn)(HANDLE, PCWSTR);

            HMODULE kernel = GetModuleHandleA("kernel32.dll");
            if (!kernel) kernel = GetModuleHandleA("KernelBase.dll");
            if (!kernel) return;

            const auto setThreadDescription = reinterpret_cast<SetThreadDescriptionFn>(
                reinterpret_cast<void*>(GetProcAddress(kernel, "SetThreadDescription")));
            if (!setThreadDescription) return;

            constexpr int WIDE_CAPACITY = 64;
            wchar_t wide[WIDE_CAPACITY] = {};
            int sourceLength = 0;
            while (name[sourceLength] != '\0' && sourceLength < WIDE_CAPACITY - 1) ++sourceLength;

            const int converted = MultiByteToWideChar(CP_UTF8, 0, name, sourceLength,
                                                      wide, WIDE_CAPACITY - 1);
            if (converted <= 0) return;
            wide[converted] = L'\0';
            setThreadDescription(GetCurrentThread(), wide);
        #elif defined(__linux__)
            char truncated[16];
            std::snprintf(truncated, sizeof(truncated), "%s", name);
            pthread_setname_np(pthread_self(), truncated);
        #elif defined(__APPLE__)
            pthread_setname_np(name);
        #else
            (void)name;
        #endif
    }
}