#pragma once
#include "light_core.h"

#include <array>

#include <glm/gtc/matrix_transform.hpp>

#include "../../core/framebuffer.h"
#include "../../core/texture.h"

// Configuration for a Light3D. Angles are half-angles in degrees; areaSize is width
// and height, or a radius in x when areaIsDisc is set.
struct Light3DConfig : LightConfig {
    LightType type = LightType::Point;
    glm::vec3 position{0.0f};
    glm::vec3 direction{0.0f, -1.0f, 0.0f};
    float range = 20.0f;
    float innerAngle = 25.0f;
    float outerAngle = 40.0f;
    glm::vec2 areaSize{1.0f, 1.0f};
    bool areaIsDisc = false;
};

// A light for the perspective 3D path; one class covers all four types.
class Light3D final : public Light {
    public:
        // Creates a 3D light from a full config.
        static std::unique_ptr<Light3D> create(const Light3DConfig& config = {}) noexcept {
            return std::unique_ptr<Light3D>(new Light3D(config));
        }

        // Creates a directional light.
        static std::unique_ptr<Light3D> createDirectional(const glm::vec3& direction,
                                                          const glm::vec3& color = glm::vec3(1.0f),
                                                          float intensity = 1.0f) noexcept {
            Light3DConfig config;
            config.type = LightType::Directional;
            config.direction = direction;
            config.color = color;
            config.intensity = intensity;
            return create(config);
        }

        // Creates an omnidirectional light.
        static std::unique_ptr<Light3D> createPoint(const glm::vec3& position, float range,
                                                    const glm::vec3& color = glm::vec3(1.0f),
                                                    float intensity = 1.0f) noexcept {
            Light3DConfig config;
            config.type = LightType::Point;
            config.position = position;
            config.range = range;
            config.color = color;
            config.intensity = intensity;
            return create(config);
        }

        // Creates a cone light, inner angle defaulted from the outer.
        static std::unique_ptr<Light3D> createSpot(const glm::vec3& position, const glm::vec3& direction,
                                                   float range, float outerAngle,
                                                   const glm::vec3& color = glm::vec3(1.0f),
                                                   float intensity = 1.0f) noexcept {
            Light3DConfig config;
            config.type = LightType::Spot;
            config.position = position;
            config.direction = direction;
            config.range = range;
            config.color = color;
            config.intensity = intensity;
            auto light = create(config);
            light->setAngles(outerAngle * 0.6f, outerAngle);
            return light;
        }

        // Creates a rectangular area light.
        static std::unique_ptr<Light3D> createArea(const glm::vec3& position, const glm::vec3& direction,
                                                   const glm::vec2& size, float range,
                                                   const glm::vec3& color = glm::vec3(1.0f),
                                                   float intensity = 1.0f) noexcept {
            Light3DConfig config;
            config.type = LightType::Area;
            config.position = position;
            config.direction = direction;
            config.areaSize = size;
            config.range = range;
            config.color = color;
            config.intensity = intensity;
            return create(config);
        }

        // This light's geometric type.
        LightType type() const noexcept override {return _config.type;}
        // Changes the type in place; pipelines read it during add().
        void setType(LightType type) noexcept {_config.type = type;}
        // 3D lights belong to a LightingPipeline3D.
        int dimensions() const noexcept override {return 3;}
        // The shared configuration this light stores.
        LightConfig& config() noexcept override {return _config;}
        // The shared configuration this light stores.
        const LightConfig& config() const noexcept override {return _config;}

        // The light's world position.
        glm::vec3 position() const noexcept {return _config.position;}
        // Sets the light's world position.
        void setPosition(const glm::vec3& position) noexcept {_config.position = position;}

        // The normalised aim direction, falling back to -Y when unset.
        glm::vec3 direction() const noexcept {
            const float length = glm::length(_config.direction);
            return (length > 1e-6f) ? _config.direction / length : glm::vec3(0.0f, -1.0f, 0.0f);
        }
        // Sets the aim direction; normalisation happens on read.
        void setDirection(const glm::vec3& direction) noexcept {_config.direction = direction;}

        // The light's reach in world units.
        float range() const noexcept {return _config.range;}
        // Sets the light's reach, clamped to zero or above.
        void setRange(float range) noexcept {_config.range = std::max(range, 0.0f);}

        // Sets the spot half-angles in degrees, outer below 90 so a shadow map can cover it.
        void setAngles(float innerDegrees, float outerDegrees) noexcept {
            _config.outerAngle = glm::clamp(outerDegrees, 0.0f, 89.0f);
            _config.innerAngle = glm::clamp(innerDegrees, 0.0f, _config.outerAngle);
        }
        // The inner half-angle in degrees.
        float innerAngle() const noexcept {return _config.innerAngle;}
        // The outer half-angle in degrees.
        float outerAngle() const noexcept {return _config.outerAngle;}

        // Emitter size for area lights: width and height, or radius in x for discs.
        glm::vec2 areaSize() const noexcept {return _config.areaSize;}
        // Sets the area emitter size.
        void setAreaSize(const glm::vec2& size) noexcept {_config.areaSize = size;}
        // True when the area emitter is a disc rather than a rectangle.
        bool areaIsDisc() const noexcept {return _config.areaIsDisc;}
        // Chooses between a disc and a rectangular area emitter.
        void setAreaIsDisc(bool isDisc) noexcept {_config.areaIsDisc = isDisc;}

        // Sphere enclosing everything the light can reach; zero radius for directional.
        glm::vec4 boundingSphere() const noexcept {
            if (_config.type == LightType::Directional) return glm::vec4(_config.position, 0.0f);
            float emitterRadius = 0.0f;
            if (_config.type == LightType::Area) {
                emitterRadius = _config.areaIsDisc ? std::max(_config.areaSize.x, 0.0f)
                                                   : 0.5f * glm::length(_config.areaSize);
            }
            return glm::vec4(_config.position, _config.range + emitterRadius);
        }

        // Shadow-map view-projection for spot and directional lights, the directional
        // box centred on this light's position.
        glm::mat4 shadowViewProjection(float directionalExtent = 50.0f) const noexcept {
            return shadowViewProjection(directionalExtent, _config.position);
        }

        // Shadow-map view-projection with the directional box centred on `directionalFocus`.
        // A resolution above zero snaps that centre to whole shadow-map texels, so a box
        // that follows the camera does not make shadow edges shimmer.
        glm::mat4 shadowViewProjection(float directionalExtent, const glm::vec3& directionalFocus,
                                       int resolution = 0) const noexcept {
            const glm::vec3 forward = direction();
            const glm::vec3 reference = (std::abs(forward.y) > 0.99f) ? glm::vec3(1.0f, 0.0f, 0.0f)
                                                                     : glm::vec3(0.0f, 1.0f, 0.0f);
            const glm::vec3 right = glm::normalize(glm::cross(reference, forward));
            const glm::vec3 up = glm::cross(forward, right);

            if (_config.type == LightType::Directional) {
                glm::vec3 focus = directionalFocus;
                if (resolution > 0) {
                    const float texel = 2.0f * directionalExtent / static_cast<float>(resolution);
                    focus = right * (std::floor(glm::dot(focus, right) / texel) * texel)
                          + up * (std::floor(glm::dot(focus, up) / texel) * texel)
                          + forward * glm::dot(focus, forward);
                }
                const glm::mat4 view = glm::lookAt(focus - forward * directionalExtent, focus, up);
                return glm::ortho(-directionalExtent, directionalExtent,
                                  -directionalExtent, directionalExtent,
                                  _config.shadows.nearPlane, directionalExtent * 2.0f) * view;
            }
            const float fov = glm::radians(glm::clamp(_config.outerAngle * 2.0f, 1.0f, 179.0f));
            const glm::mat4 view = glm::lookAt(_config.position, _config.position + forward, up);
            return glm::perspective(fov, 1.0f, _config.shadows.nearPlane,
                                    std::max(_config.range, _config.shadows.nearPlane + 1.0f)) * view;
        }

    private:
        explicit Light3D(const Light3DConfig& config) noexcept : _config(config) {}

        Light3DConfig _config;
};

// Source of the 3D ambient term. Skybox is reserved and falls back to Gradient.
enum class AmbientMode {Flat, Gradient, Skybox};

// Sky/equator/ground gradient ambient evaluated per normal, or one flat colour.
class EnvironmentLight3D final : public EnvironmentLight {
    public:
        // Creates a 3D environment light with the default daylight gradient.
        static std::shared_ptr<EnvironmentLight3D> create() noexcept {
            return std::shared_ptr<EnvironmentLight3D>(new EnvironmentLight3D());
        }

        // Creates a gradient environment from three colours.
        static std::shared_ptr<EnvironmentLight3D> createGradient(const glm::vec3& sky,
                                                                  const glm::vec3& equator,
                                                                  const glm::vec3& ground,
                                                                  float intensity = 1.0f) noexcept {
            auto environment = create();
            environment->setGradient(sky, equator, ground);
            environment->setIntensity(intensity);
            return environment;
        }

        // Creates a single-colour environment.
        static std::shared_ptr<EnvironmentLight3D> createFlat(const glm::vec3& color,
                                                              float intensity = 1.0f) noexcept {
            auto environment = create();
            environment->setFlatColor(color);
            environment->setMode(AmbientMode::Flat);
            environment->setIntensity(intensity);
            return environment;
        }

