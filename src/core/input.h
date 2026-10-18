#pragma once

#include <array>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include <glm/glm.hpp>

#include "engine_config.h"
#include "logs.h"

// Every function here must be called on the main thread: GLFW requires it, and
// on macOS calling window or event functions from another thread aborts.

#if GLFW_TARGET_VERSION >= 31
    class Cursor {
        public:
            // Creates a standard system cursor shape; null when unavailable.
            static std::unique_ptr<Cursor> standard(int shape) noexcept {
                GLFWcursor* handle = glfwCreateStandardCursor(shape);
                if (!handle) {
                    log_warn("Cursor: the platform has no standard cursor for shape 0x",
                             std::hex, shape, std::dec);
                    return nullptr;
                }
                return std::unique_ptr<Cursor>(new Cursor(handle));
            }

            // Creates a cursor from an image with a hotspot; null on failure.
            static std::unique_ptr<Cursor> custom(const GLFWimage* image, int xhot, int yhot) noexcept {
                if (!image) {log_error("Cursor::custom given a null image"); return nullptr;}
                GLFWcursor* handle = glfwCreateCursor(image, xhot, yhot);
                if (!handle) {log_warn("Cursor: the platform rejected a custom cursor image"); return nullptr;}
                return std::unique_ptr<Cursor>(new Cursor(handle));
            }

            // The underlying GLFW cursor handle.
            GLFWcursor* handle() const noexcept {return _cursor;}

            ~Cursor() {if (_cursor) glfwDestroyCursor(_cursor);}

            Cursor(const Cursor&) = delete;
            Cursor& operator=(const Cursor&) = delete;
            Cursor(Cursor&& other) noexcept : _cursor(std::exchange(other._cursor, nullptr)) {}

            Cursor& operator=(Cursor&& other) noexcept {
                if (this != &other) {
                    if (_cursor) glfwDestroyCursor(_cursor);
                    _cursor = std::exchange(other._cursor, nullptr);
                }
                return *this;
            }
        private:
            explicit Cursor(GLFWcursor* cursor) noexcept : _cursor(cursor) {}
            GLFWcursor* _cursor = nullptr;
    };
#endif

struct Joystick {
    // True when a joystick is connected at the given id.
    static bool isPresent(int jid) noexcept {return glfwJoystickPresent(jid) == GLFW_TRUE;}

    // The joystick's reported name, or empty when absent.
    static std::string name(int jid) noexcept {
        const char* text = glfwGetJoystickName(jid);
        return text ? std::string(text) : std::string();
    }

    // Current axis values, or empty when the joystick is absent.
    static std::vector<float> axes(int jid) noexcept {
        int count = 0;
        const float* values = glfwGetJoystickAxes(jid, &count);
        if (!values || count <= 0) return {};
        return std::vector<float>(values, values + count);
    }

    // Current button states, or empty when the joystick is absent.
    static std::vector<unsigned char> buttons(int jid) noexcept {
        int count = 0;
        const unsigned char* values = glfwGetJoystickButtons(jid, &count);
        if (!values || count <= 0) return {};
        return std::vector<unsigned char>(values, values + count);
    }

    // Ids of every currently connected joystick.
    static std::vector<int> connected() noexcept {
        std::vector<int> ids;
        for (int jid = GLFW_JOYSTICK_1; jid <= GLFW_JOYSTICK_LAST; ++jid) {
            if (isPresent(jid)) ids.push_back(jid);
        }
        return ids;
    }

    #if GLFW_TARGET_VERSION >= 33
        // Current hat states, or empty when the joystick is absent.
        static std::vector<unsigned char> hats(int jid) noexcept {
            int count = 0;
            const unsigned char* values = glfwGetJoystickHats(jid, &count);
            if (!values || count <= 0) return {};
            return std::vector<unsigned char>(values, values + count);
        }

        // True when the joystick has a gamepad mapping.
        static bool isGamepad(int jid) noexcept {return glfwJoystickIsGamepad(jid) == GLFW_TRUE;}

        // The joystick's SDL-style GUID, used to match gamepad mappings.
        static std::string guid(int jid) noexcept {
            const char* text = glfwGetJoystickGUID(jid);
            return text ? std::string(text) : std::string();
        }

        // The mapped gamepad name, or empty when unmapped.
        static std::string gamepadName(int jid) noexcept {
            const char* text = glfwGetGamepadName(jid);
            return text ? std::string(text) : std::string();
        }

