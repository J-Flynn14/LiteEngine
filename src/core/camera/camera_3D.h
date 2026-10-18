#pragma once
#include "camera_core.h"

struct Ray {
    glm::vec3 origin;
    glm::vec3 direction;
};

struct Frustum {
    std::array<glm::vec4, 6> planes;

    // True when a point lies inside every frustum plane.
    bool containsPoint(const glm::vec3& point) const noexcept {
        for (const auto& plane : planes) {if (glm::dot(glm::vec3(plane), point) + plane.w < 0.0f) {return false;}}
        return true;
    }

    // True when a sphere touches the frustum; prefer this for sized objects.
    bool containsSphere(const glm::vec3& centre, float radius) const noexcept {
        for (const auto& plane : planes) {
            if (glm::dot(glm::vec3(plane), centre) + plane.w < -radius) return false;
        }
        return true;
    }
};

// FlyThrough: free spectator. OrbitTurntable: orbits a target, world up pinned.
// OrbitTrackball: orbits inside setReferenceOrientation's frame, subject held still.
enum class Camera3DMode {
    FlyThrough,
    OrbitTurntable,
    OrbitTrackball,
};

struct Camera3DBinds {
    int forward = GLFW_KEY_W,    back     = GLFW_KEY_S;
    int left    = GLFW_KEY_A,    right    = GLFW_KEY_D;
    int pitchUp = GLFW_KEY_UP,   pitchDown= GLFW_KEY_DOWN;
    int yawLeft = GLFW_KEY_LEFT, yawRight = GLFW_KEY_RIGHT;
    int rollLeft= GLFW_KEY_Q,    rollRight= GLFW_KEY_E;
    int zoomIn  = GLFW_KEY_EQUAL,zoomOut  = GLFW_KEY_MINUS;
};

struct Camera3DConfig {
    float fov = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;
    float moveSpeed = 20.0f;
    float orbitSpeed = 90.f;
    float pitchRate = 55.0f;
    float rollRate = 110.0f;
    float yawRate = 35.0f;
    float lookSensitivity = 0.1f;
    float orbitFocusDistance = 10.0f;
    bool clampZoom = true;
    float zoomSpeed = 0.2f;
    float zoomScale = 0.1f;
    float zoomMin = 0.2f;
    float zoomMax = 6.0f;
};

class Camera3D final : public Camera {
    public:
        // Creates a 3D camera bound to a window.
        static std::unique_ptr<Camera3D> create(Window& window, const Camera3DConfig& config = Camera3DConfig{}) noexcept {
            return std::unique_ptr<Camera3D>(new Camera3D(window, config));
        }
        ~Camera3D() noexcept override = default;

        // The active camera mode.
        Camera3DMode getMode() const noexcept {return _mode;}

        // True in either orbit mode.
        bool isOrbiting() const noexcept {
            return _mode == Camera3DMode::OrbitTurntable || _mode == Camera3DMode::OrbitTrackball;
        }

        // Switches mode, rebuilding the target mode's state from the current
        // position and view direction so the transition is continuous.
        void setMode(Camera3DMode mode) noexcept {
            if (mode == _mode) return;

            const glm::vec3 position = getPosition();
            const glm::vec3 front = getFront();

            switch (mode) {
                case Camera3DMode::FlyThrough:
                    _position = position;
                    anglesFromDirection(front, _yaw, _pitch);
                    _pitch = glm::clamp(_pitch, -89.0f, 89.0f);
                    _up = glm::vec3(0.0f, 1.0f, 0.0f);
                    break;

                case Camera3DMode::OrbitTurntable:
                    _target = position + front * orbitRadius();
                    anglesFromDirection(front, _yaw, _pitch);
                    break;

                case Camera3DMode::OrbitTrackball: {
                    _target = position + front * orbitRadius();
                    const glm::vec3 localFront = _hasReference
                        ? glm::inverse(_referenceOrientation) * front : front;
                    anglesFromDirection(localFront, _yaw, _pitch);
                    _pitch = glm::clamp(_pitch, -89.0f, 89.0f);
                    break;
                }
            }
            _mode = mode;
        }