        // This environment belongs to a LightingPipeline3D.
        int dimensions() const noexcept override {return 3;}
        // Variant cache key for the gradient ambient.
        std::string name() const noexcept override {return "EnvironmentLight3D";}

        // The three gradient colour uniforms.
        std::string glslUniforms() const noexcept override {
            return shader_tools::glsl(R"GLSL(
                uniform vec3 uAmbientSky;
                uniform vec3 uAmbientEquator;
                uniform vec3 uAmbientGround;
            )GLSL");
        }

        // The gradient form of ambientLight().
        std::string glslFunction() const noexcept override {
            return shader_tools::glsl(R"GLSL(

                vec3 ambientLight(vec3 n) {
                    float up = clamp(n.y, -1.0, 1.0);
                    return up >= 0.0 ? mix(uAmbientEquator, uAmbientSky, up)
                                     : mix(uAmbientEquator, uAmbientGround, -up);
                }
            )GLSL");
        }

        // Writes the gradient, or the flat colour into all three slots in Flat mode.
        void upload(const Shader& shader) const noexcept override {
            if (_mode == AmbientMode::Flat) {
                const glm::vec3 flat = _flatColor * _intensity;
                shader.setVec3("uAmbientSky", flat);
                shader.setVec3("uAmbientEquator", flat);
                shader.setVec3("uAmbientGround", flat);
                return;
            }
            shader.setVec3("uAmbientSky", _skyColor * _intensity);
            shader.setVec3("uAmbientEquator", _equatorColor * _intensity);
            shader.setVec3("uAmbientGround", _groundColor * _intensity);
        }

        // How the ambient term is sourced.
        AmbientMode mode() const noexcept {return _mode;}
        // Sets the ambient source; Skybox logs a warning and uses Gradient.
        void setMode(AmbientMode mode) noexcept {
            if (mode == AmbientMode::Skybox) {
                log_warn("EnvironmentLight3D: Skybox ambient is not implemented; using Gradient");
                mode = AmbientMode::Gradient;
            }
            _mode = mode;
        }

        // The upward-facing ambient colour.
        glm::vec3 skyColor() const noexcept {return _skyColor;}
        // Sets the upward-facing ambient colour.
        void setSkyColor(const glm::vec3& color) noexcept {_skyColor = color;}

        // The horizon ambient colour.
        glm::vec3 equatorColor() const noexcept {return _equatorColor;}
        // Sets the horizon ambient colour.
        void setEquatorColor(const glm::vec3& color) noexcept {_equatorColor = color;}

        // The downward-facing ambient colour.
        glm::vec3 groundColor() const noexcept {return _groundColor;}
        // Sets the downward-facing ambient colour.
        void setGroundColor(const glm::vec3& color) noexcept {_groundColor = color;}

        // Sets all three gradient colours and switches to Gradient mode.
        void setGradient(const glm::vec3& sky, const glm::vec3& equator,
                         const glm::vec3& ground) noexcept {
            _skyColor = sky;
            _equatorColor = equator;
            _groundColor = ground;
            _mode = AmbientMode::Gradient;
        }

        // The colour used in Flat mode.
        glm::vec3 flatColor() const noexcept {return _flatColor;}
        // Sets the colour used in Flat mode.
        void setFlatColor(const glm::vec3& color) noexcept {_flatColor = color;}

        // Multiplies every colour by a blackbody tint; cumulative across calls.
        void setFromKelvin(float kelvin) noexcept {
            const glm::vec3 tint = Light::colorFromKelvin(kelvin);
            _skyColor *= tint;
            _equatorColor *= tint;
            _groundColor *= tint;
            _flatColor *= tint;
        }

        // Ambient contribution for a surface normal, matching ambientLight() in GLSL.
        glm::vec3 evaluate(const glm::vec3& normal) const noexcept {
            if (_mode == AmbientMode::Flat) return _flatColor * _intensity;
            const float up = glm::clamp(normal.y, -1.0f, 1.0f);
            const glm::vec3 blended = (up >= 0.0f) ? glm::mix(_equatorColor, _skyColor, up)
                                                   : glm::mix(_equatorColor, _groundColor, -up);
            return blended * _intensity;
        }

    private:
        EnvironmentLight3D() noexcept = default;

        AmbientMode _mode = AmbientMode::Gradient;
        glm::vec3 _skyColor{0.45f, 0.62f, 0.78f};
        glm::vec3 _equatorColor{0.32f, 0.38f, 0.42f};
        glm::vec3 _groundColor{0.14f, 0.13f, 0.12f};
        glm::vec3 _flatColor{0.30f, 0.34f, 0.38f};
};

// Where specular reflections come from. Each source defines exactly
// `vec3 reflectionSample(vec3 worldPos, vec3 n, vec3 v, float roughness)` and may use ambientLight().
class ReflectionSource {
    public:
        virtual ~ReflectionSource() noexcept = default;

        // Stable identifier, part of the shader variant cache key.
        virtual std::string name() const noexcept = 0;
        // Uniform declarations reflectionSample() reads.
        virtual std::string glslUniforms() const noexcept = 0;
        // Must define exactly `vec3 reflectionSample(vec3, vec3, vec3, float)`.
        virtual std::string glslFunction() const noexcept = 0;
        // Pushes the current values into the declared uniforms.
        virtual void upload(const Shader& shader) const noexcept = 0;

        // Overall reflection multiplier.
        float intensity() const noexcept {return _intensity;}
        // Sets the reflection multiplier, clamped to zero or above.
        void setIntensity(float intensity) noexcept {_intensity = std::max(intensity, 0.0f);}

        ReflectionSource(const ReflectionSource&) = delete;
        ReflectionSource& operator=(const ReflectionSource&) = delete;

    protected:
        ReflectionSource() noexcept = default;
        float _intensity = 1.0f;
};

// Samples the environment gradient along the reflection vector. Free; the default.
class ReflectionEnvironment final : public ReflectionSource {
    public:
        // Creates the environment-gradient reflection source.
        static std::shared_ptr<ReflectionEnvironment> create() noexcept {
            return std::shared_ptr<ReflectionEnvironment>(new ReflectionEnvironment());
        }

        // Variant cache key for the environment fallback.
        std::string name() const noexcept override {return "ReflectionEnvironment";}

        // The intensity multiplier.
        std::string glslUniforms() const noexcept override {
            return shader_tools::glsl(R"GLSL(
                uniform float uReflectionIntensity;
            )GLSL");
        }

        // An ambientLight() lookup along a roughness-widened reflection vector.
        std::string glslFunction() const noexcept override {
            return shader_tools::glsl(R"GLSL(

                vec3 reflectionSample(vec3 worldPos, vec3 n, vec3 v, float roughness) {
                    vec3 r = reflect(-v, n);
                    vec3 direction = normalize(mix(r, n, roughness * roughness));
                    return ambientLight(direction) * uReflectionIntensity;
                }
            )GLSL");
        }

        // Writes the intensity multiplier.
        void upload(const Shader& shader) const noexcept override {
            shader.setFloat("uReflectionIntensity", _intensity);
        }

    private:
        ReflectionEnvironment() noexcept = default;
};

// Samples a mipmapped cubemap, roughness selecting the mip level.
class ReflectionCubemap final : public ReflectionSource {
    public:
        // Creates a cubemap reflection source; the texture must outlive it.
        static std::shared_ptr<ReflectionCubemap> create(Texture* cubemap, int textureUnit = 9) noexcept {
            auto source = std::shared_ptr<ReflectionCubemap>(new ReflectionCubemap());
            source->setCubemap(cubemap);
            source->setTextureUnit(textureUnit);
            return source;
        }

        // Variant cache key for the cubemap source.
        std::string name() const noexcept override {return "ReflectionCubemap";}

        // The cubemap sampler, intensity and mip ceiling.
        std::string glslUniforms() const noexcept override {
            return shader_tools::glsl(R"GLSL(
                uniform samplerCube uReflectionCube;
                uniform float uReflectionIntensity;
                uniform float uReflectionMaxLod;
            )GLSL");
        }

        // A roughness-selected cubemap lookup.
        std::string glslFunction() const noexcept override {
            return shader_tools::glsl(R"GLSL(

                vec3 reflectionSample(vec3 worldPos, vec3 n, vec3 v, float roughness) {
                    vec3 r = reflect(-v, n);
                    float lod = sqrt(clamp(roughness, 0.0, 1.0)) * uReflectionMaxLod;
                    return textureLod(uReflectionCube, r, lod).rgb * uReflectionIntensity;
                }
            )GLSL");
        }

        // Binds the cubemap and writes its unit, intensity and mip ceiling.
        void upload(const Shader& shader) const noexcept override {
            if (_cubemap) _cubemap->bind(static_cast<unsigned int>(_unit));
            shader.setInt("uReflectionCube", _unit);
            shader.setFloat("uReflectionIntensity", _intensity);
            shader.setFloat("uReflectionMaxLod", _maxLod);
        }

        // The cubemap sampled for reflections; non-owning.
        Texture* cubemap() const noexcept {return _cubemap;}
        // Sets the cubemap; it must outlive this source.
        void setCubemap(Texture* cubemap) noexcept {_cubemap = cubemap;}