        // Reads the mapped gamepad state; false when unmapped or absent.
        static bool gamepadState(int jid, GLFWgamepadstate& out) noexcept {
            return glfwGetGamepadState(jid, &out) == GLFW_TRUE;
        }

        // Adds gamepad mappings in SDL_GameControllerDB format.
        static bool updateMappings(const std::string& mappings) noexcept {
            return glfwUpdateGamepadMappings(mappings.c_str()) == GLFW_TRUE;
        }
    #endif
};

class Input {
    public:
        // Records a key event; GLFW_REPEAT is ignored so held state stays true.
        void onKey(int key, int action, int mods) noexcept {
            _mods = mods;
            if (key < 0 || key >= MAX_KEYS) return;
            if (action == GLFW_PRESS) {
                _keyHeld[key] = true;
                _keyPressed[key] = true;
            } else if (action == GLFW_RELEASE) {
                _keyHeld[key] = false;
                _keyReleased[key] = true;
            }
        }

        // Records a mouse-button event.
        void onMouseButton(int button, int action, int mods) noexcept {
            _mods = mods;
            if (button < 0 || button >= MAX_MOUSE_BUTTONS) return;
            if (action == GLFW_PRESS) {
                _mouseHeld[button] = true;
                _mousePressed[button] = true;
            } else if (action == GLFW_RELEASE) {
                _mouseHeld[button] = false;
                _mouseReleased[button] = true;
            }
        }

        // Accumulates scroll offsets for this frame.
        void onScroll(double xOffset, double yOffset) noexcept {
            _scrollDelta.x += static_cast<float>(xOffset);
            _scrollDelta.y += static_cast<float>(yOffset);
        }

        // Records cursor motion, accumulating a delta once a position is known.
        void onCursorPos(double x, double y) noexcept {
            const glm::vec2 next{static_cast<float>(x), static_cast<float>(y)};
            if (_cursorValid) {_cursorDelta += next - _cursorPos;} else {_cursorValid = true;}
            _cursorPos = next;
        }

        // Appends a typed character for this frame.
        void onChar(unsigned int codepoint) noexcept {_textThisFrame.push_back(codepoint);}

        // Records the cursor entering or leaving the window.
        void onCursorEnter(bool entered) noexcept {
            _cursorInside = entered;
            if (!entered) _cursorValid = false;
        }

        // Records focus changes, releasing held input when focus is lost.
        void onFocus(bool focused) noexcept {
            _cursorValid = false;
            if (focused) return;
            _keyHeld.fill(false);
            _mouseHeld.fill(false);
            _mods = 0;
        }

        // Records paths dropped onto the window this frame.
        void onDrop(std::vector<std::string> paths) noexcept {_droppedPaths = std::move(paths);}

        // Clears per-frame edges, deltas and text; call once per frame.
        void endFrame() noexcept {
            _keyPressed.fill(false);
            _keyReleased.fill(false);
            _mousePressed.fill(false);
            _mouseReleased.fill(false);
            _scrollDelta = {0.0f, 0.0f};
            _cursorDelta = {0.0f, 0.0f};
            _textThisFrame.clear();
            _droppedPaths.clear();
        }

        // True while a key is held down. Use glfw key codes
        bool isKeyDown(int key) const noexcept {return (key >= 0 && key < MAX_KEYS) && _keyHeld[key];}
        // True on the frame a key went down. Use glfw key codes
        bool wasKeyPressed(int key) const noexcept {return (key >= 0 && key < MAX_KEYS) && _keyPressed[key];}
        // True on the frame a key came up. Use glfw key codes
        bool wasKeyReleased(int key) const noexcept {return (key >= 0 && key < MAX_KEYS) && _keyReleased[key];}

        // True while a mouse button is held down. Use glfw key codes
        bool isMouseButtonHeld(int button) const noexcept {return (button >= 0 && button < MAX_MOUSE_BUTTONS) && _mouseHeld[button];}
        // True on the frame a mouse button went down. Use glfw key codes
        bool wasMouseButtonPressed(int button) const noexcept {return (button >= 0 && button < MAX_MOUSE_BUTTONS) && _mousePressed[button];}
        // True on the frame a mouse button came up. Use glfw key codes
        bool wasMouseButtonReleased(int button) const noexcept {return (button >= 0 && button < MAX_MOUSE_BUTTONS) && _mouseReleased[button];}

