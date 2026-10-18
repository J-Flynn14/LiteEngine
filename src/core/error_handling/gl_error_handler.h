#pragma once
#include <string>
#include "../engine_config.h"
#include "../logs.h"

class GlErrorHandler {
    public:
        #if ENGINE_HAS_DEBUG_OUTPUT
            // Enables synchronous GL debug output routed through this class's callback.
            static void install() noexcept {
                glEnable(GL_DEBUG_OUTPUT);
                glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
                glDebugMessageCallback(&GlErrorHandler::callback, nullptr);
            }

            // Removes the callback and disables GL debug output.
            static void uninstall() noexcept {
                glDebugMessageCallback(nullptr, nullptr);
                glDisable(GL_DEBUG_OUTPUT);
            }
        #endif

        // Logs and drains every queued GL error; returns true if any were found.
        static bool logErrors() noexcept {
            bool any = false;
            for (GLenum err = glGetError(); err != GL_NO_ERROR; err = glGetError()) {
                log_error("GL state error: ", name(err));
                any = true;
            }
            return any;
        }

        // True if an error was queued; consumes that one error, so prefer
        // logErrors() when the error itself should also be reported.
        static bool isError() noexcept {
            return glGetError() != GL_NO_ERROR;
        }

        // Name of a GL error code, for logs.
        static const char* name(int code) noexcept {
            switch (code) {
                case GL_NO_ERROR:                        return "NO_ERROR";
                case GL_INVALID_ENUM:                    return "INVALID_ENUM";
                case GL_INVALID_VALUE:                   return "INVALID_VALUE";
                case GL_INVALID_OPERATION:               return "INVALID_OPERATION";
                case GL_OUT_OF_MEMORY:                   return "OUT_OF_MEMORY";
                #ifdef GL_STACK_UNDERFLOW
                    case GL_STACK_UNDERFLOW:             return "STACK_UNDERFLOW";
                #endif
                #ifdef GL_STACK_OVERFLOW
                    case GL_STACK_OVERFLOW:              return "STACK_OVERFLOW";
                #endif
                #if ENGINE_HAS_FRAMEBUFFER
                    case GL_INVALID_FRAMEBUFFER_OPERATION: return "INVALID_FRAMEBUFFER_OPERATION";
                #endif
                #if GL_TARGET_VERSION >= 45
                    case GL_CONTEXT_LOST:                return "CONTEXT_LOST";
                #endif
                default:                                 return "UNKNOWN";
            }
        }

        // Silently drains the GL error queue.
        static void clear() noexcept {
            while (glGetError() != GL_NO_ERROR) {}
        }

        // Last queued error code; glGetError has no text, so the name is the description.
        static int last(std::string& description) noexcept {
            const int code = glGetError();
            description = name(code);
            return code;
        }

        // Last queued error code, discarding the description.
        static int last() noexcept {
            std::string ignored;
            return last(ignored);
        }

        #if ENGINE_HAS_DEBUG_OUTPUT
            // Name of a GL debug-message type, for logs.
            static const char* typeName(unsigned int type) noexcept {
                switch (type) {
                    case GL_DEBUG_TYPE_ERROR:              return "ERROR";
                    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: return "DEPRECATED_BEHAVIOR";
                    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  return "UNDEFINED_BEHAVIOR";
                    case GL_DEBUG_TYPE_PORTABILITY:         return "PORTABILITY";
                    case GL_DEBUG_TYPE_PERFORMANCE:         return "PERFORMANCE";
                    case GL_DEBUG_TYPE_MARKER:              return "MARKER";
                    default:                                return "OTHER";
                }
            }

            // True when a debug-message type reports a portability limitation, not a fault.
            static bool indicatesUnsupported(unsigned int type) noexcept {
                return type == GL_DEBUG_TYPE_PORTABILITY;
            }
        #endif

        GlErrorHandler() = delete;
        GlErrorHandler(const GlErrorHandler&) = delete;
        GlErrorHandler& operator=(const GlErrorHandler&) = delete;
        GlErrorHandler(GlErrorHandler&&) = delete;
        GlErrorHandler& operator=(GlErrorHandler&&) = delete;
    private:
        #if ENGINE_HAS_DEBUG_OUTPUT
            // Receives GL debug messages; portability notes warn, everything else errors.
            static void APIENTRY callback(GLenum source, GLenum type, GLuint id, GLenum severity,
                                          GLsizei length, const GLchar* message, const void* userParam) noexcept {
                (void)source; (void)length; (void)userParam;
                if (severity == GL_DEBUG_SEVERITY_NOTIFICATION) return;

                const char* text = message ? message : "(no description)";
                if (indicatesUnsupported(type)) {
                    log_warn("GL ", typeName(type), ": ", text);
                    return;
                }
                log_error("GL ", typeName(type), " (", id, "): ", text);
            }
        #endif
};
