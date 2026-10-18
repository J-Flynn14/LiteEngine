#pragma once
#include "../../core/engine_config.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <glm/glm.hpp>

#include "../../core/logs.h"
#include "../../core/shader.h"

// Dimension-agnostic half of the lighting system, shared by light_2D.h and light_3D.h.
// GLSL assembly allocates inside noexcept functions; the engine treats OOM as fatal.

// Geometric light type. Area is a rectangle or disc emitter and exists only in 3D.
enum class LightType {Directional, Point, Spot, Area};

// Evaluation mode. Only Realtime is evaluated; Mixed and Baked are reserved.
enum class LightMode {Realtime, Mixed, Baked};

// Shadow filtering: none, a single depth comparison, or 3x3 PCF.
enum class ShadowType {None, Hard, Soft};

// Per-light shadow tuning, read by LightingPipeline3D for directional and spot
// lights. Biases are in shadow-map depth and world units; resolution is reserved.
struct ShadowSettings {
    ShadowType type = ShadowType::None;
    float strength = 1.0f;
    float depthBias = 0.0015f;
    float normalBias = 0.05f;
    float nearPlane = 1.0f;
    int resolution = 1024;
};

// Settings common to every light. indirectMultiplier is reserved for baked lighting.
struct LightConfig {
    glm::vec3 color{1.0f};
    float intensity = 1.0f;
    float indirectMultiplier = 1.0f;
    LightMode mode = LightMode::Realtime;
    ShadowSettings shadows;
    unsigned int cullingMask = 0xFFFFFFFFu;
    bool enabled = true;
};

// Shared state and behaviour for every light; subclasses own the config.
class Light {
    public:
        virtual ~Light() noexcept = default;

        // This light's geometric type.
        virtual LightType type() const noexcept = 0;
        // 2 or 3, matching the pipeline that can consume this light.
        virtual int dimensions() const noexcept = 0;
        // The shared configuration this light stores.
        virtual LightConfig& config() noexcept = 0;
        // The shared configuration this light stores.
        virtual const LightConfig& config() const noexcept = 0;

        // Colour premultiplied by intensity.
        glm::vec3 radiance() const noexcept {return config().color * config().intensity;}

        // The light's colour, before intensity.
        glm::vec3 color() const noexcept {return config().color;}
        // Sets the light's colour.
        void setColor(const glm::vec3& color) noexcept {config().color = color;}

        // The light's intensity multiplier.
        float intensity() const noexcept {return config().intensity;}
        // Sets the intensity, clamped to zero or above.
        void setIntensity(float intensity) noexcept {config().intensity = std::max(intensity, 0.0f);}

        // True when the light contributes to shading.
        bool enabled() const noexcept {return config().enabled;}
        // Enables or disables the light.
        void setEnabled(bool enabled) noexcept {config().enabled = enabled;}

        // The light's evaluation mode.
        LightMode mode() const noexcept {return config().mode;}
        // Sets the light's evaluation mode.
        void setMode(LightMode mode) noexcept {config().mode = mode;}

        // The light's shadow settings.
        const ShadowSettings& shadows() const noexcept {return config().shadows;}
        // Replaces the light's shadow settings.
        void setShadows(const ShadowSettings& shadows) noexcept {config().shadows = shadows;}
        // True when the light casts shadows.
        bool castsShadows() const noexcept {return config().shadows.type != ShadowType::None;}

        // The layer mask this light illuminates.
        unsigned int cullingMask() const noexcept {return config().cullingMask;}
        // Sets the layer mask this light illuminates.
        void setCullingMask(unsigned int mask) noexcept {config().cullingMask = mask;}
        // True when the light affects the given layer bits.
        bool affects(unsigned int layer) const noexcept {return (config().cullingMask & layer) != 0;}

        // Blackbody colour for a temperature in Kelvin, normalised to peak at 1.
        static glm::vec3 colorFromKelvin(float kelvin) noexcept {
            const float t = glm::clamp(kelvin, 1000.0f, 40000.0f) / 100.0f;
            glm::vec3 c(1.0f);
            if (t <= 66.0f) {
                c.g = glm::clamp(0.39008158f * std::log(t) - 0.63184144f, 0.0f, 1.0f);
                c.b = (t <= 19.0f) ? 0.0f
                    : glm::clamp(0.54320679f * std::log(t - 10.0f) - 1.19625408f, 0.0f, 1.0f);
            } else {
                c.r = glm::clamp(1.29293619f * std::pow(t - 60.0f, -0.1332047f), 0.0f, 1.0f);
                c.g = glm::clamp(1.12989086f * std::pow(t - 60.0f, -0.0755148f), 0.0f, 1.0f);
            }
            return c;
        }