        // Cycles turntable -> trackball -> fly-through -> turntable.
        void toggleMode() noexcept {
            switch (_mode) {
                case Camera3DMode::OrbitTurntable: setMode(Camera3DMode::OrbitTrackball); break;
                case Camera3DMode::OrbitTrackball: setMode(Camera3DMode::FlyThrough); break;
                case Camera3DMode::FlyThrough:     setMode(Camera3DMode::OrbitTurntable); break;
            }
        }

        // Reads the bound keys and drives the active mode: move and look in
        // fly-through, orbit and roll otherwise. zoomDelta is an external wheel offset.
        void handleMovement(float dt, const Camera3DBinds& binds = {},
                    glm::vec3 orbitPosition = {0.0f, 0.0f, 0.0f},
                    float zoomDelta = 0.0f) noexcept {
            const Input& in = _window.input();
            auto axis = [&](int pos, int neg) {
                return (in.isKeyDown(pos) ? 1.0f : 0.0f) - (in.isKeyDown(neg) ? 1.0f : 0.0f);
            };

            if (_mode == Camera3DMode::FlyThrough) {
                const glm::vec3 front = getFront();
                const glm::vec3 refUp = (std::abs(front.y) > 0.99f) ? glm::vec3(1,0,0) : glm::vec3(0,1,0);
                const glm::vec3 right = glm::normalize(glm::cross(front, refUp));

                const glm::vec3 move = front * axis(binds.forward, binds.back)
                                    + right * axis(binds.right, binds.left);
                if (glm::length(move) > 1e-6f) {
                    _position += glm::normalize(move) * _config.moveSpeed * dt;
                }

                addYawPitch(axis(binds.yawRight, binds.yawLeft)   * _config.yawRate   * dt,
                            axis(binds.pitchUp,  binds.pitchDown) * _config.pitchRate * dt);
            } else {
                setOrbitTarget(orbitPosition);

                const float roll = axis(binds.rollRight, binds.rollLeft);
                if (_mode == Camera3DMode::OrbitTrackball && roll != 0.0f) {
                    const glm::quat base = _hasReference ? _referenceOrientation
                                                        : glm::identity<glm::quat>();
                    setReferenceOrientation(base * glm::angleAxis(
                        glm::radians(roll * _config.rollRate * dt), glm::vec3(0,0,1)));
                }

                // Orbit yaw accepts both the strafe pair and the dedicated yaw pair,
                // so one binding set drives the same axis in either camera mode.
                const float orbitYaw = glm::clamp(axis(binds.right, binds.left)
                                                + axis(binds.yawRight, binds.yawLeft), -1.0f, 1.0f);
                orbit(orbitYaw * _config.yawRate * dt,
                      axis(binds.pitchUp, binds.pitchDown) * _config.pitchRate * dt);
            }

            float t = tFromZoom(getZoom());
            t += axis(binds.zoomIn, binds.zoomOut) * _config.zoomSpeed * dt;
            if (zoomDelta != 0.0f) {t += zoomDelta * _config.zoomScale;}
            setZoom(zoomFromT(t));
        }

        // Sets the fly-through position; orbit modes derive position from the target.
        void setPosition(const glm::vec3& position) noexcept {_position = position;}

        // Frame the trackball orbits within; pass the subject's orientation to hold
        // it fixed on screen while the world rotates around it. Other modes ignore this.
        void setReferenceOrientation(const glm::quat& orientation) noexcept {
            _referenceOrientation = glm::normalize(orientation);
            _hasReference = true;
        }

        // Drops the trackball reference frame, reverting to world-aligned orbiting.
        void clearReferenceOrientation() noexcept {_hasReference = false;}

        // True when a trackball reference orientation is set.
        bool hasReferenceOrientation() const noexcept {return _hasReference;}