        // Texture unit the cubemap binds to.
        int textureUnit() const noexcept {return _unit;}
        // Sets the texture unit the cubemap binds to.
        void setTextureUnit(int unit) noexcept {_unit = std::max(unit, 0);}

        // Highest mip level roughness maps to.
        float maxLod() const noexcept {return _maxLod;}
        // Sets the highest mip level, roughly log2 of the face size.
        void setMaxLod(float maxLod) noexcept {_maxLod = std::max(maxLod, 0.0f);}

    private:
        ReflectionCubemap() noexcept = default;

        Texture* _cubemap = nullptr;
        int _unit = 9;
        float _maxLod = 6.0f;
};

// Mirrors the scene through a plane into an offscreen target sampled by screen
// position. Exact for one flat surface such as water or a polished floor.
class ReflectionPlanar final : public ReflectionSource {
    public:
        // Creates a planar source for the plane {p : dot(normal, p) = distance}.
        static std::shared_ptr<ReflectionPlanar> create(const glm::vec3& normal = {0.0f, 1.0f, 0.0f},
                                                        float distance = 0.0f,
                                                        int width = 1024, int height = 1024,
                                                        int textureUnit = 10) noexcept {
            auto source = std::shared_ptr<ReflectionPlanar>(new ReflectionPlanar());
            source->setPlane(normal, distance);
            source->_width = std::max(width, 1);
            source->_height = std::max(height, 1);
            source->setTextureUnit(textureUnit);
            return source;
        }

        // Variant cache key for the planar source.
        std::string name() const noexcept override {return "ReflectionPlanar";}

        // The mirror target, plane equation, intensity, viewport and a capture-ready flag.
        std::string glslUniforms() const noexcept override {
            return shader_tools::glsl(R"GLSL(
                uniform sampler2D uReflectionPlane;
                uniform vec4 uReflectionPlaneEq;
                uniform float uReflectionIntensity;
                uniform vec2 uReflectionViewport;
                uniform float uReflectionPlaneReady;
            )GLSL");
        }

        // The mirrored capture on the plane, the ambient gradient elsewhere.
        std::string glslFunction() const noexcept override {
            return shader_tools::glsl(R"GLSL(

                vec3 reflectionSample(vec3 worldPos, vec3 n, vec3 v, float roughness) {
                    float onPlane = 1.0 - clamp(abs(dot(uReflectionPlaneEq.xyz, worldPos) - uReflectionPlaneEq.w), 0.0, 1.0);
                    float facing = clamp(dot(n, uReflectionPlaneEq.xyz), 0.0, 1.0);

                    vec2 uv = gl_FragCoord.xy / max(uReflectionViewport, vec2(1.0));
                    uv = mix(uv, vec2(0.5), roughness * roughness * 0.02);

                    vec3 mirrored = texture(uReflectionPlane, uv).rgb;
                    vec3 fallback = ambientLight(reflect(-v, n));
                    return mix(fallback, mirrored, onPlane * facing * uReflectionPlaneReady) * uReflectionIntensity;
                }
            )GLSL");
        }

        // Binds the mirror target and writes the plane, intensity and viewport. Until the
        // first render() the current viewport is used and only the fallback is shown.
        void upload(const Shader& shader) const noexcept override {
            if (_target && _target->texture()) {
                _target->texture()->bind(static_cast<unsigned int>(_unit));
            }
            glm::vec2 viewport(static_cast<float>(_viewportWidth), static_cast<float>(_viewportHeight));
            if (!_captured) {
                int current[4] = {0, 0, 1, 1};
                glGetIntegerv(GL_VIEWPORT, current);
                viewport = glm::vec2(static_cast<float>(current[2]), static_cast<float>(current[3]));
            }
            shader.setInt("uReflectionPlane", _unit);
            shader.setVec4("uReflectionPlaneEq", glm::vec4(_normal, _distance));
            shader.setFloat("uReflectionIntensity", _intensity);
            shader.setVec2("uReflectionViewport", viewport);
            shader.setFloat("uReflectionPlaneReady", _captured ? 1.0f : 0.0f);
        }

        // The world-space mirror of a view-projection matrix.
        glm::mat4 mirroredViewProjection(const glm::mat4& viewProjection) const noexcept {
            return viewProjection * mirrorMatrix();
        }

        // Reflection matrix about the plane.
        glm::mat4 mirrorMatrix() const noexcept {
            const glm::vec3 n = _normal;
            glm::mat4 m(1.0f);
            m[0][0] = 1.0f - 2.0f * n.x * n.x;
            m[1][0] = -2.0f * n.x * n.y;
            m[2][0] = -2.0f * n.x * n.z;
            m[3][0] = 2.0f * _distance * n.x;
            m[0][1] = -2.0f * n.y * n.x;
            m[1][1] = 1.0f - 2.0f * n.y * n.y;
            m[2][1] = -2.0f * n.y * n.z;
            m[3][1] = 2.0f * _distance * n.y;
            m[0][2] = -2.0f * n.z * n.x;
            m[1][2] = -2.0f * n.z * n.y;
            m[2][2] = 1.0f - 2.0f * n.z * n.z;
            m[3][2] = 2.0f * _distance * n.z;
            return m;
        }

        // Renders the mirrored scene, recovering the eye position from the matrix.
        bool render(const glm::mat4& viewProjection, const ScenePass& drawScene) noexcept {
            const glm::vec4 eye = glm::inverse(viewProjection) * glm::vec4(0.0f, 0.0f, 1.0f, 0.0f);
            const glm::vec3 viewPosition = (std::abs(eye.w) > 1e-6f) ? glm::vec3(eye) / eye.w
                                                                    : glm::vec3(0.0f);
            return render(viewProjection, viewPosition, drawScene);
        }

        // Renders the mirrored scene into this source's target. The callback binds its own
        // program and should write context.viewPosition and context.clipPlane to it.
        bool render(const glm::mat4& viewProjection, const glm::vec3& viewPosition,
                    const ScenePass& drawScene) noexcept {
            if (!drawScene) return true;
            if (!ensureTarget()) return false;

            int previousFramebuffer = 0;
            int previousViewport[4] = {0, 0, 0, 0};
            int previousFrontFace = GL_CCW;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
            glGetIntegerv(GL_VIEWPORT, previousViewport);
            glGetIntegerv(GL_FRONT_FACE, &previousFrontFace);
            const GLboolean clipWasEnabled = glIsEnabled(GL_CLIP_DISTANCE0);
            _viewportWidth = previousViewport[2];
            _viewportHeight = previousViewport[3];

            _target->bindRaw();
            glViewport(0, 0, _width, _height);
            glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

            const float side = (glm::dot(_normal, viewPosition) - _distance) >= 0.0f ? 1.0f : -1.0f;

            glFrontFace(GL_CW);
            glEnable(GL_CLIP_DISTANCE0);
            SceneContext context;
            context.viewProjection = mirroredViewProjection(viewProjection);
            context.viewPosition = glm::vec3(mirrorMatrix() * glm::vec4(viewPosition, 1.0f));
            context.clipPlane = glm::vec4(_normal, -_distance) * side;
            context.materialsRequired = true;
            drawScene(context);
            if (!clipWasEnabled) glDisable(GL_CLIP_DISTANCE0);
            glFrontFace(static_cast<unsigned int>(previousFrontFace));

            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(previousFramebuffer));
            glViewport(previousViewport[0], previousViewport[1],
                       previousViewport[2], previousViewport[3]);
            _captured = true;
            return true;
        }

        // The reflecting plane's normal.
        glm::vec3 planeNormal() const noexcept {return _normal;}
        // The reflecting plane's distance along its normal.
        float planeDistance() const noexcept {return _distance;}
        // Sets the reflecting plane.
        void setPlane(const glm::vec3& normal, float distance) noexcept {
            const float length = glm::length(normal);
            _normal = (length > 1e-6f) ? normal / length : glm::vec3(0.0f, 1.0f, 0.0f);
            _distance = distance;
        }

        // Texture unit the reflection target binds to.
        int textureUnit() const noexcept {return _unit;}
        // Sets the texture unit the reflection target binds to.
        void setTextureUnit(int unit) noexcept {_unit = std::max(unit, 0);}

    private:
        ReflectionPlanar() noexcept = default;

        // Creates the offscreen colour and depth target on first use.
        bool ensureTarget() noexcept {
            if (_target) return true;
            _target = Framebuffer::createColourDepth(_width, _height);
            if (!_target) {
                log_error("ReflectionPlanar: could not create the reflection target");
                return false;
            }
            return true;
        }

        glm::vec3 _normal{0.0f, 1.0f, 0.0f};
        float _distance = 0.0f;
        int _width = 1024, _height = 1024;
        int _viewportWidth = 1, _viewportHeight = 1;
        int _unit = 10;
        bool _captured = false;
        std::unique_ptr<Framebuffer> _target;
};