        Light(const Light&) = delete;
        Light& operator=(const Light&) = delete;

    protected:
        Light() noexcept = default;
};

// Ambient stand-in for bounced light. Subclasses emit GLSL `vec3 ambientLight(vec3 n)`.
class EnvironmentLight {
    public:
        virtual ~EnvironmentLight() noexcept = default;

        // 2 or 3, matching the pipeline that can consume this environment.
        virtual int dimensions() const noexcept = 0;
        // Stable identifier for the shader variant key; distinct GLSL needs a distinct name.
        virtual std::string name() const noexcept = 0;
        // Uniform declarations ambientLight() reads.
        virtual std::string glslUniforms() const noexcept = 0;
        // Must define exactly `vec3 ambientLight(vec3 n)`.
        virtual std::string glslFunction() const noexcept = 0;
        // Pushes the current values into the declared uniforms.
        virtual void upload(const Shader& shader) const noexcept = 0;

        // Overall ambient multiplier.
        float intensity() const noexcept {return _intensity;}
        // Sets the ambient multiplier, clamped to zero or above.
        void setIntensity(float intensity) noexcept {_intensity = std::max(intensity, 0.0f);}

        EnvironmentLight(const EnvironmentLight&) = delete;
        EnvironmentLight& operator=(const EnvironmentLight&) = delete;

    protected:
        EnvironmentLight() noexcept = default;
        float _intensity = 1.0f;
};

// What a geometry submission needs to know about the pass it feeds: the bound program
// (null means bind your own), the program for instanced geometry, the camera, culling
// volume and layers, whether only shadow casters belong, and for the lit program the
// eye position to write to uViewPosition and the plane to write to uClipPlane.
struct SceneContext {
    const Shader* shader = nullptr;
    const Shader* instancedShader = nullptr;
    glm::mat4 viewProjection{1.0f};
    glm::vec3 viewPosition{0.0f};
    glm::vec4 clipPlane{0.0f};
    bool materialsRequired = true;
    bool shadowCasters = false;
    glm::vec3 cullCentre{0.0f};
    float cullRadius = 0.0f;
    unsigned int layerMask = 0xFFFFFFFFu;

    // True when a renderable at this position and radius is worth submitting.
    bool isVisible(const glm::vec3& centre, float radius,
                   unsigned int layers = 0xFFFFFFFFu) const noexcept {
        if ((layerMask & layers) == 0) return false;
        if (cullRadius <= 0.0f) return true;
        return glm::length(centre - cullCentre) <= cullRadius + radius;
    }
};

// Submits scene geometry for one pass: a shadow slice, a planar capture, or the lit pass.
using ScenePass = std::function<void(const SceneContext&)>;

namespace shader_tools {

    inline constexpr int kMaxLights = 8;
    inline constexpr int kVec4sPerLight = 5;
    inline constexpr int kMaxShadows = 4;

    // Shader-side type code; area lights split into rectangle (3) and disc (4).
    inline float typeCode(LightType type, bool areaIsDisc) noexcept {
        switch (type) {
            case LightType::Directional: return 0.0f;
            case LightType::Point:       return 1.0f;
            case LightType::Spot:        return 2.0f;
            default:                     return areaIsDisc ? 4.0f : 3.0f;
        }
    }

    // FNV-1a over a string, for the shader variant cache key.
    inline uint64_t hashString(const std::string& text) noexcept {
        uint64_t hash = 1469598103934665603ull;
        for (const char c : text) {
            hash ^= static_cast<uint64_t>(static_cast<unsigned char>(c));
            hash *= 1099511628211ull;
        }
        return hash;
    }

    // The name of element `index` of the uLights array, built once and reused.
    inline const std::string& lightUniformName(int index) noexcept {
        static const std::array<std::string, kMaxLights * kVec4sPerLight> names = [] {
            std::array<std::string, kMaxLights * kVec4sPerLight> result;
            for (size_t i = 0; i < result.size(); ++i) {
                result[i] = "uLights[" + std::to_string(i) + "]";
            }
            return result;
        }();
        return names[static_cast<size_t>(index)];
    }
}

// One light packed for the GPU as five vec4s, shared by both dimensions:
// position/range, direction/cos outer, radiance/cos inner, params (type, area
// size or 2D falloff, soft flag) and shadow (slice or -1, depth bias, normal bias, strength).
struct PackedLight {
    glm::vec4 positionRange{0.0f};
    glm::vec4 directionOuter{0.0f};
    glm::vec4 radianceInner{0.0f};
    glm::vec4 params{0.0f};
    glm::vec4 shadow{-1.0f, 0.0f, 0.0f, 1.0f};
};