        // The current trackball reference orientation.
        glm::quat referenceOrientation() const noexcept {return _referenceOrientation;}

        // Projects a world point to window coordinates; false when behind the camera.
        bool worldToScreen(const glm::vec3& worldPos, glm::vec2& screenPos) const noexcept {
            const glm::mat4 vp = projectionMatrix() * viewMatrix();
            const glm::vec4 clip = vp * glm::vec4(worldPos, 1.0f);
            if (clip.w <= 0.0001f) return false;

            const glm::vec3 ndc = glm::vec3(clip) / clip.w;
            const float winW = static_cast<float>(_window.w_width());
            const float winH = static_cast<float>(_window.w_height());

            screenPos.x = (ndc.x * 0.5f + 0.5f) * winW;
            screenPos.y = (1.0f - (ndc.y * 0.5f + 0.5f)) * winH;
            return true;
        }

        // Builds a world-space picking ray from a window-coordinate point.
        Ray screenToWorldRay(const glm::vec2& screenPos) const noexcept {
            const float winW = static_cast<float>(_window.w_width());
            const float winH = static_cast<float>(_window.w_height());
            const glm::vec4 viewport(0.0f, 0.0f, winW, winH);

            const glm::vec3 winNear(screenPos.x, winH - screenPos.y, 0.0f);
            const glm::vec3 winFar(screenPos.x, winH - screenPos.y, 1.0f);

            const glm::mat4 view = viewMatrix();
            const glm::mat4 proj = projectionMatrix();

            const glm::vec3 nearPoint = glm::unProject(winNear, view, proj, viewport);
            const glm::vec3 farPoint = glm::unProject(winFar, view, proj, viewport);

            return { nearPoint, glm::normalize(farPoint - nearPoint) };
        }

        // The six normalised frustum planes of the current view-projection.
        Frustum getFrustum() const noexcept {
            Frustum frustum;
            const glm::mat4 m = glm::transpose(projectionMatrix() * viewMatrix());

            frustum.planes[0] = m[3] + m[0];
            frustum.planes[1] = m[3] - m[0];
            frustum.planes[2] = m[3] + m[1];
            frustum.planes[3] = m[3] - m[1];
            frustum.planes[4] = m[3] + m[2];
            frustum.planes[5] = m[3] - m[2];

            for (auto& plane : frustum.planes) {
                const float len = glm::length(glm::vec3(plane));
                if (len > 0.0f) plane /= len;
            }
            return frustum;
        }

        // Adds to yaw and pitch, applying each mode's own clamping rules.
        void addYawPitch(float deltaYaw, float deltaPitch) noexcept {
            switch (_mode) {
                case Camera3DMode::FlyThrough:
                    _yaw += deltaYaw;
                    _pitch = glm::clamp(_pitch + deltaPitch, -89.0f, 89.0f);
                    _up = glm::vec3(0.0f, 1.0f, 0.0f);
                    break;

                case Camera3DMode::OrbitTurntable:
                    _yaw = wrapDegrees(_yaw + deltaYaw);
                    _pitch = wrapDegrees(_pitch + deltaPitch);
                    break;

                // Trackball angles live in the reference frame so no roll accumulates;
                // pitch stops short of the pole, where up would be undefined.
                case Camera3DMode::OrbitTrackball:
                    _yaw = wrapDegrees(_yaw + deltaYaw);
                    _pitch = glm::clamp(_pitch + deltaPitch, -89.0f, 89.0f);
                    break;
            }
        }

        // Sets yaw and pitch outright, applying each mode's own clamping rules.
        void setYawPitch(float yaw, float pitch) noexcept {
            switch (_mode) {
                case Camera3DMode::FlyThrough:
                    _yaw = yaw;
                    _pitch = glm::clamp(pitch, -89.0f, 89.0f);
                    _up = glm::vec3(0.0f, 1.0f, 0.0f);
                    break;

                case Camera3DMode::OrbitTurntable:
                    _yaw = wrapDegrees(yaw);
                    _pitch = wrapDegrees(pitch);
                    break;

                case Camera3DMode::OrbitTrackball:
                    _yaw = wrapDegrees(yaw);
                    _pitch = glm::clamp(pitch, -89.0f, 89.0f);
                    break;
            }
        }

