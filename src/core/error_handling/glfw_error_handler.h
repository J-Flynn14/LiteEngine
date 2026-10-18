#pragma once
#include "engine_config.h"
#include "logs.h"
#include <string>

class GLFWErrorHandler {
    public:
        // Routes GLFW errors through this class's callback; safe before glfwInit.
        static void install() noexcept {glfwSetErrorCallback(&GLFWErrorHandler::callback);}

        // Removes the error callback.
        static void uninstall() noexcept {glfwSetErrorCallback(nullptr);}

        // Name of a GLFW error code, for logs.
        static const char* name(int code) noexcept {
            switch (code) {
                #ifdef GLFW_NO_ERROR
                    case GLFW_NO_ERROR:        return "NO_ERROR";
                #endif
                case GLFW_NOT_INITIALIZED:     return "NOT_INITIALIZED";
                case GLFW_NO_CURRENT_CONTEXT:  return "NO_CURRENT_CONTEXT";
                case GLFW_INVALID_ENUM:        return "INVALID_ENUM";
                case GLFW_INVALID_VALUE:       return "INVALID_VALUE";
                case GLFW_OUT_OF_MEMORY:       return "OUT_OF_MEMORY";
                case GLFW_API_UNAVAILABLE:     return "API_UNAVAILABLE";
                case GLFW_VERSION_UNAVAILABLE: return "VERSION_UNAVAILABLE";
                case GLFW_PLATFORM_ERROR:      return "PLATFORM_ERROR";
                case GLFW_FORMAT_UNAVAILABLE:  return "FORMAT_UNAVAILABLE";
                #ifdef GLFW_NO_WINDOW_CONTEXT
                    case GLFW_NO_WINDOW_CONTEXT:   return "NO_WINDOW_CONTEXT";
                #endif
                #if GLFW_TARGET_VERSION >= 34
                    case GLFW_CURSOR_UNAVAILABLE:    return "CURSOR_UNAVAILABLE";
                    case GLFW_FEATURE_UNAVAILABLE:   return "FEATURE_UNAVAILABLE";
                    case GLFW_FEATURE_UNIMPLEMENTED: return "FEATURE_UNIMPLEMENTED";
                    case GLFW_PLATFORM_UNAVAILABLE:  return "PLATFORM_UNAVAILABLE";
                #endif
                default:                       return "UNKNOWN";
            }
        }

        // Silently drains the GLFW error queue (a no-op before GLFW 3.3).
        static void clear() noexcept {
            #if GLFW_TARGET_VERSION >= 33
                const char* ignored = nullptr;
                while (glfwGetError(&ignored) != GLFW_NO_ERROR) {}
            #endif
        }

        // Last queued error code and its description (empty before GLFW 3.3).
        static int last(std::string& description) noexcept {
            #if GLFW_TARGET_VERSION >= 33
                const char* text = nullptr;
                const int code = glfwGetError(&text);
                description = text ? std::string(text) : std::string();
                return code;
            #else
                description.clear();
                return 0;
            #endif
        }

        // Last queued error code, discarding the description.
        static int last() noexcept {
            std::string ignored;
            return last(ignored);
        }

        // True when a code reports a missing platform feature rather than a fault.
        // Before 3.4 there is no such distinction, so nothing is downgraded.
        static bool indicatesUnsupported(int code) noexcept {
            #if GLFW_TARGET_VERSION >= 34
                return code == GLFW_FEATURE_UNAVAILABLE || code == GLFW_FEATURE_UNIMPLEMENTED;
            #else
                (void)code;
                return false;
            #endif
        }

        GLFWErrorHandler() = delete;
        GLFWErrorHandler(const GLFWErrorHandler&) = delete;
        GLFWErrorHandler& operator=(const GLFWErrorHandler&) = delete;
        GLFWErrorHandler(GLFWErrorHandler&&) = delete;
        GLFWErrorHandler& operator=(GLFWErrorHandler&&) = delete;
    private:
        // Receives GLFW errors; missing-feature codes warn, everything else errors.
        static void callback(int code, const char* description) noexcept {
            const char* text = description ? description : "(no description)";
            if (indicatesUnsupported(code)) {
                log_warn("GLFW ", name(code), ": ", text);
                return;
            }
            log_error("GLFW ", name(code), " (", code, "): ", text);
        }
};