// The CPU half of a LightingModel: concrete values for the uniforms its makeSurface() reads.
// Obtain one from LightingModel::createMaterial() rather than naming the type.
class Material {
    public:
        virtual ~Material() noexcept = default;

        // Name of the LightingModel these uniforms belong to; compare to catch mismatches.
        virtual std::string modelName() const noexcept = 0;
        // Writes this material's values into the shader's material uniforms.
        virtual void upload(const Shader& shader) const noexcept = 0;
        // An independent copy, for per-object materials sharing a template.
        virtual std::unique_ptr<Material> clone() const noexcept = 0;

        Material(const Material&) = delete;
        Material& operator=(const Material&) = delete;

    protected:
        Material() noexcept = default;
};

// A shading model: decides how arriving light interacts with a surface.
// Contributes, in order, `struct Surface`, its material uniforms, `Surface makeSurface()`,
// and `brdf(...)` plus `ambientTerm(...)`; may rely on LightSample, ambientLight and reflectionSample.
class LightingModel {
    public:
        virtual ~LightingModel() noexcept = default;

        // 2 or 3, matching the pipeline that can consume this model.
        virtual int dimensions() const noexcept = 0;
        // Stable identifier for the shader variant key; distinct GLSL needs a distinct name.
        virtual std::string name() const noexcept = 0;
        // Must define exactly `struct Surface`.
        virtual std::string glslSurface() const noexcept = 0;
        // Uniform declarations makeSurface() reads.
        virtual std::string glslMaterialUniforms() const noexcept = 0;
        // Must define exactly `Surface makeSurface()`.
        virtual std::string glslMaterial() const noexcept = 0;
        // Must define `vec3 brdf(Surface, LightSample, vec3 n, vec3 v)` and
        // `vec3 ambientTerm(Surface, vec3 worldPos, vec3 n, vec3 v)`.
        virtual std::string glslShading() const noexcept = 0;
        // A material carrying this model's default values.
        virtual std::unique_ptr<Material> createMaterial() const noexcept = 0;

        // False for models that ignore lights and ambient, so the pipeline skips uploading them.
        virtual bool isLit() const noexcept {return true;}

        // Pushes default material values into a freshly built shader.
        void uploadDefaults(const Shader& shader) const noexcept {createMaterial()->upload(shader);}

        LightingModel(const LightingModel&) = delete;
        LightingModel& operator=(const LightingModel&) = delete;

    protected:
        LightingModel() noexcept = default;
};

// Output transform at the end of the generated fragment shader. None passes values
// through untouched; Reinhard and ACES compress HDR and then gamma-encode unless
// setGammaEncode(false) is used for an sRGB framebuffer.
enum class ToneMap {None, Reinhard, ACES};

// Collects the lights for a draw, uploads them, and owns the generated shader
// variants. Subclasses supply the per-dimension GLSL; setShader() bypasses generation.
class LightingPipeline {
    public:
        virtual ~LightingPipeline() noexcept = default;

        // 2 or 3.
        virtual int dimensions() const noexcept = 0;

        // Empties the light set, ready for the next draw.
        virtual void clear() noexcept {_packed.clear();}

        // Number of lights currently packed.
        size_t size() const noexcept {return _packed.size();}

        // The packed lights, in submission order.
        const std::vector<PackedLight>& lights() const noexcept {return _packed;}

        // The most lights one draw can carry.
        static constexpr int maxLights() noexcept {return shader_tools::kMaxLights;}

        // Replaces the generated shader. Null restores the generated default.
        void setShader(std::shared_ptr<Shader> shader) noexcept {_override = std::move(shader);}

        // True when shader() returns generated source rather than an override.
        bool usingDefaultShader() const noexcept {return _override == nullptr;}

        // The override if set, otherwise the cached generated variant, compiling it on first
        // use. `instanced` picks the dimension's second variant: instanced meshes in 3D,
        // sprite quads in 2D. Null if compilation failed; remembered until clearShaderCache().
        const Shader* shader(bool instanced = false) const noexcept {
            if (_override) return _override.get();

            const uint64_t key = variantKey(instanced);
            const auto found = _variants.find(key);
            if (found != _variants.end()) return found->second.get();

            std::shared_ptr<Shader> built = buildShader(instanced);
            if (!built) {
                log_error("LightingPipeline: failed to build the default shader for model '",
                          modelBase().name(), "'");
                _variants.emplace(key, nullptr);
                return nullptr;
            }
            const Shader* raw = built.get();
            _variants.emplace(key, std::move(built));
            return raw;
        }

