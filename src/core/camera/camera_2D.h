#pragma once
#include "camera_core.h"

struct Camera2DConfig {
    float panSpeed = 500.0f;
    float smoothness = 0.1f;
    float snapDistance = 0.001f;
    bool clampZoom = true;
    float zoomSpeed = 0.2f;
    float zoomScale = 0.1f;
    float zoomMin = 0.1f;
    float zoomMax = 100.0f;
};

class Camera2D final : public Camera {
    public:
        // Creates a 2D orthographic camera bound to a window.
        static std::unique_ptr<Camera2D> create(Window& window, const Camera2DConfig& config = Camera2DConfig{}) noexcept {
            return std::unique_ptr<Camera2D>(new Camera2D(window, config));
        }
        ~Camera2D() noexcept override = default;

        // Moves the camera to a world position immediately.
        void setPosition(const glm::dvec2& position) noexcept {_position = position; _target = position;}

        // The camera's current world position.
        glm::dvec2 getPosition() const noexcept {return _position;}

        // Sets a target the camera eases toward on update().
        void moveTo(const glm::dvec2& target) noexcept {_target = target;}

        // Pans the target by a world-space delta.
        void pan(const glm::dvec2& delta) noexcept {_target += delta;}

        // Reads the bound keys, pans and zooms accordingly, then eases the position.
        // zoomDelta is an external wheel offset, scaled by the config's zoomScale.
        void handleMovement(float dt, int upKey=GLFW_KEY_W, int downKey=GLFW_KEY_S, int leftKey=GLFW_KEY_A, int rightKey=GLFW_KEY_D,
                    int zoomInKey=GLFW_KEY_EQUAL, int zoomOutKey=GLFW_KEY_MINUS, float zoomDelta = 0.0f) noexcept {

            const Input& in = _window.input();
            const bool up = in.isKeyDown(upKey);
            const bool down = in.isKeyDown(downKey);
            const bool left = in.isKeyDown(leftKey);
            const bool right = in.isKeyDown(rightKey);
            const bool zoomIn = in.isKeyDown(zoomInKey);
            const bool zoomOut = in.isKeyDown(zoomOutKey);

            glm::dvec2 dir{0.0, 0.0};
            if (up)    {dir.y += 1.0;}
            if (down)  {dir.y -= 1.0;}
            if (left)  {dir.x -= 1.0;}
            if (right) {dir.x += 1.0;}

            if (dir.x != 0.0 || dir.y != 0.0) {
                dir = glm::normalize(dir);
                const double speed = static_cast<double>(_config.panSpeed * dt)
                                / static_cast<double>(glm::max(_zoom, 0.0001f));
                pan(dir * speed);
            }

            float t = tFromZoom(getZoom());
            if (zoomIn)            {t += _config.zoomSpeed * dt;}
            if (zoomOut)           {t -= _config.zoomSpeed * dt;}
            if (zoomDelta != 0.0f) {t += zoomDelta * _config.zoomScale;}
            setZoom(zoomFromT(t));

            update(dt);
        }

        // Eases the position toward the target; snaps once inside snapDistance.
        void update(float deltaSeconds) noexcept {
            const glm::dvec2 offset = _target - _position;
            const double distance = glm::length(offset);
            if (distance <= static_cast<double>(_config.snapDistance)) {_position = _target; return;}

            const double smoothness = glm::clamp(static_cast<double>(_config.smoothness), 0.0, 1.0);
            if (smoothness <= 0.0) {_position = _target; return;}

            const double blend = 1.0 - std::pow(1.0 - smoothness, static_cast<double>(deltaSeconds) * 60.0);
            _position += offset * glm::clamp(blend, 0.0, 1.0);
        }

        // Pan speed in world units per second, from the config.
        float panSpeed() const noexcept {return _config.panSpeed;}

        // The world position the view is currently centred on.
        glm::dvec2 renderOrigin() const noexcept {return _position;}

        // Sub-texel remainder of the render origin, for stable pixel-art snapping.
        glm::vec2 subTexelOffset() const noexcept {
            const glm::dvec2 origin = renderOrigin();
            return glm::vec2(origin - glm::round(origin));
        }