        // Orbits the camera when in an orbit mode; ignored in fly-through.
        void orbit(float deltaYawDeg, float deltaPitchDeg) noexcept {
            if (isOrbiting()) addYawPitch(deltaYawDeg, deltaPitchDeg);
        }

        // Sets the world point the orbit modes revolve around.
        void setOrbitTarget(const glm::vec3& target) noexcept {_target = target;}

        // The world point the orbit modes revolve around.
        glm::vec3 getOrbitTarget() const noexcept {return _target;}

        // The camera's world position for the active mode.
        glm::vec3 getPosition() const noexcept {
            switch (_mode) {
                case Camera3DMode::OrbitTurntable:
                    return _target - sphericalDirection(_yaw, _pitch) * orbitRadius();
                case Camera3DMode::OrbitTrackball:
                    return _target + (trackballRotation() * glm::vec3(0.0f, 0.0f, 1.0f)) * orbitRadius();
                default:
                    return _position;
            }
        }

        // The camera's forward direction for the active mode.
        glm::vec3 getFront() const noexcept {
            if (_mode == Camera3DMode::OrbitTrackball) {
                return trackballRotation() * glm::vec3(0.0f, 0.0f, -1.0f);
            }
            return sphericalDirection(_yaw, _pitch);
        }

        // The view matrix for the current position, direction and up vector.
        glm::mat4 viewMatrix() const noexcept {
            const glm::vec3 pos = getPosition();
            return glm::lookAt(pos, pos + getFront(), currentUp());
        }

        // Perspective projection; fly-through maps zoom onto field of view.
        glm::mat4 projectionMatrix() const noexcept override {
            if (_mode != Camera3DMode::FlyThrough) {
                return glm::perspective(glm::radians(_config.fov), aspectRatio(), _config.nearPlane, _config.farPlane);
            }
            const float baseFov = glm::clamp(_config.fov, 1.0f, 179.0f);
            const float fovAtZoomMin = glm::clamp(_config.fov / glm::max(_zoomMin, 0.0001f), 1.0f, 179.0f);
            const float fovAtZoomMax = glm::clamp(_config.fov / glm::max(_zoomMax, 0.0001f), 1.0f, 179.0f);
            const float effectiveFov = linearZoomLerp(fovAtZoomMin, baseFov, fovAtZoomMax);
            return glm::perspective(glm::radians(effectiveFov), aspectRatio(), _config.nearPlane, _config.farPlane);
        }

        Camera3D(const Camera3D&) = delete;
        Camera3D& operator=(const Camera3D&) = delete;

    private:
        explicit Camera3D(Window& window, const Camera3DConfig& config) noexcept
            : Camera(window, config.clampZoom, config.zoomMin, config.zoomMax),
              _config(config),
              _baseOrbitRadius(config.orbitFocusDistance) {}

        // Wraps an angle into [0, 360).
        static float wrapDegrees(float degrees) noexcept {
            degrees = std::fmod(degrees, 360.0f);
            if (degrees < 0.0f) degrees += 360.0f;
            return degrees;
        }

        // Unit direction for a yaw/pitch pair, in degrees.
        static glm::vec3 sphericalDirection(float yawDeg, float pitchDeg) noexcept {
            return glm::vec3(
                std::cos(glm::radians(yawDeg)) * std::cos(glm::radians(pitchDeg)),
                std::sin(glm::radians(pitchDeg)),
                std::cos(glm::radians(pitchDeg)) * std::sin(glm::radians(yawDeg))
            );
        }

