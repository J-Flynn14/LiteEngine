#pragma once
#include "light_core.h"

// Configuration for a Light2D. Positions, range and height are Camera2D world units,
// angles are half-angles in degrees, and a falloff exponent above 1 tightens the pool of
// light. Height (point and spot) and elevation (directional, 90 is straight down) only
// affect normal-mapped sprites.
struct Light2DConfig : LightConfig {
    LightType type = LightType::Point;
    glm::vec2 position{0.0f};
    glm::vec2 direction{0.0f, -1.0f};
    float range = 200.0f;
    float innerAngle = 25.0f;
    float outerAngle = 40.0f;
    float falloff = 1.0f;
    float height = 64.0f;
    float elevation = 45.0f;
};

// A light for the orthographic 2D path: directional, point or spot. Area is not supported.
class Light2D final : public Light {
    public:
        // Creates a 2D light from a full config.
        static std::unique_ptr<Light2D> create(const Light2DConfig& config = {}) noexcept {
            return std::unique_ptr<Light2D>(new Light2D(config));
        }

        // Creates an omnidirectional 2D light.
        static std::unique_ptr<Light2D> createPoint(const glm::vec2& position, float range,
                                                    const glm::vec3& color = glm::vec3(1.0f),
                                                    float intensity = 1.0f) noexcept {
            Light2DConfig config;
            config.type = LightType::Point;
            config.position = position;
            config.range = range;
            config.color = color;
            config.intensity = intensity;
            return create(config);
        }

        // Creates a cone-shaped 2D light, inner angle defaulted from the outer.
        static std::unique_ptr<Light2D> createSpot(const glm::vec2& position, const glm::vec2& direction,
                                                   float range, float outerAngle,
                                                   const glm::vec3& color = glm::vec3(1.0f),
                                                   float intensity = 1.0f) noexcept {
            Light2DConfig config;
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

        // Creates a 2D light with parallel rays and no position.
        static std::unique_ptr<Light2D> createDirectional(const glm::vec2& direction,
                                                          const glm::vec3& color = glm::vec3(1.0f),
                                                          float intensity = 1.0f) noexcept {
            Light2DConfig config;
            config.type = LightType::Directional;
            config.direction = direction;
            config.color = color;
            config.intensity = intensity;
            return create(config);
        }

        // This light's geometric type.
        LightType type() const noexcept override {return _config.type;}
        // Changes the type in place; Area falls back to Point with a warning.
        void setType(LightType type) noexcept {_config.type = supportedType(type);}
        // 2D lights belong to a LightingPipeline2D.
        int dimensions() const noexcept override {return 2;}
        // The shared configuration this light stores.
        LightConfig& config() noexcept override {return _config;}
        // The shared configuration this light stores.
        const LightConfig& config() const noexcept override {return _config;}

        // The light's world position.
        glm::vec2 position() const noexcept {return _config.position;}
        // Sets the light's world position.
        void setPosition(const glm::vec2& position) noexcept {_config.position = position;}

        // The normalised aim direction, falling back to -Y when unset.
        glm::vec2 direction() const noexcept {
            const float length = glm::length(_config.direction);
            return (length > 1e-6f) ? _config.direction / length : glm::vec2(0.0f, -1.0f);
        }
        // Sets the aim direction; normalisation happens on read.
        void setDirection(const glm::vec2& direction) noexcept {_config.direction = direction;}

        // The light's reach in world units.
        float range() const noexcept {return _config.range;}
        // Sets the light's reach, clamped to zero or above.
        void setRange(float range) noexcept {_config.range = std::max(range, 0.0f);}

        // The falloff exponent; above 1 tightens the pool of light.
        float falloff() const noexcept {return _config.falloff;}
        // Sets the falloff exponent, clamped away from zero.
        void setFalloff(float falloff) noexcept {_config.falloff = std::max(falloff, 0.01f);}

        // Height of a point or spot light above the sprite plane, in world units.
        float height() const noexcept {return _config.height;}
        // Sets the height above the sprite plane; lower lights rake across normal maps.
        void setHeight(float height) noexcept {_config.height = std::max(height, 0.0f);}

        // Elevation of a directional light in degrees, 90 being straight down.
        float elevation() const noexcept {return _config.elevation;}
        // Sets the directional elevation, clamped to 0..90 degrees.
        void setElevation(float degrees) noexcept {_config.elevation = glm::clamp(degrees, 0.0f, 90.0f);}

        // Sets the spot half-angles in degrees, keeping inner within outer.
        void setAngles(float innerDegrees, float outerDegrees) noexcept {
            _config.outerAngle = glm::clamp(outerDegrees, 0.0f, 179.0f);
            _config.innerAngle = glm::clamp(innerDegrees, 0.0f, _config.outerAngle);
        }
        // The inner half-angle in degrees.
        float innerAngle() const noexcept {return _config.innerAngle;}
        // The outer half-angle in degrees.
        float outerAngle() const noexcept {return _config.outerAngle;}

        // World-space reach as (minX, minY, maxX, maxY), matching Camera2D::getCulledBounds.
        glm::vec4 bounds() const noexcept {
            return glm::vec4(_config.position - glm::vec2(_config.range),
                             _config.position + glm::vec2(_config.range));
        }

    private:
        explicit Light2D(const Light2DConfig& config) noexcept : _config(config) {
            _config.type = supportedType(config.type);
        }

        // Maps Area, which has no 2D form, to Point.
        static LightType supportedType(LightType type) noexcept {
            if (type != LightType::Area) return type;
            log_warn("Light2D: area lights are not supported in 2D; using Point");
            return LightType::Point;
        }

        Light2DConfig _config;
};

// Flat ambient for the 2D path; the surface normal is ignored.
class EnvironmentLight2D final : public EnvironmentLight {
    public:
        // Creates a 2D environment light with a neutral dim ambient.
        static std::shared_ptr<EnvironmentLight2D> create() noexcept {
            return std::shared_ptr<EnvironmentLight2D>(new EnvironmentLight2D());
        }

