#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <unordered_map>
#include <utility>
#include <tuple>

#include "logs.h"
#include "timer.h"
#include "window.h"

// Convenience wrapper around a WindowManager and the windows it owns.
// Starts with one main window; add more with addWindow().
//
// In an App the actual swap interval of each window is managed by the App:
// one window is vsynced to pace the loop and the rest run with interval 0, so
// N windows don't divide the frame rate. Window::vsync() acts as "this window
// would like vsync". Calling Window::setVsync() directly mid-loop affects that
// context, but the pacing window is only re-picked when a window is added or closed.
class App {
    public:
        // Upper bound on a frame's delta time, in seconds. Longer frames (a stall,
        // a minimised window, a debugger break) count as this long, so physics
        // slows briefly instead of jumping.
        static constexpr float MAX_DELTA_TIME = 0.1f;

        // Creates the WindowManager and the main window; null on failure.
       static std::unique_ptr<App> create(WindowConfig windowConfig = WindowConfig{},
                                           WindowManagerConfig managerConfig = WindowManagerConfig{}) {
            auto windowManager = WindowManager::createShared(managerConfig);
            if (!windowManager) {log_error("App received null WindowManager in create()"); return nullptr;}

            std::unique_ptr<App> app(new App(std::move(windowManager)));
            Window* main = app->addWindow(std::move(windowConfig));
            if (!main) {log_error("App failed to create its main window"); return nullptr;}

            app->_mainId = main->id();
            return app;
        }
        // Runs every window until all have closed. Closed windows are destroyed
        // between frames. Windows without an entry in the map run with no callbacks.
        void mainLoop(const std::unordered_map<unsigned int, FrameCallbacks>& callbacksPerWindow = {}) {
            static const FrameCallbacks none{};
            Timer& timer = _windowManager->timer();

            _pacer = applyVsyncPolicy();
            if (const Window* main = mainWindow(); main && main->refresh_rate()) {
                timer.setTargetFPS(main->refresh_rate());
            }
            bool wasTimerPaced = false;
            timer.tick(); 

            while (!_windows.empty()) {
                updateDeltaTime(timer);

                if (allWindowsIconified()) WindowManager::waitEvents();
                else                       WindowManager::pollEvents();

                for (const auto& [id, window] : _windows) {
                    if (!window->isOpen()) continue;
                    auto it = callbacksPerWindow.find(id);
                    window->runFrame(it != callbacksPerWindow.end() ? it->second : none, deltaTime());
                }

             
                const bool vsyncPaced = _pacer && _pacer->isOpen() && !_pacer->isIconified();
                if (!vsyncPaced) {
                    if (!wasTimerPaced) timer.resync();
                    timer.sleepUntilNextFrame();
                }
                wasTimerPaced = !vsyncPaced;

           
                const size_t before = _windows.size();
                for (auto it = _windows.begin(); it != _windows.end(); ) {
                    if (!it->second->isOpen()) it = _windows.erase(it);
                    else                       ++it;
                }
                if (_windows.size() != before) _pacer = applyVsyncPolicy();
            }
        }

        // Runs the main window until it closes. Single-window apps only:
        // use the per-window overload when more than one window exists.
        // Callbacks must not throw: Window::runFrame is noexcept.
        void mainLoop(const FrameCallbacks& callbacks) {
            if (_windows.size() != 1) {
                log_error("mainLoop(FrameCallbacks) requires exactly one window, found ",
                          _windows.size(), "; use the per-window mainLoop instead");
                return;
            }

            Window* window = mainWindow();
            if (!window) {log_error("mainLoop(FrameCallbacks): the main window no longer exists"); return;}

         
            window->makeContextCurrent();
            window->setVsync(window->vsync());
            _pacer = window->vsync() ? window : nullptr;

            Timer& timer = _windowManager->timer();
            if (window->refresh_rate()) timer.setTargetFPS(window->refresh_rate());
            bool wasTimerPaced = false;
            timer.tick();  

            log_info("vsync: ", window->vsync(),", refresh_rate: ", window->refresh_rate(),", timer target: ", timer.targetFPS());

            while (window->isOpen()) {
                updateDeltaTime(timer);

                const bool iconified = window->isIconified();   
                if (iconified) WindowManager::waitEvents();
                else           WindowManager::pollEvents();

                window->runFrame(callbacks, _deltaTime);

                // Vsync already blocks in the buffer swap, so the timer only paces
                // frames when nothing else does; sleeping on both drops frames.
                const bool timerPaced = !iconified && !_pacer;
                if (timerPaced) {
                    if (!wasTimerPaced) timer.resync();
                    timer.sleepUntilNextFrame();
                }
                wasTimerPaced = timerPaced;
            }

            _pacer = nullptr;
            _windows.erase(window->id());   
        }