// Screen-space reflections: marches the reflection ray through the current depth
// buffer, falling back to the ambient gradient for anything off screen or occluded.
class ReflectionScreenSpace final : public ReflectionSource {
    public:
        // Creates a screen-space source; feed it setSceneTextures and setCamera every frame.
        static std::shared_ptr<ReflectionScreenSpace> create(int colourUnit = 11,
                                                             int depthUnit = 12) noexcept {
            auto source = std::shared_ptr<ReflectionScreenSpace>(new ReflectionScreenSpace());
            source->_colourUnit = std::max(colourUnit, 0);
            source->_depthUnit = std::max(depthUnit, 0);
            return source;
        }

        // Variant cache key for the screen-space source.
        std::string name() const noexcept override {return "ReflectionScreenSpace";}

        // Scene colour and depth, camera matrices and march tuning.
        std::string glslUniforms() const noexcept override {
            return shader_tools::glsl(R"GLSL(
                uniform sampler2D uSsrColour;
                uniform sampler2D uSsrDepth;
                uniform mat4 uSsrView;
                uniform mat4 uSsrProjection;
                uniform float uReflectionIntensity;
                uniform int uSsrSteps;
                uniform float uSsrThickness;
            )GLSL");
        }

        // A view-space ray march against the depth buffer.
        std::string glslFunction() const noexcept override {
            return shader_tools::glsl(R"GLSL(

                vec3 ssrProject(vec3 viewPos) {
                    vec4 clip = uSsrProjection * vec4(viewPos, 1.0);
                    vec3 ndc = clip.xyz / max(clip.w, 1e-5);
                    return ndc * 0.5 + 0.5;
                }

                vec3 reflectionSample(vec3 worldPos, vec3 n, vec3 v, float roughness) {
                    vec3 fallback = ambientLight(reflect(-v, n));

                    vec3 viewPos = (uSsrView * vec4(worldPos, 1.0)).xyz;
                    vec3 viewNormal = normalize(mat3(uSsrView) * n);
                    vec3 rayDir = normalize(reflect(normalize(viewPos), viewNormal));

                    if (rayDir.z > 0.0) return fallback * uReflectionIntensity;

                    int steps = max(uSsrSteps, 1);
                    float stride = 0.5;
                    vec3 position = viewPos;

                    for (int i = 0; i < steps; ++i) {
                        position += rayDir * stride;
                        stride *= 1.15;

                        vec3 screen = ssrProject(position);
                        if (any(lessThan(screen.xy, vec2(0.0))) || any(greaterThan(screen.xy, vec2(1.0)))) break;

                        float sceneDepth = texture(uSsrDepth, screen.xy).r;
                        float delta = screen.z - sceneDepth;

                        if (delta > 0.0 && delta < uSsrThickness) {
                            vec2 edge = smoothstep(vec2(0.0), vec2(0.15), screen.xy)
                                      * smoothstep(vec2(0.0), vec2(0.15), 1.0 - screen.xy);
                            float confidence = edge.x * edge.y * (1.0 - clamp(roughness * 1.5, 0.0, 1.0));
                            vec3 hit = texture(uSsrColour, screen.xy).rgb;
                            return mix(fallback, hit, confidence) * uReflectionIntensity;
                        }
                    }
                    return fallback * uReflectionIntensity;
                }
            )GLSL");
        }

        // Binds the scene textures and writes the camera matrices and tuning.
        void upload(const Shader& shader) const noexcept override {
            if (_colour) _colour->bind(static_cast<unsigned int>(_colourUnit));
            if (_depth) _depth->bind(static_cast<unsigned int>(_depthUnit));
            shader.setInt("uSsrColour", _colourUnit);
            shader.setInt("uSsrDepth", _depthUnit);
            shader.setMat4("uSsrView", _view);
            shader.setMat4("uSsrProjection", _projection);
            shader.setFloat("uReflectionIntensity", _intensity);
            shader.setInt("uSsrSteps", _steps);
            shader.setFloat("uSsrThickness", _thickness);
        }

        // Supplies the scene colour and depth; non-owning, and must outlive this source.
        void setSceneTextures(Texture* colour, Texture* depth) noexcept {
            _colour = colour;
            _depth = depth;
        }

        // Supplies the camera matrices the march runs in; call every frame. The viewport
        // is accepted for compatibility and not needed by the march.
        void setCamera(const glm::mat4& view, const glm::mat4& projection,
                       const glm::vec2& viewport) noexcept {
            (void)viewport;
            _view = view;
            _projection = projection;
        }

        // March step count.
        int steps() const noexcept {return _steps;}
        // Sets the march step count; higher reflects further at linear cost.
        void setSteps(int steps) noexcept {_steps = std::max(steps, 1);}

        // Depth tolerance for accepting a hit, in [0,1] depth units.
        float thickness() const noexcept {return _thickness;}
        // Sets the hit tolerance; too small misses hits, too large smears them.
        void setThickness(float thickness) noexcept {_thickness = std::max(thickness, 1e-5f);}

    private:
        ReflectionScreenSpace() noexcept = default;

        Texture* _colour = nullptr;
        Texture* _depth = nullptr;
        glm::mat4 _view{1.0f};
        glm::mat4 _projection{1.0f};
        int _colourUnit = 11, _depthUnit = 12;
        int _steps = 24;
        float _thickness = 0.002f;
};

// Packs one 3D light for the GPU, with no shadow slice assigned.
inline PackedLight packLight(const Light3D& light) noexcept {
    PackedLight packed;
    packed.positionRange = glm::vec4(light.position(), light.range());
    packed.directionOuter = glm::vec4(light.direction(),
                                      std::cos(glm::radians(light.outerAngle())));
    packed.radianceInner = glm::vec4(light.radiance(),
                                     std::cos(glm::radians(light.innerAngle())));
    packed.params = glm::vec4(shader_tools::typeCode(light.type(), light.areaIsDisc()),
                              light.areaSize().x, light.areaSize().y,
                              light.shadows().type == ShadowType::Soft ? 1.0f : 0.0f);
    packed.shadow = glm::vec4(-1.0f, light.shadows().depthBias,
                              light.shadows().normalBias, light.shadows().strength);
    return packed;
}

// Values for BlinnPhong3D; gloss is a Blinn-Phong exponent, not a 0..1 value.
class BlinnPhongMaterial final : public Material {
    public:
        // Creates a material carrying BlinnPhong3D's default values.
        static std::unique_ptr<BlinnPhongMaterial> create() noexcept {
            return std::unique_ptr<BlinnPhongMaterial>(new BlinnPhongMaterial());
        }

        // The model these uniforms belong to.
        std::string modelName() const noexcept override {return "BlinnPhong3D";}

        // Writes albedo, specular tint, gloss, occlusion and reflectivity.
        void upload(const Shader& shader) const noexcept override {
            shader.setVec3("uAlbedo", albedo);
            shader.setVec3("uSpecularTint", specularTint);
            shader.setFloat("uGloss", gloss);
            shader.setFloat("uOcclusion", occlusion);
            shader.setFloat("uReflectivity", reflectivity);
        }

        // An independent copy of this material.
        std::unique_ptr<Material> clone() const noexcept override {
            auto copy = create();
            copy->albedo = albedo;
            copy->specularTint = specularTint;
            copy->gloss = gloss;
            copy->occlusion = occlusion;
            copy->reflectivity = reflectivity;
            return copy;
        }

        glm::vec3 albedo{1.0f};
        glm::vec3 specularTint{1.0f};
        float gloss = 32.0f;
        float occlusion = 1.0f;
        float reflectivity = 0.0f;

    private:
        BlinnPhongMaterial() noexcept = default;
};

// Values for MetallicRoughness3D; author albedo in linear space.
class MetallicRoughnessMaterial final : public Material {
    public:
        // Creates a material carrying MetallicRoughness3D's default values.
        static std::unique_ptr<MetallicRoughnessMaterial> create() noexcept {
            return std::unique_ptr<MetallicRoughnessMaterial>(new MetallicRoughnessMaterial());
        }

        // The model these uniforms belong to.
        std::string modelName() const noexcept override {return "MetallicRoughness3D";}

        // Writes albedo, metallic, roughness, occlusion and emissive.
        void upload(const Shader& shader) const noexcept override {
            shader.setVec3("uAlbedo", albedo);
            shader.setFloat("uMetallic", metallic);
            shader.setFloat("uRoughness", roughness);
            shader.setFloat("uOcclusion", occlusion);
            shader.setVec3("uEmissive", emissive);
        }

        // An independent copy of this material.
        std::unique_ptr<Material> clone() const noexcept override {
            auto copy = create();
            copy->albedo = albedo;
            copy->metallic = metallic;
            copy->roughness = roughness;
            copy->occlusion = occlusion;
            copy->emissive = emissive;
            return copy;
        }

        glm::vec3 albedo{0.8f};
        float metallic = 0.0f;
        float roughness = 0.5f;
        float occlusion = 1.0f;
        glm::vec3 emissive{0.0f};

    private:
        MetallicRoughnessMaterial() noexcept = default;
};

// Values for Unlit3D: a single emissive colour.
class Unlit3DMaterial final : public Material {
    public:
        // Creates a material carrying Unlit3D's default colour.
        static std::unique_ptr<Unlit3DMaterial> create() noexcept {
            return std::unique_ptr<Unlit3DMaterial>(new Unlit3DMaterial());
        }