        // Creates a 2D environment light from a colour and intensity.
        static std::shared_ptr<EnvironmentLight2D> createFlat(const glm::vec3& color,
                                                              float intensity = 1.0f) noexcept {
            auto environment = create();
            environment->setColor(color);
            environment->setIntensity(intensity);
            return environment;
        }

        // This environment belongs to a LightingPipeline2D.
        int dimensions() const noexcept override {return 2;}
        // Variant cache key for the flat ambient.
        std::string name() const noexcept override {return "EnvironmentLight2D";}

        // The single ambient colour uniform.
        std::string glslUniforms() const noexcept override {
            return shader_tools::glsl(R"GLSL(
                uniform vec3 uAmbient;
            )GLSL");
        }

        // The flat form of ambientLight().
        std::string glslFunction() const noexcept override {
            return shader_tools::glsl(R"GLSL(

                vec3 ambientLight(vec3 n) {return uAmbient;}
            )GLSL");
        }

        // Writes the ambient colour scaled by intensity.
        void upload(const Shader& shader) const noexcept override {
            shader.setVec3("uAmbient", _color * _intensity);
        }

        // The ambient colour.
        glm::vec3 color() const noexcept {return _color;}
        // Sets the ambient colour.
        void setColor(const glm::vec3& color) noexcept {_color = color;}

        // Multiplies the colour by a blackbody tint; cumulative across calls.
        void setFromKelvin(float kelvin) noexcept {_color *= Light::colorFromKelvin(kelvin);}

        // Ambient contribution, matching ambientLight() in GLSL.
        glm::vec3 evaluate() const noexcept {return _color * _intensity;}

    private:
        EnvironmentLight2D() noexcept = default;

        glm::vec3 _color{0.20f, 0.22f, 0.26f};
};

// Packs one 2D light for the GPU: z of the position is the height, a directional light's
// direction tilts into the plane by its elevation, and params.y is the falloff exponent.
inline PackedLight packLight(const Light2D& light) noexcept {
    PackedLight packed;
    glm::vec3 direction(light.direction(), 0.0f);
    if (light.type() == LightType::Directional) {
        const float elevation = glm::radians(glm::clamp(light.elevation(), 0.0f, 90.0f));
        direction = glm::vec3(light.direction() * std::cos(elevation), -std::sin(elevation));
    }
    packed.positionRange = glm::vec4(light.position(), std::max(light.height(), 0.0f), light.range());
    packed.directionOuter = glm::vec4(direction, std::cos(glm::radians(light.outerAngle())));
    packed.radianceInner = glm::vec4(light.radiance(),
                                     std::cos(glm::radians(light.innerAngle())));
    packed.params = glm::vec4(shader_tools::typeCode(light.type(), false),
                              light.falloff(), 0.0f, 0.0f);
    return packed;
}

// Values for Flat2D: sprite albedo, tinted by arriving radiance.
class Flat2DMaterial final : public Material {
    public:
        // Creates a material carrying Flat2D's default albedo.
        static std::unique_ptr<Flat2DMaterial> create() noexcept {
            return std::unique_ptr<Flat2DMaterial>(new Flat2DMaterial());
        }