        // Yaw and pitch, in degrees, for a direction vector.
        static void anglesFromDirection(const glm::vec3& dir, float& yawDeg, float& pitchDeg) noexcept {
            const glm::vec3 d = glm::normalize(dir);
            pitchDeg = glm::degrees(std::asin(glm::clamp(d.y, -1.0f, 1.0f)));
            yawDeg = glm::degrees(std::atan2(d.z, d.x));
        }

        // Orientation looking along front with up as the roll reference.
        // The camera looks down its own -Z, hence the negated third column.
        static glm::quat rotationFromBasis(const glm::vec3& front, const glm::vec3& up) noexcept {
            const glm::vec3 f = glm::normalize(front);
            glm::vec3 reference = up;
            glm::vec3 right = glm::cross(f, reference);
            if (glm::length(right) < 1e-4f) {
                reference = (std::abs(f.y) > 0.99f) ? glm::vec3(1.0f, 0.0f, 0.0f)
                                                    : glm::vec3(0.0f, 1.0f, 0.0f);
                right = glm::cross(f, reference);
            }
            right = glm::normalize(right);
            const glm::vec3 orthoUp = glm::cross(right, f);
            return glm::normalize(glm::quat_cast(glm::mat3(right, orthoUp, -f)));
        }

        // The up vector for the active mode.
        glm::vec3 currentUp() const noexcept {
            switch (_mode) {
                case Camera3DMode::OrbitTurntable: return computeTurntableUp();
                case Camera3DMode::OrbitTrackball: return trackballRotation() * glm::vec3(0.0f, 1.0f, 0.0f);
                default:                           return _up;
            }
        }

        // Orbit angles resolved into world space through the reference frame,
        // whose own up is what locks camera tilt to the subject.
        glm::quat trackballRotation() const noexcept {
            const glm::quat local = rotationFromBasis(sphericalDirection(_yaw, _pitch),
                                                      glm::vec3(0.0f, 1.0f, 0.0f));
            if (!_hasReference) return local;
            return glm::normalize(_referenceOrientation * local);
        }

        // Up vector that keeps a turntable orbit free of roll.
        glm::vec3 computeTurntableUp() const noexcept {
            const float pRad = glm::radians(_pitch);
            const float yRad = glm::radians(_yaw);
            return glm::vec3(
                -std::sin(pRad) * std::cos(yRad),
                 std::cos(pRad),
                -std::sin(pRad) * std::sin(yRad)
            );
        }

        // Interpolates a value across the zoom range, with 1.0 as the midpoint.
        float linearZoomLerp(float atZoomMin, float atZoomOne, float atZoomMax) const noexcept {
            const float zoom = glm::max(_zoom, 0.0001f);
            if (zoom <= 1.0f) {
                const float t = glm::clamp((zoom - _zoomMin) / glm::max(1.0f - _zoomMin, 0.0001f), 0.0f, 1.0f);
                return glm::mix(atZoomMin, atZoomOne, t);
            }
            const float t = glm::clamp((zoom - 1.0f) / glm::max(_zoomMax - 1.0f, 0.0001f), 0.0f, 1.0f);
            return glm::mix(atZoomOne, atZoomMax, t);
        }

        // Distance from the orbit target at the current zoom.
        float orbitRadius() const noexcept {
            return linearZoomLerp(
                _baseOrbitRadius / glm::max(_zoomMin, 0.0001f),
                _baseOrbitRadius,
                _baseOrbitRadius / glm::max(_zoomMax, 0.0001f)
            );
        }

        Camera3DConfig _config;
        Camera3DMode _mode = Camera3DMode::FlyThrough;

        glm::vec3 _position{0.0f, 0.0f, 5.0f};
        glm::vec3 _up{0.0f, 1.0f, 0.0f};
        glm::vec3 _target{0.0f, 0.0f, 0.0f};

        // Frame the trackball's yaw/pitch are measured in.
        glm::quat _referenceOrientation{1.0f, 0.0f, 0.0f, 0.0f};
        bool _hasReference = false;

        float _baseOrbitRadius;
        float _yaw = -90.0f;
        float _pitch = 0.0f;
};