        // Visible world rectangle as {minX, minY, maxX, maxY}, rounded outward. Padded
        // by half a unit because projectionMatrix() centres on the rounded origin.
        std::array<int, 4> getCulledBounds() const noexcept {
            const glm::dvec2 origin = renderOrigin();
            const float halfW = halfWidth() + 0.5f;
            const float halfH = halfHeight() + 0.5f;

            return {
                static_cast<int>(std::floor(origin.x - halfW)),
                static_cast<int>(std::floor(origin.y - halfH)),
                static_cast<int>(std::ceil(origin.x + halfW)),
                static_cast<int>(std::ceil(origin.y + halfH))
            };
        }

        // Is this world point visible? Conservative: never false for a visible point.
        bool inView(glm::dvec2 worldCoord) const noexcept {
            const std::array<int, 4> bounds = getCulledBounds();
            return worldCoord.x >= static_cast<double>(bounds[0])
                && worldCoord.x <= static_cast<double>(bounds[2])
                && worldCoord.y >= static_cast<double>(bounds[1])
                && worldCoord.y <= static_cast<double>(bounds[3]);
        }

        // As above, grown by radius. Prefer for sized objects; a point test pops at edges.
        bool inView(glm::dvec2 worldCoord, double radius) const noexcept {
            const std::array<int, 4> bounds = getCulledBounds();
            return worldCoord.x + radius >= static_cast<double>(bounds[0])
                && worldCoord.x - radius <= static_cast<double>(bounds[2])
                && worldCoord.y + radius >= static_cast<double>(bounds[1])
                && worldCoord.y - radius <= static_cast<double>(bounds[3]);
        }

        // World to window coordinates, origin top-left with Y down.
        // World space is Y up, hence the inverted Y term.
        glm::dvec2 worldToScreen(glm::dvec2 worldCoord) const noexcept {
            const double halfW = static_cast<double>(halfWidth());
            const double halfH = static_cast<double>(halfHeight());
            if (halfW <= 0.0 || halfH <= 0.0) return {0.0, 0.0};

            const double windowW = static_cast<double>(_window.w_width());
            const double windowH = static_cast<double>(_window.w_height());
            return {
                (worldCoord.x - _position.x) / halfW * (windowW * 0.5) + windowW * 0.5,
                (_position.y - worldCoord.y) / halfH * (windowH * 0.5) + windowH * 0.5
            };
        }

        // Window coordinates (as reported by the cursor) back to world space.
        glm::dvec2 screenToWorld(glm::dvec2 screenCoord) const noexcept {
            const double windowW = static_cast<double>(_window.w_width());
            const double windowH = static_cast<double>(_window.w_height());
            if (windowW <= 0.0 || windowH <= 0.0) return _position;

            const double ndcX = (screenCoord.x / windowW) * 2.0 - 1.0;
            const double ndcY = (screenCoord.y / windowH) * 2.0 - 1.0;

            return {_position.x + ndcX * static_cast<double>(halfWidth()),
                    _position.y - ndcY * static_cast<double>(halfHeight())};
        }

        // Orthographic projection centred on the pixel-snapped render origin.
        glm::mat4 projectionMatrix() const noexcept override {
            const glm::dvec2 snapped = glm::round(renderOrigin());
            const float halfW = halfWidth();
            const float halfH = halfHeight();

            return glm::ortho(
                static_cast<float>(snapped.x) - halfW, static_cast<float>(snapped.x) + halfW,
                static_cast<float>(snapped.y) - halfH, static_cast<float>(snapped.y) + halfH,
                -1.0f, 1.0f
            );
        }

        Camera2D(const Camera2D&) = delete;
        Camera2D& operator=(const Camera2D&) = delete;
    private:
        explicit Camera2D(Window& window, const Camera2DConfig& config) noexcept
            : Camera(window, config.clampZoom, config.zoomMin, config.zoomMax), _config(config) {}

        // Half the visible world width at the current zoom.
        float halfWidth() const noexcept {
            return (static_cast<float>(_window.w_width()) * 0.5f) / glm::max(_zoom, 0.0001f);
        }

        // Half the visible world height at the current zoom.
        float halfHeight() const noexcept {
            return (static_cast<float>(_window.w_height()) * 0.5f) / glm::max(_zoom, 0.0001f);
        }

        Camera2DConfig _config;
        glm::dvec2 _position{0.0, 0.0};
        glm::dvec2 _target{0.0, 0.0};
};