        // The model these uniforms belong to.
        std::string modelName() const noexcept override {return "Unlit3D";}
        // Writes the emissive colour.
        void upload(const Shader& shader) const noexcept override {shader.setVec3("uColor", color);}

        // An independent copy of this material.
        std::unique_ptr<Material> clone() const noexcept override {
            auto copy = create();
            copy->color = color;
            return copy;
        }

        glm::vec3 color{1.0f};

    private:
        Unlit3DMaterial() noexcept = default;
};

// Base for models the 3D pipeline accepts.
class LightingModel3D : public LightingModel {
    public:
        // Fixed at 3.
        int dimensions() const noexcept final {return 3;}
    protected:
        LightingModel3D() noexcept = default;
};

// Blinn-Phong with half-Lambert diffuse and a Fresnel-weighted reflection term.
// With default materials it reproduces the pre-split pipeline exactly.
class BlinnPhong3D final : public LightingModel3D {
    public:
        // Creates the Blinn-Phong shading model.
        static std::shared_ptr<BlinnPhong3D> create() noexcept {
            return std::shared_ptr<BlinnPhong3D>(new BlinnPhong3D());
        }

        // Variant cache key for this model.
        std::string name() const noexcept override {return "BlinnPhong3D";}

        // Albedo, specular tint, gloss exponent, occlusion and reflectivity.
        std::string glslSurface() const noexcept override {
            return shader_tools::glsl(R"GLSL(

                struct Surface {
                    vec3 albedo;
                    vec3 specularTint;
                    float gloss;
                    float occlusion;
                    float reflectivity;
                };
            )GLSL");
        }

        // The uniforms BlinnPhongMaterial writes.
        std::string glslMaterialUniforms() const noexcept override {
            return shader_tools::glsl(R"GLSL(
                uniform vec3 uAlbedo;
                uniform vec3 uSpecularTint;
                uniform float uGloss;
                uniform float uOcclusion;
                uniform float uReflectivity;
            )GLSL");
        }

        // Fills Surface straight from those uniforms.
        std::string glslMaterial() const noexcept override {
            return shader_tools::glsl(R"GLSL(

                Surface makeSurface() {
                    Surface s;
                    s.albedo = uAlbedo;
                    s.specularTint = uSpecularTint;
                    s.gloss = uGloss;
                    s.occlusion = uOcclusion;
                    s.reflectivity = uReflectivity;
                    return s;
                }
            )GLSL");
        }

        // Wrapped diffuse plus Blinn-Phong specular, with a non-energy-conserving ambient reflection.
        std::string glslShading() const noexcept override {
            return shader_tools::glsl(R"GLSL(

                float wrappedDiffuse(vec3 n, vec3 l, float wrap) {
                    return clamp((dot(n, l) + wrap) / (1.0 + wrap), 0.0, 1.0);
                }

                vec3 brdf(Surface s, LightSample light, vec3 n, vec3 v) {
                    float shininess = max(s.gloss, 1e-3);
                    vec3 diffuse = s.albedo * wrappedDiffuse(n, light.direction, 0.35);

                    vec3 halfway = normalize(light.direction + v);
                    float ndoth = max(dot(n, halfway), 0.0);
                    vec3 specular = s.specularTint * pow(ndoth, shininess) * step(0.0, dot(n, light.direction));

                    return light.radiance * (diffuse + specular);
                }

                vec3 ambientTerm(Surface s, vec3 worldPos, vec3 n, vec3 v) {
                    vec3 diffuse = s.albedo * ambientLight(n) * s.occlusion;
                    float fresnel = pow(1.0 - clamp(dot(n, v), 0.0, 1.0), 5.0);
                    float gloss01 = clamp(s.gloss / 128.0, 0.0, 1.0);
                    vec3 reflected = reflectionSample(worldPos, n, v, 1.0 - gloss01);
                    return diffuse + reflected * s.specularTint * s.reflectivity * mix(0.04, 1.0, fresnel);
                }
            )GLSL");
        }

        // A material carrying this model's default values.
        std::unique_ptr<Material> createMaterial() const noexcept override {return BlinnPhongMaterial::create();}

    private:
        BlinnPhong3D() noexcept = default;
};

// Cook-Torrance metallic-roughness (GGX, Smith height-correlated, Schlick). Energy
// conserving; expects linear input and a tone map other than None.
class MetallicRoughness3D final : public LightingModel3D {
    public:
        // Creates the Cook-Torrance metallic-roughness shading model.
        static std::shared_ptr<MetallicRoughness3D> create() noexcept {
            return std::shared_ptr<MetallicRoughness3D>(new MetallicRoughness3D());
        }

        // Variant cache key for this model.
        std::string name() const noexcept override {return "MetallicRoughness3D";}

        // Albedo, metallic, roughness, occlusion and emissive.
        std::string glslSurface() const noexcept override {
            return shader_tools::glsl(R"GLSL(

                struct Surface {
                    vec3 albedo;
                    float metallic;
                    float roughness;
                    float occlusion;
                    vec3 emissive;
                };
            )GLSL");
        }

        // The uniforms MetallicRoughnessMaterial writes.
        std::string glslMaterialUniforms() const noexcept override {
            return shader_tools::glsl(R"GLSL(
                uniform vec3 uAlbedo;
                uniform float uMetallic;
                uniform float uRoughness;
                uniform float uOcclusion;
                uniform vec3 uEmissive;
            )GLSL");
        }

        // Fills Surface, clamping metallic and flooring roughness to keep GGX finite.
        std::string glslMaterial() const noexcept override {
            return shader_tools::glsl(R"GLSL(

                Surface makeSurface() {
                    Surface s;
                    s.albedo = uAlbedo;
                    s.metallic = clamp(uMetallic, 0.0, 1.0);
                    s.roughness = clamp(uRoughness, 0.045, 1.0);
                    s.occlusion = uOcclusion;
                    s.emissive = uEmissive;
                    return s;
                }
            )GLSL");
        }

        // GGX direct lighting with a split-sum ambient term using an analytic BRDF fit.
        std::string glslShading() const noexcept override {
            return shader_tools::glsl(R"GLSL(

                const float kPi = 3.14159265359;

                float distributionGGX(float ndoth, float roughness) {
                    float a = roughness * roughness;
                    float a2 = a * a;
                    float d = ndoth * ndoth * (a2 - 1.0) + 1.0;
                    return a2 / max(kPi * d * d, 1e-7);
                }

                float visibilitySmith(float ndotv, float ndotl, float roughness) {
                    float a = roughness * roughness;
                    float a2 = a * a;
                    float lambdaV = ndotl * sqrt(ndotv * ndotv * (1.0 - a2) + a2);
                    float lambdaL = ndotv * sqrt(ndotl * ndotl * (1.0 - a2) + a2);
                    return 0.5 / max(lambdaV + lambdaL, 1e-7);
                }

                vec3 fresnelSchlick(vec3 f0, float vdoth) {
                    return f0 + (1.0 - f0) * pow(1.0 - vdoth, 5.0);
                }

                vec3 fresnelRoughness(vec3 f0, float ndotv, float roughness) {
                    vec3 ceiling = max(vec3(1.0 - roughness), f0);
                    return f0 + (ceiling - f0) * pow(1.0 - ndotv, 5.0);
                }

                vec2 environmentBRDF(float ndotv, float roughness) {
                    vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022);
                    vec4 c1 = vec4(1.0, 0.0425, 1.04, -0.04);
                    vec4 r = roughness * c0 + c1;
                    float a004 = min(r.x * r.x, exp2(-9.28 * ndotv)) * r.x + r.y;
                    return vec2(-1.04, 1.04) * a004 + r.zw;
                }

                vec3 brdf(Surface s, LightSample light, vec3 n, vec3 v) {
                    vec3 l = light.direction;
                    float ndotl = clamp(dot(n, l), 0.0, 1.0);
                    if (ndotl <= 0.0) return vec3(0.0);

                    float ndotv = clamp(abs(dot(n, v)), 1e-4, 1.0);
                    vec3 h = normalize(l + v);
                    float ndoth = clamp(dot(n, h), 0.0, 1.0);
                    float vdoth = clamp(dot(v, h), 0.0, 1.0);

                    vec3 f0 = mix(vec3(0.04), s.albedo, s.metallic);
                    vec3 fresnel = fresnelSchlick(f0, vdoth);

                    vec3 specular = fresnel
                                  * distributionGGX(ndoth, s.roughness)
                                  * visibilitySmith(ndotv, ndotl, s.roughness);

                    vec3 diffuse = (1.0 - fresnel) * (1.0 - s.metallic) * s.albedo / kPi;

                    return light.radiance * ndotl * (diffuse + specular);
                }

                vec3 ambientTerm(Surface s, vec3 worldPos, vec3 n, vec3 v) {
                    float ndotv = clamp(abs(dot(n, v)), 1e-4, 1.0);
                    vec3 f0 = mix(vec3(0.04), s.albedo, s.metallic);

                    vec3 irradiance = ambientLight(n);
                    vec3 fresnel = fresnelRoughness(f0, ndotv, s.roughness);
                    vec3 diffuse = (1.0 - fresnel) * (1.0 - s.metallic) * s.albedo * irradiance;

                    vec3 prefiltered = reflectionSample(worldPos, n, v, s.roughness);
                    vec2 ab = environmentBRDF(ndotv, s.roughness);
                    vec3 specular = prefiltered * (f0 * ab.x + ab.y);

                    return (diffuse + specular) * s.occlusion + s.emissive;
                }
            )GLSL");
        }

        // A material carrying this model's default values.
        std::unique_ptr<Material> createMaterial() const noexcept override {return MetallicRoughnessMaterial::create();}

    private:
        MetallicRoughness3D() noexcept = default;
};