        // Creates a window owned by the App; null on failure.
        Window* addWindow(WindowConfig config = WindowConfig{}) {
            auto window = Window::create(_windowManager, std::move(config));
            if (!window) {log_error("App failed to create a window"); return nullptr;}

            Window* raw = window.get();
            _windows.try_emplace(raw->id(), std::move(window));
            return raw;
        }

        // The window with this ID, or null if none (e.g. it was closed).
        Window* findWindow(unsigned int id) const noexcept {
            auto it = _windows.find(id);
            return it != _windows.end() ? it->second.get() : nullptr;
        }

        // The window with this ID. Throws std::out_of_range if none.
        Window& getWindow(unsigned int id) const {return *_windows.at(id);}

        // The window created by App::create, or null once it has closed.
        Window* mainWindow() const noexcept {return findWindow(_mainId);}

        // Number of windows the App currently owns.
        size_t windowCount() const noexcept {return _windows.size();}

        // Seconds the last frame took, clamped to MAX_DELTA_TIME. Use this for simulation.
        float deltaTime() const noexcept {return _deltaTime;}

        // Unclamped seconds the last frame took, for FPS counters and profiling.
        float rawDeltaTime() const noexcept {return _rawDeltaTime;}

        // The shared WindowManager.
        WindowManager& manager() const noexcept {return *_windowManager;}

        App(const App&) = delete;
        App& operator=(const App&) = delete;
        App(App&&) = delete;
        App& operator=(App&&) = delete;

    private:
        explicit App(std::shared_ptr<WindowManager> windowManager) noexcept
            : _windowManager(std::move(windowManager)) {}

        // Ticks the timer once per loop iteration and clamps the result.
        void updateDeltaTime(Timer& timer) noexcept {
            _rawDeltaTime = timer.tick();
            _deltaTime = std::min(_rawDeltaTime, MAX_DELTA_TIME);
        }

        // True when every window is minimised (false when there are none).
        bool allWindowsIconified() const noexcept {
            for (const auto& [id, window] : _windows) {
                if (!window->isIconified()) return false;
            }
            return !_windows.empty();
        }

        // Vsyncs one window (the main one if it wants vsync) and sets every other
        // window's swap interval to 0. Returns the pacing window, or null when no
        // window wants vsync.
        Window* applyVsyncPolicy() noexcept {
            Window* pacer = mainWindow();
            if (!pacer || !pacer->vsync()) {
                pacer = nullptr;
                for (const auto& [id, window] : _windows) {
                    if (window->vsync()) {pacer = window.get(); break;}
                }
            }

            GLFWwindow* previous = glfwGetCurrentContext();
            for (const auto& [id, window] : _windows) {
                glfwMakeContextCurrent(window->getHandle());  
                glfwSwapInterval(window.get() == pacer ? 1 : 0);
            }
            glfwMakeContextCurrent(previous);
            return pacer;
        }

        std::shared_ptr<WindowManager> _windowManager;
        std::unordered_map<unsigned int, std::unique_ptr<Window>> _windows;
        unsigned int _mainId = 0;
        Window* _pacer = nullptr;
        float _deltaTime = 0.0f;
        float _rawDeltaTime = 0.0f;
};