        // Cursor position in window coordinates.
        glm::vec2 cursorPosition() const noexcept {return _cursorPos;}
        // Cursor movement accumulated this frame.
        glm::vec2 cursorDeltaThisFrame() const noexcept {return _cursorDelta;}
        // Scroll offsets accumulated this frame.
        glm::vec2 scrollThisFrame() const noexcept {return _scrollDelta;}
        // True while the cursor is inside the window.
        bool isCursorInside() const noexcept {return _cursorInside;}

        // Modifier bits from the most recent key or button event.
        int modifiers() const noexcept {return _mods;}
        // True when shift was held during the last input event.
        bool isShiftHeld() const noexcept {return (_mods & GLFW_MOD_SHIFT) != 0;}
        // True when control was held during the last input event.
        bool isControlHeld() const noexcept {return (_mods & GLFW_MOD_CONTROL) != 0;}
        // True when alt was held during the last input event.
        bool isAltHeld() const noexcept {return (_mods & GLFW_MOD_ALT) != 0;}
        // True when super/command was held during the last input event.
        bool isSuperHeld() const noexcept {return (_mods & GLFW_MOD_SUPER) != 0;}

        // Unicode codepoints typed this frame.
        const std::vector<unsigned int>& textThisFrame() const noexcept {return _textThisFrame;}
        // File paths dropped onto the window this frame.
        const std::vector<std::string>& droppedPathsThisFrame() const noexcept {return _droppedPaths;}
        // The window this input is attached to, or null.
        GLFWwindow* attachedWindow() const noexcept {return _window;}

        // Polls a key's state directly from GLFW, bypassing the cached edges.
        int getKey(int key) const noexcept {
            if (!_window || key < 0 || key >= MAX_KEYS) return GLFW_RELEASE;
            return glfwGetKey(_window, key);
        }

        // Polls a mouse button's state directly from GLFW.
        int getMouseButton(int button) const noexcept {
            if (!_window || button < 0 || button >= MAX_MOUSE_BUTTONS) return GLFW_RELEASE;
            return glfwGetMouseButton(_window, button);
        }

        // Reads the live cursor position from GLFW.
        void getCursorPos(double* x, double* y) const noexcept {
            if (!_window) {if (x) *x = 0.0; if (y) *y = 0.0; return;}
            glfwGetCursorPos(_window, x, y);
        }

        // Warps the cursor; on Wayland this only works in disabled-cursor mode.
        void setCursorPos(double x, double y) noexcept {
            if (!_window) return;
            if (!_cursorWarpAnyMode && cursorMode() != GLFW_CURSOR_DISABLED) {
                log_warn("Input::setCursorPos: this backend only warps the cursor in disabled-cursor mode");
                return;
            }
            glfwSetCursorPos(_window, x, y);
            _cursorPos = {static_cast<float>(x), static_cast<float>(y)};
            _cursorValid = false;
        }

        // Reads a GLFW input mode for this window.
        int getInputMode(int mode) const noexcept {return _window ? glfwGetInputMode(_window, mode) : 0;}
        // Sets a GLFW input mode for this window.
        void setInputMode(int mode, int value) noexcept {if (_window) glfwSetInputMode(_window, mode, value);}

        // The current cursor mode (normal, hidden, disabled, captured).
        int cursorMode() const noexcept {return getInputMode(GLFW_CURSOR);}
        // Sets the cursor mode and invalidates the cached delta origin.
        void setCursorMode(int mode) noexcept {
            setInputMode(GLFW_CURSOR, mode);
            _cursorValid = false;
        }
        // Shows the cursor and lets it leave the window.
        void setCursorNormal() noexcept {setCursorMode(GLFW_CURSOR_NORMAL);}
        // Hides the cursor while keeping it free to move.
        void setCursorHidden() noexcept {setCursorMode(GLFW_CURSOR_HIDDEN);}
        // Hides and locks the cursor, for mouse-look controls.
        void setCursorDisabled() noexcept {setCursorMode(GLFW_CURSOR_DISABLED);}
        // True while the cursor is locked to the window.
        bool isCursorDisabled() const noexcept {return cursorMode() == GLFW_CURSOR_DISABLED;}

        // True when sticky keys are enabled.
        bool stickyKeys() const noexcept {return getInputMode(GLFW_STICKY_KEYS) == GLFW_TRUE;}
        // Enables or disables sticky keys.
        void setStickyKeys(bool enabled) noexcept {setInputMode(GLFW_STICKY_KEYS, enabled ? GLFW_TRUE : GLFW_FALSE);}
        // True when sticky mouse buttons are enabled.
        bool stickyMouseButtons() const noexcept {return getInputMode(GLFW_STICKY_MOUSE_BUTTONS) == GLFW_TRUE;}
        // Enables or disables sticky mouse buttons.
        void setStickyMouseButtons(bool enabled) noexcept {setInputMode(GLFW_STICKY_MOUSE_BUTTONS, enabled ? GLFW_TRUE : GLFW_FALSE);}