        // Drops every cached variant. Call after changing the GL context.
        void clearShaderCache() noexcept {_variants.clear();}

        // Preprocessor defines baked into every generated shader; part of the variant key.
        const std::vector<std::string>& defines() const noexcept {return _defines;}
        // Replaces the define list.
        void setDefines(std::vector<std::string> defines) noexcept {_defines = std::move(defines);}
        // Appends one define, ignoring duplicates.
        void addDefine(const std::string& define) noexcept {
            if (std::find(_defines.begin(), _defines.end(), define) == _defines.end()) {
                _defines.push_back(define);
            }
        }

        // The "#version" line generated shaders open with.
        const std::string& versionDirective() const noexcept {return _version;}
        // Replaces the "#version" line for generated shaders.
        void setVersionDirective(std::string directive) noexcept {_version = std::move(directive);}

        // The output transform generated shaders apply; part of the variant key.
        ToneMap toneMap() const noexcept {return _toneMap;}
        // Sets the output transform. Leave it on for MetallicRoughness3D.
        void setToneMap(ToneMap toneMap) noexcept {_toneMap = toneMap;}

        // True when Reinhard and ACES gamma-encode their output; part of the variant key.
        bool gammaEncode() const noexcept {return _gammaEncode;}
        // Disable when rendering into an sRGB framebuffer that encodes for you.
        void setGammaEncode(bool encode) noexcept {_gammaEncode = encode;}

        // The full generated fragment source, optionally the second variant.
        std::string fragmentSource(bool instanced = false) const noexcept {return buildFragmentSource(instanced);}
        // The full generated vertex source, optionally the second variant.
        std::string vertexSource(bool instanced = false) const noexcept {return buildVertexSource(instanced);}

        // Uploads the light set, ambient and per-dimension state into the given shader.
        // Skipped for an unlit model on the generated shader, which declares none of it.
        void upload(const Shader& shader) const noexcept {
            if (usingDefaultShader() && !modelBase().isLit()) return;

            shader.setInt("uLightCount", static_cast<int>(_packed.size()));
            for (size_t i = 0; i < _packed.size(); ++i) {
                const int base = static_cast<int>(i) * shader_tools::kVec4sPerLight;
                shader.setVec4(shader_tools::lightUniformName(base), _packed[i].positionRange);
                shader.setVec4(shader_tools::lightUniformName(base + 1), _packed[i].directionOuter);
                shader.setVec4(shader_tools::lightUniformName(base + 2), _packed[i].radianceInner);
                shader.setVec4(shader_tools::lightUniformName(base + 3), _packed[i].params);
                shader.setVec4(shader_tools::lightUniformName(base + 4), _packed[i].shadow);
            }
            environmentBase().upload(shader);
            uploadDimensionState(shader);
        }

        // Uploads into the current shader variant; false when no usable shader exists.
        bool upload(bool instanced = false) const noexcept {
            const Shader* target = shader(instanced);
            if (!target) return false;
            upload(*target);
            return true;
        }

        LightingPipeline(const LightingPipeline&) = delete;
        LightingPipeline& operator=(const LightingPipeline&) = delete;

    protected:
        LightingPipeline() noexcept : _version(Platform::get_glsl_version()) {}

        // Must declare `struct LightSample {vec3 direction; vec3 radiance;}` and
        // `LightSample sampleLight(int index, vec3 worldPos, vec3 n)`.
        virtual std::string glslLightSampling() const noexcept = 0;

        // The full vertex shader for this dimension, opening with sourceHeader().
        virtual std::string buildVertexSource(bool instanced) const noexcept = 0;

        // The fragment main() for this dimension and variant.
        virtual std::string glslFragmentMain(bool instanced) const noexcept = 0;

        // Extra per-dimension uniforms, called at the end of upload().
        virtual void uploadDimensionState(const Shader&) const noexcept {}

        // GLSL declaring reflectionSample(); the default is a stub returning black.
        virtual std::string glslReflectionBlock() const noexcept {
            return shader_tools::glsl(R"GLSL(

                vec3 reflectionSample(vec3 worldPos, vec3 n, vec3 v, float roughness) {return vec3(0.0);}
            )GLSL");
        }

        // Extra text folded into the shader variant key by subclasses.
        virtual std::string variantSuffix() const noexcept {return std::string();}

        // The "#version" line followed by the configured defines, for every generated stage.
        std::string sourceHeader() const noexcept {
            std::string text = _version + "\n";
            for (const std::string& define : _defines) {text += "#define " + define + "\n";}
            return text;
        }

