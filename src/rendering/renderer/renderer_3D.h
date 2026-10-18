#pragma once
#include "renderer_core.h"

#include <unordered_map>

#include <glm/gtc/quaternion.hpp>

#include "light_3D.h"

// Position, quaternion rotation and scale for an object in the 3D path.
struct Transform3D {
    glm::vec3 position{0.0f};
    glm::quat rotation{1.0f, 0.0f, 0.0f, 0.0f};
    glm::vec3 scale{1.0f};

    // Model matrix, applied as scale then rotate then translate.
    glm::mat4 matrix() const noexcept {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), position);
        model *= glm::mat4_cast(rotation);
        return glm::scale(model, scale);
    }

    // Matrix for transforming normals: the inverse transpose, shortcut to the rotation
    // (flipped for negative scale) when the scale is uniform.
    glm::mat3 normalMatrix() const noexcept {
        if (isUniformlyScaled()) {
            const glm::mat3 rotationMatrix(glm::mat4_cast(rotation));
            return scale.x < 0.0f ? -rotationMatrix : rotationMatrix;
        }
        return glm::mat3(glm::transpose(glm::inverse(matrix())));
    }

    // True when all three scale axes match, within a small tolerance.
    bool isUniformlyScaled() const noexcept {
        return std::abs(scale.x - scale.y) < 1e-6f && std::abs(scale.y - scale.z) < 1e-6f;
    }

    // The transform's forward axis (-Z) after rotation.
    glm::vec3 forward() const noexcept {return rotation * glm::vec3(0.0f, 0.0f, -1.0f);}
    // The transform's right axis (+X) after rotation.
    glm::vec3 right() const noexcept {return rotation * glm::vec3(1.0f, 0.0f, 0.0f);}
    // The transform's up axis (+Y) after rotation.
    glm::vec3 up() const noexcept {return rotation * glm::vec3(0.0f, 1.0f, 0.0f);}

    // Builds a transform at a position, with an optional uniform scale.
    static Transform3D at(const glm::vec3& position, float uniformScale = 1.0f) noexcept {
        Transform3D transform;
        transform.position = position;
        transform.scale = glm::vec3(uniformScale);
        return transform;
    }

    // Builds a transform rotated by an angle in radians about an axis.
    static Transform3D axisAngle(const glm::vec3& position, const glm::vec3& axis,
                                 float radians, float uniformScale = 1.0f) noexcept {
        Transform3D transform = at(position, uniformScale);
        const float length = glm::length(axis);
        if (length > 1e-6f) {
            transform.rotation = glm::angleAxis(radians, axis / length);
        }
        return transform;
    }

    // Builds a transform whose forward axis points at a target, choosing another up
    // axis when the target lies along worldUp.
    static Transform3D lookingAt(const glm::vec3& position, const glm::vec3& target,
                                 const glm::vec3& worldUp = {0.0f, 1.0f, 0.0f}) noexcept {
        Transform3D transform = at(position);
        const glm::vec3 offset = target - position;
        const float distance = glm::length(offset);
        if (distance > 1e-6f) {
            const glm::vec3 direction = offset / distance;
            glm::vec3 up = worldUp;
            if (glm::length(glm::cross(direction, up)) < 1e-4f) {
                up = (std::abs(direction.y) < 0.99f) ? glm::vec3(0.0f, 1.0f, 0.0f)
                                                     : glm::vec3(0.0f, 0.0f, 1.0f);
            }
            const glm::mat3 basis = glm::transpose(glm::mat3(glm::lookAt(position, target, up)));
            transform.rotation = glm::normalize(glm::quat_cast(basis));
        }
        return transform;
    }
};

// One mesh drawn in the 3D path: deliberately flat, with no hierarchy. The mesh is
// non-owning and must outlive the renderer; boundsRadius is local and centred on the
// origin, 0 never culls; castsShadow false keeps it out of shadow slices only.
struct Renderable {
    const Mesh* mesh = nullptr;
    Transform3D transform;
    std::shared_ptr<Material> material;
    float boundsRadius = 0.0f;
    unsigned int layers = 0xFFFFFFFFu;
    bool castsShadow = true;

    // Centre of the world-space bounding sphere.
    glm::vec3 worldCentre() const noexcept {return transform.position;}

    // Radius of the world-space bounding sphere, scaled by the largest axis.
    float worldRadius() const noexcept {return boundsRadius * renderer_tools::maxScale(transform.scale);}
};

