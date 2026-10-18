#pragma once
#include "../core/engine_config.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "../core/buffer.h"
#include "./lights/light_core.h"
#include "./meshes/mesh.h"
#include "../core/shader.h"
#include "../core/vertex_array.h"

// Dimension-agnostic helpers shared by renderer_2D.h and renderer_3D.h.
namespace renderer_tools {

    // Largest absolute scale component, for scaling a local bounding radius.
    inline float maxScale(const glm::vec3& scale) noexcept {
        return std::max(std::max(std::abs(scale.x), std::abs(scale.y)), std::abs(scale.z));
    }

    // Smallest power-of-two multiple of `minimum` that holds `needed`, so buffers that
    // creep upwards do not reallocate every frame.
    inline size_t growCapacity(size_t needed, size_t minimum) noexcept {
        size_t capacity = std::max<size_t>(minimum, 1);
        while (capacity < needed) capacity *= 2;
        return capacity;
    }

#if ENGINE_HAS_VERTEX_ARRAY

    // Creates a GL_DYNAMIC_DRAW array buffer of the given size in bytes.
    inline std::unique_ptr<Buffer> createDynamicBuffer(size_t bytes) noexcept {
        BufferConfig config;
        config.target = GL_ARRAY_BUFFER;
        config.size = bytes;
        config.usage = GL_DYNAMIC_DRAW;
        return Buffer::create(config);
    }

    // Orphans a dynamic buffer and then uploads into it, so the write cannot stall on
    // a draw still reading the previous contents.
    inline void uploadDynamic(Buffer& buffer, const void* data, size_t bytes) noexcept {
        buffer.resize(buffer.size(), nullptr);
        buffer.subData(0, bytes, data);
    }

#endif
}