#pragma once
#include <array>
#include <cmath>
#include <memory>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "../window.h"

// Screen space here is WINDOW coordinates -- the units glfwGetCursorPos reports --
// so picking and projection agree on HiDPI displays with a larger framebuffer.
class Camera {
    public:
        virtual ~Camera() noexcept = default;
 
        // Sets the zoom factor, clamped when the config asked for clamping.
        void setZoom(float zoom) noexcept {
            _zoom = _clampZoom ? glm::clamp(zoom, _zoomMin, _zoomMax) : zoom;
        }
 
        // The current zoom factor.
        float getZoom() const noexcept {return _zoom;}
 
        // Maps a 0..1 slider position onto the zoom range, with 1.0 at the midpoint.
        // Logarithmic, so equal steps in t scale the view by equal factors; a linear
        // map makes each step a huge relative jump just above 1 and a tiny one near max.
        float zoomFromT(float t) const noexcept {
            t = glm::clamp(t, 0.0f, 1.0f);
            const float logMin = std::log(glm::max(_zoomMin, 1e-4f));
            const float logMax = std::log(glm::max(_zoomMax, 1e-4f));
            return std::exp(t <= 0.5f ? glm::mix(logMin, 0.0f, t * 2.0f)
                                      : glm::mix(0.0f, logMax, (t - 0.5f) * 2.0f));
        }
 
        // Inverse of the above.
        float tFromZoom(float z) const noexcept {
            z = glm::clamp(z, _zoomMin, _zoomMax);
            const float logMin = std::log(glm::max(_zoomMin, 1e-4f));
            const float logMax = std::log(glm::max(_zoomMax, 1e-4f));
            const float logZ = std::log(glm::max(z, 1e-4f));
            return logZ <= 0.0f
                ? 0.5f * (logZ - logMin) / glm::max(-logMin, 1e-4f)
                : 0.5f + 0.5f * logZ / glm::max(logMax, 1e-4f);
        }
 
        // The projection matrix for this camera's current state.
        virtual glm::mat4 projectionMatrix() const noexcept = 0;
 
        Camera(const Camera&) = delete;
        Camera& operator=(const Camera&) = delete;
    protected:
        explicit Camera(Window& window, bool clampZoom, float zoomMin, float zoomMax) noexcept
            : _window(window), _clampZoom(clampZoom), _zoomMin(zoomMin), _zoomMax(zoomMax) {}
 
        // Window aspect ratio, guarded against a zero-height window.
        float aspectRatio() const noexcept {
            const int height = _window.w_height();
            if (height <= 0) return 1.0f;
            return static_cast<float>(_window.w_width()) / static_cast<float>(height);
        }
        Window& _window;
        float _zoom = 1.0f;
        bool _clampZoom;
        float _zoomMin;
        float _zoomMax;
};