        // The model these uniforms belong to.
        std::string modelName() const noexcept override {return "Flat2D";}
        // Writes the sprite albedo.
        void upload(const Shader& shader) const noexcept override {shader.setVec3("uAlbedo", albedo);}

        // An independent copy of this material.
        std::unique_ptr<Material> clone() const noexcept override {
            auto copy = create();
            copy->albedo = albedo;
            return copy;
        }

        glm::vec3 albedo{1.0f};

    private:
        Flat2DMaterial() noexcept = default;
};

// Values for Unlit2D: a single emissive colour.
class Unlit2DMaterial final : public Material {
    public:
        // Creates a material carrying Unlit2D's default colour.
        static std::unique_ptr<Unlit2DMaterial> create() noexcept {
            return std::unique_ptr<Unlit2DMaterial>(new Unlit2DMaterial());
        }

        // The model these uniforms belong to.
        std::string modelName() const noexcept override {return "Unlit2D";}
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
        Unlit2DMaterial() noexcept = default;
};

// Base for models the 2D pipeline accepts.
class LightingModel2D : public LightingModel {
    public:
        // Fixed at 2.
        int dimensions() const noexcept final {return 2;}
    protected:
        LightingModel2D() noexcept = default;
};

// Sprite lighting: arriving radiance tints the albedo. Normal-mapped surfaces are shaded
// relative to a flat one, so their flat areas match unmapped sprites exactly and the
// relief only adds light or shade (up to 3x). The 2D default.
class Flat2D final : public LightingModel2D {
    public:
        // Creates the flat 2D shading model.
        static std::shared_ptr<Flat2D> create() noexcept {
            return std::shared_ptr<Flat2D>(new Flat2D());
        }

        // Variant cache key for this model.
        std::string name() const noexcept override {return "Flat2D";}

        // One albedo colour.
        std::string glslSurface() const noexcept override {
            return shader_tools::glsl(R"GLSL(

                struct Surface {vec3 albedo;};
            )GLSL");
        }

        // The uniform Flat2DMaterial writes.
        std::string glslMaterialUniforms() const noexcept override {
            return shader_tools::glsl(R"GLSL(
                uniform vec3 uAlbedo;
            )GLSL");
        }

        // Fills Surface from that uniform.
        std::string glslMaterial() const noexcept override {
            return shader_tools::glsl(R"GLSL(

                Surface makeSurface() {Surface s; s.albedo = uAlbedo; return s;}
            )GLSL");
        }

        // Radiance and ambient both tint the albedo.
        std::string glslShading() const noexcept override {
            return shader_tools::glsl(R"GLSL(

                float relief(vec3 n, vec3 l) {
                    float ratio = max(dot(n, l), 0.0) / max(l.z, 1e-3);
                    return mix(1.0, min(ratio, 3.0), smoothstep(0.0, 0.05, l.z));
                }

                vec3 brdf(Surface s, LightSample light, vec3 n, vec3 v) {
                    return light.radiance * s.albedo * relief(n, light.direction);
                }

                vec3 ambientTerm(Surface s, vec3 worldPos, vec3 n, vec3 v) {
                    return s.albedo * ambientLight(n);
                }
            )GLSL");
        }

        // A material carrying this model's default values.
        std::unique_ptr<Material> createMaterial() const noexcept override {return Flat2DMaterial::create();}

    private:
        Flat2D() noexcept = default;
};

// Emissive 2D surfaces, ignoring every light and the ambient term.
class Unlit2D final : public LightingModel2D {
    public:
        // Creates the unlit 2D shading model.
        static std::shared_ptr<Unlit2D> create() noexcept {
            return std::shared_ptr<Unlit2D>(new Unlit2D());
        }

        // Variant cache key for this model.
        std::string name() const noexcept override {return "Unlit2D";}

        // One emissive colour.
        std::string glslSurface() const noexcept override {
            return shader_tools::glsl(R"GLSL(

                struct Surface {vec3 color;};
            )GLSL");
        }

        // The uniform Unlit2DMaterial writes.
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
        std::unique_ptr<Material> createMaterial() const noexcept override {return Unlit2DMaterial::create();}

        // Ignores lights and ambient.
        bool isLit() const noexcept override {return false;}