// Emissive 3D surfaces for gizmos, UI geometry and light proxies; ignores all lighting.
class Unlit3D final : public LightingModel3D {
    public:
        // Creates the unlit 3D shading model.
        static std::shared_ptr<Unlit3D> create() noexcept {
            return std::shared_ptr<Unlit3D>(new Unlit3D());
        }

        // Variant cache key for this model.
        std::string name() const noexcept override {return "Unlit3D";}

        // One emissive colour.
        std::string glslSurface() const noexcept override {
            return shader_tools::glsl(R"GLSL(

                struct Surface {vec3 color;};
            )GLSL");
        }

        // The uniform Unlit3DMaterial writes.
        std::string glslMaterialUniforms() const noexcept override {
            return shader_tools::glsl(R"GLSL(
                uniform vec3 uColor;
            )GLSL");
        }

        // Fills Surface from that uniform.
        std::string glslMaterial() const noexcept override {
            return shader_tools::glsl(R"GLSL(

                Surface makeSurface() {Surface s; s.color = uColor; return s;}
            )GLSL");
        }

        // No direct contribution; the ambient term is the colour itself.
        std::string glslShading() const noexcept override {
            return shader_tools::glsl(R"GLSL(

                vec3 brdf(Surface s, LightSample light, vec3 n, vec3 v) {return vec3(0.0);}
                vec3 ambientTerm(Surface s, vec3 worldPos, vec3 n, vec3 v) {return s.color;}
            )GLSL");
        }

        // A material carrying this model's default values.
        std::unique_ptr<Material> createMaterial() const noexcept override {return Unlit3DMaterial::create();}

        // Ignores lights and ambient.
        bool isLit() const noexcept override {return false;}

    private:
        Unlit3D() noexcept = default;
};

// The 3D lighting path: perspective, per-pixel normals, all four light types,
// shadow maps for directional and spot lights, and a swappable reflection source.
class LightingPipeline3D final : public LightingPipeline {
    public:
        // Creates a 3D pipeline using BlinnPhong3D and a gradient environment.
        static std::unique_ptr<LightingPipeline3D> create() noexcept {
            return std::unique_ptr<LightingPipeline3D>(new LightingPipeline3D());
        }

        // This pipeline consumes 3D lights and models.
        int dimensions() const noexcept override {return 3;}

        // Empties the light set and releases every shadow slice.
        void clear() noexcept override {
            _packed.clear();
            _shadowCount = 0;
        }

        // Adds a light; false once the set is full. Disabled lights are skipped and return
        // true. Shadow-casting directional and spot lights claim slices in submission order.
        bool add(const Light3D& light) noexcept {
            if (!light.enabled()) return true;
            if (static_cast<int>(_packed.size()) >= shader_tools::kMaxLights) return false;

            PackedLight packed = packLight(light);
            if (light.castsShadows() && canCastShadows(light.type())
                && _shadowCount < shader_tools::kMaxShadows) {
                const bool directional = light.type() == LightType::Directional;
                const glm::vec3 focus = _hasShadowFocus ? _shadowFocus : light.position();
                const size_t slice = static_cast<size_t>(_shadowCount);
                packed.shadow.x = static_cast<float>(_shadowCount);
                _shadowMatrices[slice] = light.shadowViewProjection(_directionalExtent, focus, _shadowResolution);

                ShadowVolume& volume = _shadowVolumes[slice];
                volume.centre = directional ? focus : light.position();
                volume.radius = directional ? _directionalExtent * 1.7320508f : light.range();
                volume.layerMask = light.cullingMask();
                ++_shadowCount;
            }
            _packed.push_back(packed);
            return true;
        }

        // Adds the light only if it reaches the given sphere; directional lights always apply.
        bool addIfRelevant(const Light3D& light, const glm::vec3& centre, float radius) noexcept {
            if (light.type() != LightType::Directional) {
                const glm::vec4 sphere = light.boundingSphere();
                if (glm::length(glm::vec3(sphere) - centre) > sphere.w + radius) return true;
            }
            return add(light);
        }

        // The active shading model.
        const LightingModel3D& model() const noexcept {
            return static_cast<const LightingModel3D&>(modelBase());
        }
        // Replaces the shading model; null restores BlinnPhong3D.
        void setModel(std::shared_ptr<LightingModel3D> model) noexcept {
            _model = model ? std::static_pointer_cast<LightingModel>(std::move(model))
                           : std::static_pointer_cast<LightingModel>(BlinnPhong3D::create());
        }

        // The active environment light.
        const EnvironmentLight3D& environment() const noexcept {
            return static_cast<const EnvironmentLight3D&>(environmentBase());
        }
        // Mutable access to the active environment light.
        EnvironmentLight3D& environment() noexcept {
            return static_cast<EnvironmentLight3D&>(*_environment);
        }
        // Replaces the environment light; null restores the default gradient.
        void setEnvironment(std::shared_ptr<EnvironmentLight3D> environment) noexcept {
            _environment = environment
                ? std::static_pointer_cast<EnvironmentLight>(std::move(environment))
                : std::static_pointer_cast<EnvironmentLight>(EnvironmentLight3D::create());
        }

        // The active reflection source.
        const ReflectionSource& reflection() const noexcept {return *_reflection;}
        // Mutable access to the active reflection source.
        ReflectionSource& reflection() noexcept {return *_reflection;}
        // Replaces the reflection source; null restores the environment fallback.
        void setReflection(std::shared_ptr<ReflectionSource> reflection) noexcept {
            _reflection = reflection ? std::move(reflection) : ReflectionEnvironment::create();
        }

        // Fills each claimed shadow slice by re-issuing the scene. The context carries a bound
        // depth-only program (uLightViewProjection set; the callback sets uModel and draws) and
        // an unbound instanced one for instanced geometry. Call after the light set is built
        // and before the lit pass. False if resources could not be created.
        bool renderShadows(const ScenePass& drawScene) noexcept {
            if (!_shadowsEnabled || _shadowCount == 0 || !drawScene) return true;
            if (!ensureShadowResources() || !ensureDepthShader()) return false;

            int previousFramebuffer = 0;
            int previousProgram = 0;
            int previousViewport[4] = {0, 0, 0, 0};
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previousFramebuffer);
            glGetIntegerv(GL_CURRENT_PROGRAM, &previousProgram);
            glGetIntegerv(GL_VIEWPORT, previousViewport);