        #if GLFW_TARGET_VERSION >= 31
            // Sets the window's cursor image, or restores the default when null.
            void setCursor(const Cursor* cursor) noexcept {
                if (_window) glfwSetCursor(_window, cursor ? cursor->handle() : nullptr);
            }
        #endif

        #if GLFW_TARGET_VERSION >= 32
            // The layout-specific printable name of a key, for key-binding UI.
            static std::string keyName(int key, int scancode) noexcept {
                const char* text = glfwGetKeyName(key, scancode);
                return text ? std::string(text) : std::string();
            }
        #endif

        #if GLFW_TARGET_VERSION >= 33
            // The platform scancode for a key token.
            static int keyScancode(int key) noexcept {return glfwGetKeyScancode(key);}

            // True when lock-key modifier bits are reported.
            bool lockKeyMods() const noexcept {return getInputMode(GLFW_LOCK_KEY_MODS) == GLFW_TRUE;}
            // Enables or disables lock-key modifier bits.
            void setLockKeyMods(bool enabled) noexcept {setInputMode(GLFW_LOCK_KEY_MODS, enabled ? GLFW_TRUE : GLFW_FALSE);}
            // True when caps lock was on during the last input event.
            bool isCapsLockOn() const noexcept {return (_mods & GLFW_MOD_CAPS_LOCK) != 0;}
            // True when num lock was on during the last input event.
            bool isNumLockOn() const noexcept {return (_mods & GLFW_MOD_NUM_LOCK) != 0;}

            // True when the running system supports raw mouse motion.
            static bool rawMouseMotionSupported() noexcept {return glfwRawMouseMotionSupported() == GLFW_TRUE;}
            // True when raw mouse motion is currently enabled.
            bool rawMouseMotion() const noexcept {return getInputMode(GLFW_RAW_MOUSE_MOTION) == GLFW_TRUE;}
            // Enables raw mouse motion; false when the system cannot provide it.
            bool setRawMouseMotion(bool enabled) noexcept {
                if (enabled && !rawMouseMotionSupported()) return false;
                setInputMode(GLFW_RAW_MOUSE_MOTION, enabled ? GLFW_TRUE : GLFW_FALSE);
                return true;
            }
        #endif

        #if GLFW_TARGET_VERSION >= 34
            // Confines the visible cursor to the window's content area.
            void setCursorCaptured() noexcept {setCursorMode(GLFW_CURSOR_CAPTURED);}
        #endif

        ~Input() = default;

        Input(const Input&) = delete;
        Input& operator=(const Input&) = delete;
    private:
        friend class Window;

        Input() = default;

        // Binds this input to a window and records its cursor-warp capability.
        void attach(GLFWwindow* window, bool cursorWarpAnyMode) noexcept {
            _window = window;
            _cursorWarpAnyMode = cursorWarpAnyMode;
        }

        // Unbinds this input from its window.
        void detach() noexcept {_window = nullptr;}

        static constexpr int MAX_KEYS = GLFW_KEY_LAST + 1;
        static constexpr int MAX_MOUSE_BUTTONS = GLFW_MOUSE_BUTTON_LAST + 1;

        GLFWwindow* _window = nullptr;
        bool _cursorWarpAnyMode = true;

        std::array<bool, MAX_KEYS> _keyHeld{};
        std::array<bool, MAX_KEYS> _keyPressed{};
        std::array<bool, MAX_KEYS> _keyReleased{};

        std::array<bool, MAX_MOUSE_BUTTONS> _mouseHeld{};
        std::array<bool, MAX_MOUSE_BUTTONS> _mousePressed{};
        std::array<bool, MAX_MOUSE_BUTTONS> _mouseReleased{};

        glm::vec2 _cursorPos{0.0f, 0.0f};
        glm::vec2 _cursorDelta{0.0f, 0.0f};
        glm::vec2 _scrollDelta{0.0f, 0.0f};
        bool _cursorInside = false;
        bool _cursorValid = false;
        int _mods = 0;
        std::vector<unsigned int> _textThisFrame;
        std::vector<std::string> _droppedPaths;
};