    private:
        Unlit2D() noexcept = default;
};

// The 2D lighting path: orthographic, Camera2D world units, exponent-shaped linear
// falloff, no area lights and no shadows.
class LightingPipeline2D final : public LightingPipeline {
    public:
        // Creates a 2D pipeline using Flat2D and a flat ambient.
        static std::unique_ptr<LightingPipeline2D> create() noexcept {
            return std::unique_ptr<LightingPipeline2D>(new LightingPipeline2D());
        }

        // This pipeline consumes 2D lights and models.
        int dimensions() const noexcept override {return 2;}

        // Adds a light; false once the set is full. Disabled lights are skipped and return true.
        bool add(const Light2D& light) noexcept {
            if (!light.enabled()) return true;
            if (static_cast<int>(_packed.size()) >= shader_tools::kMaxLights) return false;
            _packed.push_back(packLight(light));
            return true;
        }

        // Adds the light only if it reaches the (minX, minY, maxX, maxY) rectangle;
        // directional lights always apply.
        bool addIfRelevant(const Light2D& light, const glm::vec4& bounds) noexcept {
            if (light.type() != LightType::Directional) {
                const glm::vec4 reach = light.bounds();
                if (reach.z < bounds.x || reach.x > bounds.z ||
                    reach.w < bounds.y || reach.y > bounds.w) return true;
            }
            return add(light);
        }

        // As above, taking Camera2D::getCulledBounds() directly.
        bool addIfRelevant(const Light2D& light, const std::array<int, 4>& bounds) noexcept {
            return addIfRelevant(light, glm::vec4(static_cast<float>(bounds[0]), static_cast<float>(bounds[1]),
                                                  static_cast<float>(bounds[2]), static_cast<float>(bounds[3])));
        }

        // The active shading model.
        const LightingModel2D& model() const noexcept {
            return static_cast<const LightingModel2D&>(modelBase());
        }
        // The lit sprite program for Renderer2D::drawQuads, with this pipeline's model,
        // ambient and lights. Upload into it with upload(true) or upload(*spriteShader()).
        const Shader* spriteShader() const noexcept {return shader(true);}

        // Replaces the shading model; null restores Flat2D.
        void setModel(std::shared_ptr<LightingModel2D> model) noexcept {
            _model = model ? std::static_pointer_cast<LightingModel>(std::move(model))
                           : std::static_pointer_cast<LightingModel>(Flat2D::create());
        }

        // The active environment light.
        const EnvironmentLight2D& environment() const noexcept {
            return static_cast<const EnvironmentLight2D&>(environmentBase());
        }
        // Mutable access to the active environment light.
        EnvironmentLight2D& environment() noexcept {
            return static_cast<EnvironmentLight2D&>(*_environment);
        }
        // Replaces the environment light; null restores the default flat ambient.
        void setEnvironment(std::shared_ptr<EnvironmentLight2D> environment) noexcept {
            _environment = environment
                ? std::static_pointer_cast<EnvironmentLight>(std::move(environment))
                : std::static_pointer_cast<EnvironmentLight>(EnvironmentLight2D::create());
        }

    protected:
        // Linear reach shaped by the falloff exponent, plus spot cone attenuation.
        std::string glslLightSampling() const noexcept override {
            return shader_tools::glsl(R"GLSL(

                struct LightSample {
                    vec3 direction;
                    vec3 radiance;
                };

                float rangeFalloff2D(float dist, float range, float power) {
                    float t = clamp(1.0 - dist / max(range, 1e-4), 0.0, 1.0);
                    return pow(t, max(power, 0.01));
                }

                float coneFalloff2D(vec2 toSurface, vec2 spotDir, float cosOuter, float cosInner) {
                    float cd = dot(toSurface, spotDir);
                    float t = clamp((cd - cosOuter) / max(cosInner - cosOuter, 1e-4), 0.0, 1.0);
                    return t * t;
                }

                LightSample sampleLight(int index, vec3 worldPos, vec3 n) {
                    int base = index * kVec4sPerLight;
                    vec4 positionRange = uLights[base];
                    vec4 directionOuter = uLights[base + 1];
                    vec4 radianceInner = uLights[base + 2];
                    vec4 params = uLights[base + 3];

                    float kind = params.x;
                    LightSample result;
                    float attenuation = 1.0;

                    if (kind < 0.5) {
                        result.direction = normalize(-directionOuter.xyz);
                    } else {
                        vec2 offset = positionRange.xy - worldPos.xy;
                        float dist = max(length(offset), 1e-4);
                        result.direction = normalize(vec3(offset, positionRange.z));
                        attenuation = rangeFalloff2D(dist, positionRange.w, params.y);
                        if (kind > 1.5 && kind < 2.5) {
                            attenuation *= coneFalloff2D(-offset / dist, directionOuter.xy,
                                                         directionOuter.w, radianceInner.w);
                        }
                    }

                    result.radiance = radianceInner.rgb * max(attenuation, 0.0);
                    return result;
                }
            )GLSL");
        }