        // The model, as the shared base.
        const LightingModel& modelBase() const noexcept {return *_model;}
        // The environment, as the shared base.
        const EnvironmentLight& environmentBase() const noexcept {return *_environment;}

        std::vector<PackedLight> _packed;
        std::shared_ptr<LightingModel> _model;
        std::shared_ptr<EnvironmentLight> _environment;

    private:
        // Uniform declarations shared by every dimension and model.
        std::string glslLightUniforms() const noexcept {
            return "const int kMaxLights = " + std::to_string(shader_tools::kMaxLights) + ";\n"
                   "const int kVec4sPerLight = " + std::to_string(shader_tools::kVec4sPerLight) + ";\n"
                   "uniform vec4 uLights[kMaxLights * kVec4sPerLight];\n"
                   "uniform int uLightCount;\n";
        }

        // The loop tying light sampling to the model's brdf, identical in every dimension.
        static std::string glslGlue() noexcept {
            return shader_tools::glsl(R"GLSL(

                vec3 shadeSurface(Surface s, vec3 worldPos, vec3 n, vec3 v) {
                    vec3 total = ambientTerm(s, worldPos, n, v);
                    for (int i = 0; i < uLightCount; ++i) {
                        LightSample light = sampleLight(i, worldPos, n);
                        if (dot(light.radiance, light.radiance) <= 0.0) continue;
                        total += brdf(s, light, n, v);
                    }
                    return total;
                }
            )GLSL");
        }

        // Defines `vec3 applyToneMap(vec3)`, called by every fragment main().
        std::string glslToneMap() const noexcept {
            const std::string encode = _gammaEncode
                ? "\nvec3 encodeOutput(vec3 c) {return pow(max(c, vec3(0.0)), vec3(1.0 / 2.2));}\n"
                : "\nvec3 encodeOutput(vec3 c) {return c;}\n";
            switch (_toneMap) {
                case ToneMap::Reinhard:
                    return encode + shader_tools::glsl(R"GLSL(

                        vec3 applyToneMap(vec3 c) {return encodeOutput(c / (c + vec3(1.0)));}
                    )GLSL");
                case ToneMap::ACES:
                    return encode + shader_tools::glsl(R"GLSL(

                        vec3 applyToneMap(vec3 c) {
                            return encodeOutput(clamp((c * (2.51 * c + 0.03)) / (c * (2.43 * c + 0.59) + 0.14), 0.0, 1.0));
                        }
                    )GLSL");
                default:
                    return shader_tools::glsl(R"GLSL(

                        vec3 applyToneMap(vec3 c) {return c;}
                    )GLSL");
            }
        }

        // Concatenates every fragment contribution in dependency order.
        std::string buildFragmentSource(bool instanced) const noexcept {
            return sourceHeader()
                 + glslLightUniforms()
                 + environmentBase().glslUniforms()
                 + modelBase().glslMaterialUniforms()
                 + glslLightSampling()
                 + environmentBase().glslFunction()
                 + glslReflectionBlock()
                 + modelBase().glslSurface()
                 + modelBase().glslMaterial()
                 + modelBase().glslShading()
                 + glslGlue()
                 + glslToneMap()
                 + glslFragmentMain(instanced);
        }

        // Hash of everything that changes the generated source.
        uint64_t variantKey(bool instanced) const noexcept {
            std::string key = modelBase().name() + '|' + environmentBase().name() + '|' + _version
                            + '|' + std::to_string(static_cast<int>(_toneMap))
                            + (_gammaEncode ? "|GAMMA" : "|LINEAR") + '|' + variantSuffix();
            if (instanced) key += "|INSTANCED";
            for (const std::string& define : _defines) {key += '|' + define;}
            return shader_tools::hashString(key);
        }

        // Compiles one variant and seeds it with the model's default material.
        std::shared_ptr<Shader> buildShader(bool instanced) const noexcept {
            ShaderConfig config;
            config.vertex = {ShaderSource::fromText(buildVertexSource(instanced),
                                                    "lighting/" + modelBase().name() + ".vert")};
            config.fragment = {ShaderSource::fromText(buildFragmentSource(instanced),
                                                      "lighting/" + modelBase().name() + ".frag")};
            std::unique_ptr<Shader> shader = Shader::create(config);
            if (!shader) return nullptr;
            modelBase().uploadDefaults(*shader);
            return std::shared_ptr<Shader>(std::move(shader));
        }

        mutable std::unordered_map<uint64_t, std::shared_ptr<Shader>> _variants;
        std::shared_ptr<Shader> _override;
        std::vector<std::string> _defines;
        std::string _version;
        ToneMap _toneMap = ToneMap::None;
        bool _gammaEncode = true;
};