// One 32-byte instance matching the instanced shaders' attributes at locations 3 and 4:
// position with a uniform positive scale, and an xyzw rotation quaternion.
struct MeshInstance {
    glm::vec4 positionScale{0.0f, 0.0f, 0.0f, 1.0f};
    glm::vec4 rotation{0.0f, 0.0f, 0.0f, 1.0f};

    // Builds an instance from a transform, taking the largest absolute scale axis.
    static MeshInstance from(const Transform3D& transform) noexcept {
        MeshInstance instance;
        instance.positionScale = glm::vec4(transform.position, renderer_tools::maxScale(transform.scale));
        instance.rotation = glm::vec4(transform.rotation.x, transform.rotation.y,
                                      transform.rotation.z, transform.rotation.w);
        return instance;
    }
};
static_assert(sizeof(MeshInstance) == 32, "MeshInstance must match the instanced attribute layout");

// Many copies of one mesh drawn with a single instanced call; prefer it to many
// Renderables of the same mesh. boundsRadius is per instance, before its scale.
struct InstancedBatch {
    const Mesh* mesh = nullptr;
    std::shared_ptr<Material> material;
    std::vector<MeshInstance> instances;
    float boundsRadius = 0.0f;
    unsigned int layers = 0xFFFFFFFFu;
    bool castsShadow = true;
};

#if ENGINE_HAS_VERTEX_ARRAY

// Draws a flat list of Renderables and InstancedBatches for one pass. The same lists
// feed the lit pass, every shadow slice and every planar capture; the SceneContext
// decides the program, camera and culling.
class Renderer3D {
    public:
        // Empties both lists, ready for the next frame. Instance buffers are kept.
        void clear() noexcept {
            _items.clear();
            _batches.clear();
        }

        // Releases every cached instance buffer. Call after changing the GL context.
        void releaseInstanceBuffers() noexcept {_gpuBatches.clear();}

        // Releases one mesh's cached instance buffer. Call before destroying a mesh that
        // was drawn instanced, so a new mesh at the same address cannot reuse its bindings.
        void releaseInstanceBuffers(const Mesh& mesh) noexcept {_gpuBatches.erase(&mesh);}

        // Adds a copy of a renderable.
        void submit(const Renderable& item) noexcept {_items.push_back(item);}

        // Adds a renderable built from its parts.
        void submit(const Mesh& mesh, const Transform3D& transform,
                    std::shared_ptr<Material> material = nullptr, float boundsRadius = 0.0f) noexcept {
            Renderable item;
            item.mesh = &mesh;
            item.transform = transform;
            item.material = std::move(material);
            item.boundsRadius = boundsRadius;
            _items.push_back(std::move(item));
        }

        // Adds an instanced batch.
        void submitInstanced(InstancedBatch batch) noexcept {_batches.push_back(std::move(batch));}

        // Renderables currently submitted.
        const std::vector<Renderable>& items() const noexcept {return _items;}
        // Number of renderables currently submitted.
        size_t size() const noexcept {return _items.size();}
        // Instanced batches currently submitted.
        const std::vector<InstancedBatch>& batches() const noexcept {return _batches;}

        // Draws the renderables with context.shader; does nothing when it is null, as in
        // the lit pass and planar captures, which use the overload taking a shader.
        void draw(const SceneContext& context, bool shadowPass = false) const noexcept {
            if (context.shader) draw(*context.shader, context, shadowPass);
        }

        // Draws every renderable the context admits with the given, already bound, shader.
        // Sets uModel, plus uNormalMatrix and the material when the pass requires materials.
        // Shadow passes, flagged by the context or the argument, skip non-casters.
        void draw(const Shader& shader, const SceneContext& context, bool shadowPass = false) const noexcept {
            const bool castersOnly = shadowPass || context.shadowCasters;
            for (const Renderable& item : _items) {
                if (!item.mesh) continue;
                if (castersOnly && !item.castsShadow) continue;
                if (!context.isVisible(item.worldCentre(), item.worldRadius(), item.layers)) continue;

                shader.setMat4("uModel", item.transform.matrix());
                if (context.materialsRequired) {
                    shader.setMat3("uNormalMatrix", item.transform.normalMatrix());
                    if (item.material) item.material->upload(shader);
                }
                item.mesh->draw();
            }
        }

        // Draws the batches with context.instancedShader, binding it for the duration and
        // rebinding context.shader afterwards. Does nothing when it is null.
        void drawInstanced(const SceneContext& context, bool shadowPass = false) const noexcept {
            if (!context.instancedShader || _batches.empty()) return;
            glUseProgram(context.instancedShader->id());
            drawInstanced(*context.instancedShader, context, shadowPass);
            if (context.shader) glUseProgram(context.shader->id());
        }