        // World position and UV from a 2D model transform, or for the sprite variant the
        // Renderer2D quad layout: world-space position, UV and normalised RGBA8 tint.
        std::string buildVertexSource(bool sprite) const noexcept override {
            if (sprite) {
                return sourceHeader() + shader_tools::glsl(R"GLSL(

                    layout(location = 0) in vec2 aPosition;
                    layout(location = 1) in vec2 aUV;
                    layout(location = 2) in vec4 aTint;

                    uniform mat4 uModel;
                    uniform mat4 uViewProjection;

                    out vec3 vWorldPos;
                    out vec2 vUV;
                    out vec4 vTint;

                    void main() {
                        vec4 world = uModel * vec4(aPosition, 0.0, 1.0);
                        vWorldPos = vec3(world.xy, 0.0);
                        vUV = aUV;
                        vTint = aTint;
                        gl_Position = uViewProjection * world;
                    }
                )GLSL");
            }
            return sourceHeader() + shader_tools::glsl(R"GLSL(

                layout(location = 0) in vec2 aPosition;
                layout(location = 1) in vec2 aUV;

                uniform mat4 uModel;
                uniform mat4 uViewProjection;

                out vec3 vWorldPos;
                out vec2 vUV;

                void main() {
                    vec4 world = uModel * vec4(aPosition, 0.0, 1.0);
                    vWorldPos = vec3(world.xy, 0.0);
                    vUV = aUV;
                    gl_Position = uViewProjection * world;
                }
            )GLSL");
        }

        // Shades with a fixed +Z normal and view vector, then tone maps. The sprite variant
        // multiplies the lit result by texture and tint, which equals lighting a textured
        // albedo for any model linear in albedo. Output is premultiplied, matching textures
        // loaded with premultiplyAlpha: blend with (GL_ONE, GL_ONE_MINUS_SRC_ALPHA). When uUseNormalMap
        // is set it takes the normal from uNormalMap (green up; define NORMAL_MAP_GREEN_DOWN
        // for DirectX-style maps), following mirrored or flipped UVs.
        std::string glslFragmentMain(bool sprite) const noexcept override {
            if (sprite) {
                return shader_tools::glsl(R"GLSL(

                    in vec3 vWorldPos;
                    in vec2 vUV;
                    in vec4 vTint;

                    uniform sampler2D uTexture;
                    uniform sampler2D uNormalMap;
                    uniform int uUseNormalMap;

                    out vec4 FragColor;

                    vec3 spriteNormal() {
                        if (uUseNormalMap == 0) return vec3(0.0, 0.0, 1.0);
                        vec3 m = texture(uNormalMap, vUV).xyz * 2.0 - 1.0;
                    #ifdef NORMAL_MAP_GREEN_DOWN
                        m.y = -m.y;
                    #endif
                        m.x *= dFdx(vUV.x) < 0.0 ? -1.0 : 1.0;
                        m.y *= dFdy(vUV.y) > 0.0 ? -1.0 : 1.0;
                        return dot(m, m) > 1e-8 ? normalize(m) : vec3(0.0, 0.0, 1.0);
                    }

                    void main() {
                        vec4 texel = texture(uTexture, vUV) * vec4(vTint.rgb * vTint.a, vTint.a);
                        if (texel.a < 0.001) discard;
                        vec3 n = spriteNormal();
                        vec3 lit = shadeSurface(makeSurface(), vWorldPos, n, vec3(0.0, 0.0, 1.0)) * texel.rgb;
                        FragColor = vec4(applyToneMap(lit), texel.a);
                    }
                )GLSL");
            }
            return shader_tools::glsl(R"GLSL(

                in vec3 vWorldPos;
                in vec2 vUV;

                out vec4 FragColor;

                void main() {
                    vec3 n = vec3(0.0, 0.0, 1.0);
                    FragColor = vec4(applyToneMap(shadeSurface(makeSurface(), vWorldPos, n, n)), 1.0);
                }
            )GLSL");
        }

    private:
        LightingPipeline2D() noexcept {
            _model = Flat2D::create();
            _environment = EnvironmentLight2D::create();
        }
};