            const GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
            const GLboolean cullWasEnabled = glIsEnabled(GL_CULL_FACE);
            const GLboolean offsetWasEnabled = glIsEnabled(GL_POLYGON_OFFSET_FILL);
            GLboolean depthMaskWasEnabled = GL_TRUE;
            glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMaskWasEnabled);
            int previousCullFace = GL_BACK;
            glGetIntegerv(GL_CULL_FACE_MODE, &previousCullFace);
            float previousOffsetFactor = 0.0f, previousOffsetUnits = 0.0f;
            glGetFloatv(GL_POLYGON_OFFSET_FACTOR, &previousOffsetFactor);
            glGetFloatv(GL_POLYGON_OFFSET_UNITS, &previousOffsetUnits);

            glEnable(GL_DEPTH_TEST);
            glDepthMask(GL_TRUE);
            glViewport(0, 0, _shadowResolution, _shadowResolution);

            if (_slopeBias != 0.0f || _constantBias != 0.0f) {
                glEnable(GL_POLYGON_OFFSET_FILL);
                glPolygonOffset(_slopeBias, _constantBias);
            }
            if (_cullFrontFaces) {
                glEnable(GL_CULL_FACE);
                glCullFace(GL_FRONT);
            }

            for (int slice = 0; slice < _shadowCount; ++slice) {
                const size_t index = static_cast<size_t>(slice);
                _shadowTargets[index]->bindRaw();
                glClear(GL_DEPTH_BUFFER_BIT);
                if (_depthShaderInstanced) {
                    _depthShaderInstanced->setMat4("uLightViewProjection", _shadowMatrices[index]);
                }
                _depthShader->use();
                _depthShader->setMat4("uLightViewProjection", _shadowMatrices[index]);

                SceneContext context;
                context.shader = _depthShader.get();
                context.instancedShader = _depthShaderInstanced.get();
                context.viewProjection = _shadowMatrices[index];
                context.materialsRequired = false;
                context.shadowCasters = true;
                context.cullCentre = _shadowVolumes[index].centre;
                context.cullRadius = _shadowVolumes[index].radius;
                context.layerMask = _shadowVolumes[index].layerMask;
                drawScene(context);
            }

            if (_slopeBias != 0.0f || _constantBias != 0.0f) {
                glPolygonOffset(previousOffsetFactor, previousOffsetUnits);
                if (!offsetWasEnabled) glDisable(GL_POLYGON_OFFSET_FILL);
            }
            if (_cullFrontFaces) {
                glCullFace(static_cast<unsigned int>(previousCullFace));
                if (!cullWasEnabled) glDisable(GL_CULL_FACE);
            }
            glUseProgram(static_cast<unsigned int>(previousProgram));
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(previousFramebuffer));
            glViewport(previousViewport[0], previousViewport[1],
                       previousViewport[2], previousViewport[3]);
            glDepthMask(depthMaskWasEnabled);
            if (!depthWasEnabled) glDisable(GL_DEPTH_TEST);
            return true;
        }

        // Slope-scaled and constant glPolygonOffset applied during the shadow pass.
        // Raise the slope factor first for acne; the constant only if flat surfaces shimmer.
        void setShadowPolygonOffset(float slopeFactor, float constantUnits) noexcept {
            _slopeBias = slopeFactor;
            _constantBias = constantUnits;
        }
        // The slope-scaled polygon offset factor.
        float shadowSlopeBias() const noexcept {return _slopeBias;}
        // The constant polygon offset in depth units.
        float shadowConstantBias() const noexcept {return _constantBias;}

        // True when the shadow pass renders back faces only.
        bool shadowCullsFrontFaces() const noexcept {return _cullFrontFaces;}
        // Renders back faces into the shadow map; drops single-sided geometry from it.
        void setShadowCullFrontFaces(bool cull) noexcept {_cullFrontFaces = cull;}

        // True when shadow rendering is on.
        bool shadowsEnabled() const noexcept {return _shadowsEnabled;}
        // Enables or disables shadows; takes effect at the next renderShadows() and upload().
        void setShadowsEnabled(bool enabled) noexcept {_shadowsEnabled = enabled;}

        // Shadow slices claimed by the current light set.
        int shadowCount() const noexcept {return _shadowCount;}
        // The most shadow-casting lights one draw can carry.
        static constexpr int maxShadows() noexcept {return shader_tools::kMaxShadows;}

        // Edge length of each shadow map, in texels.
        int shadowResolution() const noexcept {return _shadowResolution;}
        // Sets the shadow map size, rebuilding the depth array on next use.
        void setShadowResolution(int resolution) noexcept {
            const int clamped = std::max(resolution, 1);
            if (clamped == _shadowResolution) return;
            _shadowResolution = clamped;
            _shadowTargets.clear();
            _shadowTexture.reset();
        }

        // Half-extent of a directional light's shadow box, in world units.
        float directionalExtent() const noexcept {return _directionalExtent;}
        // Sets the directional shadow box half-extent.
        void setDirectionalExtent(float extent) noexcept {
            _directionalExtent = std::max(extent, 0.01f);
        }

        // Centre of directional shadow boxes, normally the camera or its target. Applies
        // to lights added afterwards; until set, each light's own position is used.
        void setShadowFocus(const glm::vec3& focus) noexcept {
            _shadowFocus = focus;
            _hasShadowFocus = true;
        }
        // Reverts directional shadow boxes to each light's own position.
        void clearShadowFocus() noexcept {_hasShadowFocus = false;}
        // The directional shadow focus, meaningful while hasShadowFocus() is true.
        glm::vec3 shadowFocus() const noexcept {return _shadowFocus;}
        // True when a directional shadow focus has been set.
        bool hasShadowFocus() const noexcept {return _hasShadowFocus;}

        // Texture unit the shadow array binds to.
        int shadowTextureUnit() const noexcept {return _shadowUnit;}
        // Sets the texture unit the shadow array binds to.
        void setShadowTextureUnit(int unit) noexcept {_shadowUnit = std::max(unit, 0);}

    protected:
        // The active reflection source's uniforms and reflectionSample().
        std::string glslReflectionBlock() const noexcept override {
            return _reflection->glslUniforms() + _reflection->glslFunction();
        }

        // The reflection source's name, since swapping it changes the generated source.
        std::string variantSuffix() const noexcept override {return _reflection->name();}

        // Uploads the reflection source and the shadow array, unassigning slices while
        // shadows are disabled.
        void uploadDimensionState(const Shader& shader) const noexcept override {
            _reflection->upload(shader);
            if (!ensureShadowResources()) return;

            _shadowTexture->bind(static_cast<unsigned int>(_shadowUnit));
            shader.setInt("uShadowMaps", _shadowUnit);
            shader.setVec2("uShadowTexelSize", glm::vec2(1.0f / static_cast<float>(_shadowResolution)));
            if (_shadowCount > 0) {
                shader.setMat4Array("uShadowMatrices", _shadowMatrices.data(), _shadowCount);
            }
            if (_shadowsEnabled) return;
            for (size_t i = 0; i < _packed.size(); ++i) {
                if (_packed[i].shadow.x < 0.0f) continue;
                glm::vec4 unshadowed = _packed[i].shadow;
                unshadowed.x = -1.0f;
                shader.setVec4(shader_tools::lightUniformName(
                    static_cast<int>(i) * shader_tools::kVec4sPerLight + 4), unshadowed);
            }
        }

        // Range and cone attenuation, the closest-point area approximation and the shadow lookup.
        std::string glslLightSampling() const noexcept override {
            return "const int kMaxShadows = " + std::to_string(shader_tools::kMaxShadows) + ";\n"
                 + shader_tools::glsl(R"GLSL(

                struct LightSample {
                    vec3 direction;
                    vec3 radiance;
                };

                uniform mat4 uShadowMatrices[kMaxShadows];
                uniform sampler2DArrayShadow uShadowMaps;
                uniform vec2 uShadowTexelSize;

                float rangeFalloff(float dist, float range) {
                    float r = max(range, 1e-4);
                    float x = dist / r;
                    float window = clamp(1.0 - x * x * x * x, 0.0, 1.0);
                    float ref = 0.25 * r;
                    return window * window * (ref * ref) / (dist * dist + 0.25 * ref * ref);
                }

                float coneFalloff(vec3 toSurface, vec3 spotDir, float cosOuter, float cosInner) {
                    float cd = dot(toSurface, spotDir);
                    float t = clamp((cd - cosOuter) / max(cosInner - cosOuter, 1e-4), 0.0, 1.0);
                    return t * t;
                }

                vec3 perpendicularTo(vec3 n) {
                    vec3 reference = abs(n.y) > 0.99 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 1.0, 0.0);
                    return normalize(cross(n, reference));
                }

                float shadowFactor(vec4 shadowData, float soft, vec3 worldPos, vec3 n, vec3 toLight) {
                    int slice = int(shadowData.x + 0.5);
                    if (shadowData.x < 0.0) return 1.0;

                    float ndotl = clamp(dot(n, toLight), 0.0, 1.0);
                    vec3 offsetPos = worldPos + n * shadowData.z * (1.0 - ndotl);

                    vec4 lightClip = uShadowMatrices[slice] * vec4(offsetPos, 1.0);
                    if (lightClip.w <= 0.0) return 1.0;

                    vec3 proj = (lightClip.xyz / lightClip.w) * 0.5 + 0.5;
                    if (proj.z > 1.0) return 1.0;
                    if (any(lessThan(proj.xy, vec2(0.0))) || any(greaterThan(proj.xy, vec2(1.0)))) return 1.0;

                    float depth = proj.z - shadowData.y * max(1.0 - ndotl, 0.05);
                    float lit = 0.0;

                    if (soft > 0.5) {
                        for (int y = -1; y <= 1; ++y) {
                            for (int x = -1; x <= 1; ++x) {
                                vec2 offset = vec2(float(x), float(y)) * uShadowTexelSize;
                                lit += texture(uShadowMaps, vec4(proj.xy + offset, float(slice), depth));
                            }
                        }
                        lit /= 9.0;
                    } else {
                        lit = texture(uShadowMaps, vec4(proj.xy, float(slice), depth));
                    }

                    lit *= smoothstep(0.0, 0.15, ndotl);
                    return mix(1.0, lit, clamp(shadowData.w, 0.0, 1.0));
                }

                LightSample sampleLight(int index, vec3 worldPos, vec3 n) {
                    int base = index * kVec4sPerLight;
                    vec4 positionRange = uLights[base];
                    vec4 directionOuter = uLights[base + 1];
                    vec4 radianceInner = uLights[base + 2];
                    vec4 params = uLights[base + 3];
                    vec4 shadowData = uLights[base + 4];

                    float kind = params.x;
                    LightSample result;
                    float attenuation = 1.0;

                    if (kind < 0.5) {
                        result.direction = -directionOuter.xyz;
                    } else {
                        vec3 offset = positionRange.xyz - worldPos;
                        if (kind > 2.5) {
                            vec3 spanX = perpendicularTo(directionOuter.xyz);
                            vec3 spanY = normalize(cross(directionOuter.xyz, spanX));
                            if (kind > 3.5) {
                                float radius = max(params.y, 0.0);
                                vec2 planar = vec2(dot(-offset, spanX), dot(-offset, spanY));
                                float planarLength = length(planar);
                                if (planarLength > 1e-5) {
                                    vec2 clamped = planar * (min(planarLength, radius) / planarLength);
                                    offset += spanX * clamped.x + spanY * clamped.y;
                                }
                            } else {
                                vec2 halfExtent = 0.5 * params.yz;
                                offset += spanX * clamp(dot(-offset, spanX), -halfExtent.x, halfExtent.x)
                                        + spanY * clamp(dot(-offset, spanY), -halfExtent.y, halfExtent.y);
                            }
                        }
                        float dist = max(length(offset), 1e-4);
                        result.direction = offset / dist;
                        attenuation = rangeFalloff(dist, positionRange.w);
                        if (kind > 1.5 && kind < 2.5) {
                            attenuation *= coneFalloff(-result.direction, directionOuter.xyz,
                                                       directionOuter.w, radianceInner.w);
                        }
                    }

                    attenuation *= shadowFactor(shadowData, params.w, worldPos, n, result.direction);

                    result.radiance = radianceInner.rgb * max(attenuation, 0.0);
                    return result;
                }
            )GLSL");
        }

        // World position, normal and UV from uModel, or from per-instance attributes
        // (position/scale at location 3, rotation quaternion at 4). Writes the
        // uClipPlane distance used by planar reflection captures.
        std::string buildVertexSource(bool instanced) const noexcept override {
            std::string source = sourceHeader() + shader_tools::glsl(R"GLSL(

                layout(location = 0) in vec3 aPosition;
                layout(location = 1) in vec2 aUV;
                layout(location = 2) in vec3 aNormal;

                uniform mat4 uViewProjection;
                uniform vec4 uClipPlane;

                out vec3 vWorldPos;
                out vec3 vNormal;
                out vec2 vUV;
            )GLSL");

            if (!instanced) {
                source += shader_tools::glsl(R"GLSL(

                    uniform mat4 uModel;
                    uniform mat3 uNormalMatrix;

                    void main() {
                        vec4 world = uModel * vec4(aPosition, 1.0);
                        vWorldPos = world.xyz;
                        vNormal = uNormalMatrix * aNormal;
                        vUV = aUV;
                        gl_ClipDistance[0] = dot(world, uClipPlane);
                        gl_Position = uViewProjection * world;
                    }
                )GLSL");
                return source;
            }

            source += shader_tools::glsl(R"GLSL(

                layout(location = 3) in vec4 aInstancePosScale;
                layout(location = 4) in vec4 aInstanceRotation;
            )GLSL") + shader_tools::quaternionRotateGLSL() + shader_tools::glsl(R"GLSL(

                void main() {
                    vec3 scaled = aPosition * aInstancePosScale.w;
                    vec3 world = rotateByQuaternion(aInstanceRotation, scaled) + aInstancePosScale.xyz;
                    vWorldPos = world;
                    vNormal = rotateByQuaternion(aInstanceRotation, aNormal);
                    vUV = aUV;
                    gl_ClipDistance[0] = dot(vec4(world, 1.0), uClipPlane);
                    gl_Position = uViewProjection * vec4(world, 1.0);
                }
            )GLSL");
            return source;
        }

        // Shades the interpolated surface against the view vector and tone maps.
        std::string glslFragmentMain(bool instanced) const noexcept override {
            (void)instanced;
            return shader_tools::glsl(R"GLSL(

                in vec3 vWorldPos;
                in vec3 vNormal;
                in vec2 vUV;

                uniform vec3 uViewPosition;

                out vec4 FragColor;

                void main() {
                    vec3 n = normalize(vNormal);
                    vec3 v = normalize(uViewPosition - vWorldPos);
                    FragColor = vec4(applyToneMap(shadeSurface(makeSurface(), vWorldPos, n, v)), 1.0);
                }
            )GLSL");
        }

    private:
        LightingPipeline3D() noexcept {
            _model = BlinnPhong3D::create();
            _environment = EnvironmentLight3D::create();
            _reflection = ReflectionEnvironment::create();
        }

        // Point and area lights would need cube maps, which are not supported.
        static bool canCastShadows(LightType type) noexcept {
            return type == LightType::Directional || type == LightType::Spot;
        }

        // Creates the shadow depth array and one framebuffer per slice. Always created,
        // so the shadow sampler has a complete texture bound.
        bool ensureShadowResources() const noexcept {
            if (_shadowTexture && static_cast<int>(_shadowTargets.size()) == shader_tools::kMaxShadows) {
                return true;
            }

            if (!_shadowTexture) {
                TextureSpec spec;
                spec.type = TextureType::Texture2DArray;
                spec.width = _shadowResolution;
                spec.height = _shadowResolution;
                spec.layers = shader_tools::kMaxShadows;
                spec.internalFormat = GL_DEPTH_COMPONENT24;
                spec.minFilter = GL_LINEAR;
                spec.magFilter = GL_LINEAR;
                spec.wrapS = GL_CLAMP_TO_EDGE;
                spec.wrapT = GL_CLAMP_TO_EDGE;
                spec.depthCompare = true;
                spec.compareFunc = GL_LEQUAL;
                spec.debugLabel = "lighting/shadow_array";

                _shadowTexture = Texture::createEmpty(spec);
                if (!_shadowTexture) {
                    log_error("LightingPipeline3D: could not create the shadow depth array");
                    return false;
                }
                _shadowTargets.clear();
            }

            for (int slice = static_cast<int>(_shadowTargets.size()); slice < shader_tools::kMaxShadows; ++slice) {
                FramebufferConfig config;
                config.width = _shadowResolution;
                config.height = _shadowResolution;
                config.hasColour = false;

                FramebufferAttachment depth;
                depth.point = GL_DEPTH_ATTACHMENT;
                depth.externalTexture = _shadowTexture.get();
                depth.layer = slice;
                config.attachments = {depth};

                auto target = Framebuffer::create(config);
                if (!target) {
                    log_error("LightingPipeline3D: could not create shadow framebuffer for slice ", slice);
                    _shadowTargets.clear();
                    return false;
                }
                _shadowTargets.push_back(std::move(target));
            }
            return true;
        }

        // Builds the depth-only programs, rebuilding them when the version or defines change.
        // A failed instanced program only leaves instanced geometry out of the shadow maps.
        bool ensureDepthShader() noexcept {
            if (_depthShader && _depthVersion == versionDirective() && _depthDefines == defines()) {
                return true;
            }

            ShaderConfig config;
            config.vertex = {ShaderSource::fromText(
                sourceHeader() + shader_tools::glsl(R"GLSL(

                    layout(location = 0) in vec3 aPosition;
                    uniform mat4 uModel;
                    uniform mat4 uLightViewProjection;
                    void main() {gl_Position = uLightViewProjection * uModel * vec4(aPosition, 1.0);}
                )GLSL"), "lighting/shadow_depth.vert")};
            config.fragment = {ShaderSource::fromText(
                sourceHeader() + shader_tools::glsl(R"GLSL(

                    void main() {}
                )GLSL"), "lighting/shadow_depth.frag")};

            _depthShader = Shader::create(config);
            if (!_depthShader) {
                log_error("LightingPipeline3D: could not build the shadow depth shader");
                return false;
            }

            ShaderConfig instancedConfig;
            instancedConfig.vertex = {ShaderSource::fromText(
                sourceHeader() + shader_tools::glsl(R"GLSL(

                    layout(location = 0) in vec3 aPosition;
                    layout(location = 3) in vec4 aInstancePosScale;
                    layout(location = 4) in vec4 aInstanceRotation;
                    uniform mat4 uLightViewProjection;
                )GLSL") + shader_tools::quaternionRotateGLSL() + shader_tools::glsl(R"GLSL(

                    void main() {
                        vec3 world = rotateByQuaternion(aInstanceRotation, aPosition * aInstancePosScale.w)
                                   + aInstancePosScale.xyz;
                        gl_Position = uLightViewProjection * vec4(world, 1.0);
                    }
                )GLSL"), "lighting/shadow_depth_instanced.vert")};
            instancedConfig.fragment = config.fragment;

            _depthShaderInstanced = Shader::create(instancedConfig);
            if (!_depthShaderInstanced) {
                log_error("LightingPipeline3D: could not build the instanced shadow depth shader");
            }
            _depthVersion = versionDirective();
            _depthDefines = defines();
            return true;
        }

        // Bounds of what one shadow slice can reach, handed to the pass for culling.
        struct ShadowVolume {
            glm::vec3 centre{0.0f};
            float radius = 0.0f;
            unsigned int layerMask = 0xFFFFFFFFu;
        };

        bool _shadowsEnabled = true;
        int _shadowCount = 0;
        int _shadowResolution = 1024;
        int _shadowUnit = 8;
        float _directionalExtent = 25.0f;
        float _slopeBias = 2.0f;
        float _constantBias = 4.0f;
        bool _cullFrontFaces = false;
        bool _hasShadowFocus = false;
        glm::vec3 _shadowFocus{0.0f};

        std::array<glm::mat4, shader_tools::kMaxShadows> _shadowMatrices{};
        std::array<ShadowVolume, shader_tools::kMaxShadows> _shadowVolumes{};

        std::shared_ptr<ReflectionSource> _reflection;

        mutable std::unique_ptr<Texture> _shadowTexture;
        mutable std::vector<std::unique_ptr<Framebuffer>> _shadowTargets;
        std::unique_ptr<Shader> _depthShader;
        std::unique_ptr<Shader> _depthShaderInstanced;
        std::string _depthVersion;
        std::vector<std::string> _depthDefines;
};