        // Draws the batches with the given, already bound, instanced shader. Instances are
        // culled on the CPU before upload.
        void drawInstanced(const Shader& shader, const SceneContext& context,
                           bool shadowPass = false) const noexcept {
            const bool castersOnly = shadowPass || context.shadowCasters;
            for (const InstancedBatch& batch : _batches) {
                if (!batch.mesh || batch.instances.empty()) continue;
                if (castersOnly && !batch.castsShadow) continue;
                if ((context.layerMask & batch.layers) == 0) continue;

                _visible.clear();
                for (const MeshInstance& instance : batch.instances) {
                    const glm::vec3 centre(instance.positionScale);
                    const float radius = batch.boundsRadius * instance.positionScale.w;
                    if (context.isVisible(centre, radius, batch.layers)) _visible.push_back(instance);
                }
                if (_visible.empty()) continue;

                GpuBatch* gpu = acquire(*batch.mesh, _visible.size());
                if (!gpu) continue;

                renderer_tools::uploadDynamic(*gpu->buffer, _visible.data(),
                                              _visible.size() * sizeof(MeshInstance));

                if (context.materialsRequired && batch.material) batch.material->upload(shader);

                const int count = static_cast<int>(_visible.size());
                if (gpu->vao->hasIndices()) gpu->vao->drawIndexedInstanced(gpu->topology, count);
                else gpu->vao->drawInstanced(gpu->topology, 0, static_cast<int>(gpu->vertexCount), count);
            }
        }

        // How many renderables the context admits, without drawing.
        size_t visibleCount(const SceneContext& context, bool shadowPass = false) const noexcept {
            const bool castersOnly = shadowPass || context.shadowCasters;
            size_t count = 0;
            for (const Renderable& item : _items) {
                if (!item.mesh || (castersOnly && !item.castsShadow)) continue;
                if (context.isVisible(item.worldCentre(), item.worldRadius(), item.layers)) ++count;
            }
            return count;
        }

    private:
        // A mesh's own buffers plus a per-instance buffer at divisor 1, kept across frames.
        struct GpuBatch {
            std::unique_ptr<Buffer> buffer;
            std::unique_ptr<VertexArray> vao;
            unsigned int topology = GL_TRIANGLES;
            size_t vertexCount = 0;
            size_t capacity = 0;
        };

        // The per-instance attributes at locations 3 and 4.
        static std::vector<VertexAttribute> instanceAttributes() noexcept {
            VertexAttribute positionScale;
            positionScale.location = 3;
            positionScale.components = 4;
            positionScale.type = GL_FLOAT;
            positionScale.offset = offsetof(MeshInstance, positionScale);
            positionScale.divisor = 1;

            VertexAttribute rotation;
            rotation.location = 4;
            rotation.components = 4;
            rotation.type = GL_FLOAT;
            rotation.offset = offsetof(MeshInstance, rotation);
            rotation.divisor = 1;

            return {positionScale, rotation};
        }

        // The cached batch for a mesh, rebuilt larger when this frame needs more room.
        GpuBatch* acquire(const Mesh& mesh, size_t needed) const noexcept {
            auto found = _gpuBatches.find(&mesh);
            if (found != _gpuBatches.end() && found->second.capacity >= needed) {
                return &found->second;
            }

            const size_t capacity = renderer_tools::growCapacity(needed, 64);

            GpuBatch batch;
            batch.buffer = renderer_tools::createDynamicBuffer(capacity * sizeof(MeshInstance));
            if (!batch.buffer) {
                log_error("Renderer3D: could not create an instance buffer for ", needed, " instances");
                return nullptr;
            }

            VertexArrayConfig vaoConfig;
            if (!mesh.appendBindings(vaoConfig)) {
                log_error("Renderer3D: mesh does not expose its vertex bindings for instancing");
                return nullptr;
            }
            vaoConfig.vertexBuffers.push_back({batch.buffer.get(), sizeof(MeshInstance), 0,
                                               instanceAttributes(), 1});

            batch.vao = VertexArray::create(vaoConfig);
            if (!batch.vao) {
                log_error("Renderer3D: could not create an instanced vertex array");
                return nullptr;
            }
            batch.topology = mesh.topology();
            batch.vertexCount = mesh.vertexCount();
            batch.capacity = capacity;

            const auto inserted = _gpuBatches.insert_or_assign(&mesh, std::move(batch));
            return &inserted.first->second;
        }

        std::vector<Renderable> _items;
        std::vector<InstancedBatch> _batches;

        mutable std::unordered_map<const Mesh*, GpuBatch> _gpuBatches;
        mutable std::vector<MeshInstance> _visible;
};

#endif