#pragma once
#include <chrono>
#include <cstdint>
#include <thread>

// Frame-pacing slack. Once the loop is this far behind the schedule is
// abandoned rather than caught up, so a stall cannot cause a burst of frames.
inline constexpr std::chrono::milliseconds MAX_LAG{250};

// Rate used when a caller asks for zero frames per second, which has no
// meaningful interval and would otherwise divide by zero.
inline constexpr uint32_t DEFAULT_TARGET_FPS = 60;

class Timer {
    public:
        using Clock = std::chrono::steady_clock;

        explicit Timer(uint32_t targetFPS = DEFAULT_TARGET_FPS) noexcept {setTargetFPS(targetFPS);}

        // Seconds since the previous tick, and marks this instant as the last frame.
        float tick() noexcept {
            const Clock::time_point now = Clock::now();
            const float dt = std::chrono::duration<float>(now - _lastFrame).count();
            _lastFrame = now;
            return dt;
        }

        void resync() noexcept {_nextFrame = Clock::now();}

        // Sleeps until the next scheduled frame, resyncing when too far behind.
        void sleepUntilNextFrame() noexcept {
            _nextFrame += _targetDt;
            const Clock::time_point now = Clock::now();
            if (now - _nextFrame > MAX_LAG) {_nextFrame = now;}
            else {std::this_thread::sleep_until(_nextFrame);}
        }

        // Sets the pacing rate, from which the per-frame interval is derived.
        // Zero has no interval, so it falls back to DEFAULT_TARGET_FPS.
        void setTargetFPS(uint32_t targetFPS) noexcept {
            if (targetFPS == 0) {
                log_warn("Timer::setTargetFPS given 0; using ", DEFAULT_TARGET_FPS, " instead");
                targetFPS = DEFAULT_TARGET_FPS;
            }
            _targetFPS = targetFPS;
            _targetDt = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::duration<double>(1.0 / static_cast<double>(targetFPS)));
        }

        // The rate the pacing interval was derived from.
        uint32_t targetFPS() const noexcept {return _targetFPS;}
        // The interval one frame is paced to.
        std::chrono::nanoseconds targetInterval() const noexcept {return _targetDt;}

        // The instant the last tick() recorded. Prefer this name; now() is the
        // older spelling of the same thing and does not read the clock.
        Clock::time_point lastFrameTime() const noexcept {return _lastFrame;}
        // The instant the last tick() recorded, not the current time.
        Clock::time_point now() const noexcept {return _lastFrame;}
        // The instant sleepUntilNextFrame() is currently pacing towards.
        Clock::time_point nextFrame() const noexcept {return _nextFrame;}

        Timer(const Timer&) = delete;
        Timer& operator=(const Timer&) = delete;
    private:
        uint32_t _targetFPS = DEFAULT_TARGET_FPS;
        std::chrono::nanoseconds _targetDt{};
        Clock::time_point _lastFrame = Clock::now();
        Clock::time_point _nextFrame = Clock::now();
};