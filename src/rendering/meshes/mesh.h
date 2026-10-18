#pragma once
#include "../../core/engine_config.h"
#include "../../core/buffer.h"
#include "../../core/vertex_array.h"

#if ENGINE_HAS_VERTEX_ARRAY

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

#include "../../core/logs.h"

namespace mesh_detail {

    // Byte size of one component of an attribute type; 0 when unrecognised.
    inline size_t attributeTypeSize(unsigned int type) noexcept {
        switch (type) {
            case GL_BYTE:  case GL_UNSIGNED_BYTE:  return 1;
            case GL_SHORT: case GL_UNSIGNED_SHORT: return 2;
            #ifdef GL_HALF_FLOAT
                case GL_HALF_FLOAT: return 2;
            #endif
            case GL_INT: case GL_UNSIGNED_INT: case GL_FLOAT: return 4;
            case GL_DOUBLE: return 8;
            default: return 0;
        }
    }

    // Stride implied by the furthest-reaching attribute; 0 when any is invalid.
    inline size_t computeStride(const std::vector<VertexAttribute>& attributes) noexcept {
        size_t stride = 0;
        for (const VertexAttribute& attribute : attributes) {
            const size_t size = attributeTypeSize(attribute.type);
            if (size == 0 || attribute.components <= 0) return 0;
            stride = std::max(stride, attribute.offset + size * static_cast<size_t>(attribute.components));
        }
        return stride;
    }
}

struct VertexLayout {
    std::vector<VertexAttribute> attributes;
    size_t stride = 0;

    // True when the layout has attributes and a non-zero stride.
    bool isValid() const noexcept {return !attributes.empty() && stride > 0;}

    // Builds a tightly packed float layout from per-attribute component counts.
    static VertexLayout floats(std::initializer_list<int> components, unsigned int firstLocation = 0) noexcept {
        VertexLayout layout;
        size_t offset = 0;
        unsigned int location = firstLocation;
        for (int count : components) {
            if (count < 1 || count > 4) {
                log_error("VertexLayout::floats: a component count must be 1-4, got ", count);
                return VertexLayout{};
            }
            VertexAttribute attribute;
            attribute.location = location++;
            attribute.components = count;
            attribute.type = GL_FLOAT;
            attribute.normalised = false;
            attribute.offset = offset;
            layout.attributes.push_back(attribute);
            offset += static_cast<size_t>(count) * sizeof(float);
        }
        layout.stride = offset;
        return layout;
    }

    // Builds a layout from explicit attributes, deriving the stride.
    static VertexLayout from(std::vector<VertexAttribute> attributes) noexcept {
        VertexLayout layout;
        layout.stride = mesh_detail::computeStride(attributes);
        layout.attributes = std::move(attributes);
        return layout;
    }
};

namespace VertexLayouts {
    // 2D position only.
    inline VertexLayout pos2()           noexcept {return VertexLayout::floats({2});}
    // 2D position plus texture coordinates.
    inline VertexLayout pos2_uv2()       noexcept {return VertexLayout::floats({2, 2});}
    // 2D position, texture coordinates and an RGBA float colour.
    inline VertexLayout pos2_uv2_col4()  noexcept {return VertexLayout::floats({2, 2, 4});}
    // 3D position only.
    inline VertexLayout pos3()           noexcept {return VertexLayout::floats({3});}
    // 3D position plus texture coordinates.
    inline VertexLayout pos3_uv2()       noexcept {return VertexLayout::floats({3, 2});}
    // 3D position, texture coordinates and a normal.
    inline VertexLayout pos3_uv2_nrm3()  noexcept {return VertexLayout::floats({3, 2, 3});}
}

struct MeshData {
    std::vector<float> vertices;
    std::vector<unsigned int> indices;   // empty => non-indexed, drawn with glDrawArrays
    VertexLayout layout;
    unsigned int topology = GL_TRIANGLES;

    // Vertex count implied by the data size and layout stride.
    size_t vertexCount() const noexcept {
        if (layout.stride == 0) return 0;
        return (vertices.size() * sizeof(float)) / layout.stride;
    }

    // True when the mesh carries an index buffer.
    bool isIndexed() const noexcept {return !indices.empty();}
};

class Mesh {
    public:
        // Uploads mesh data into GPU buffers and a VAO; null on failure.
        static std::unique_ptr<Mesh> create(const MeshData& data, unsigned int usage = GL_STATIC_DRAW) noexcept {
            if (!data.layout.isValid()) {
                log_error("Mesh: vertex layout has no attributes or a zero stride");
                return nullptr;
            }
            if (data.vertices.empty()) {
                log_error("Mesh: no vertex data");
                return nullptr;
            }

            const size_t byteSize = data.vertices.size() * sizeof(float);
            if (byteSize % data.layout.stride != 0) {
                log_error("Mesh: vertex data (", byteSize, " bytes) is not a whole number of ",
                          data.layout.stride, "-byte vertices");
                return nullptr;
            }

            const size_t vertexCount = byteSize / data.layout.stride;
            for (unsigned int index : data.indices) {
                if (static_cast<size_t>(index) >= vertexCount) {
                    log_error("Mesh: index ", index, " is out of range for ", vertexCount, " vertices");
                    return nullptr;
                }
            }

            auto vbo = Buffer::from(GL_ARRAY_BUFFER, data.vertices, usage);
            if (!vbo) return nullptr;

            std::unique_ptr<Buffer> ebo;
            if (data.isIndexed()) {
                ebo = Buffer::from(GL_ELEMENT_ARRAY_BUFFER, data.indices, usage);
                if (!ebo) return nullptr;
            }

            VertexArrayConfig config;
            config.vertexBuffers.push_back({vbo.get(), data.layout.stride, 0, data.layout.attributes, 0});
            config.indexBuffer = ebo.get();

            auto vao = VertexArray::create(config);
            if (!vao) return nullptr;

            return std::unique_ptr<Mesh>(new Mesh(std::move(vao), std::move(vbo), std::move(ebo),
                                                  data.layout, data.topology, vertexCount));
        }

        // Kept for existing call sites. Prefer create(MeshData).
        static std::unique_ptr<Mesh> createFromVectors(std::vector<float> vertices,
                                            std::vector<unsigned int> indices,
                                            std::vector<VertexAttribute> attributes,
                                            unsigned int topology = GL_TRIANGLES) noexcept {
            MeshData data;
            data.vertices = std::move(vertices);
            data.indices = std::move(indices);
            data.layout = VertexLayout::from(std::move(attributes));
            data.topology = topology;
            return create(data);
        }

        // Draws the mesh with its stored topology.
        void draw() const noexcept {draw(_topology);}

        // Draws the mesh with an explicit topology, indexed when it has indices.
        void draw(unsigned int topology) const noexcept {
            if (_vao->hasIndices()) _vao->drawIndexed(topology);
            else                    _vao->draw(topology, 0, static_cast<int>(_vertexCount));
        }

        // The topology used by the no-argument draw().
        unsigned int topology() const noexcept {return _topology;}
        // Sets the topology used by the no-argument draw().
        void setTopology(unsigned int topology) noexcept {_topology = topology;}

        // Vertex count uploaded to the vertex buffer.
        size_t vertexCount() const noexcept {return _vertexCount;}
        // Index count, or 0 for a non-indexed mesh.
        size_t indexCount() const noexcept {return _vao->indexCount();}
        // True when the mesh has an index buffer.
        bool isIndexed() const noexcept {return _vao->hasIndices();}

        // The underlying vertex array, for custom draw calls.
        const VertexArray& vertexArray() const noexcept {return *_vao;}

        // The layout its vertex buffer was uploaded with.
        const VertexLayout& layout() const noexcept {return _layout;}

        // Adds this mesh's vertex and index buffers to a VertexArrayConfig, so a
        // caller can build a derived vertex array that also binds its own
        // buffers -- an instanced one, for example. The mesh keeps ownership, so
        // it must outlive any vertex array built this way. False if it has no
        // vertex buffer to contribute.
        bool appendBindings(VertexArrayConfig& config) const noexcept {
            if (!_vbo) return false;
            config.vertexBuffers.push_back({_vbo.get(), _layout.stride, 0, _layout.attributes, 0});
            if (_ebo) config.indexBuffer = _ebo.get();
            return true;
        }

        Mesh(const Mesh&) = delete;
        Mesh& operator=(const Mesh&) = delete;

        // Movable: every GPU handle it owns lives behind a unique_ptr.
        Mesh(Mesh&&) noexcept = default;
        Mesh& operator=(Mesh&&) noexcept = default;

    private:
        explicit Mesh(std::unique_ptr<VertexArray> vao, std::unique_ptr<Buffer> vbo,
                      std::unique_ptr<Buffer> ebo, VertexLayout layout,
                      unsigned int topology, size_t vertexCount) noexcept
            : _vao(std::move(vao)), _vbo(std::move(vbo)), _ebo(std::move(ebo)),
              _layout(std::move(layout)), _topology(topology), _vertexCount(vertexCount) {}

        std::unique_ptr<VertexArray> _vao;
        std::unique_ptr<Buffer> _vbo, _ebo;
        VertexLayout _layout;
        unsigned int _topology = GL_TRIANGLES;
        size_t _vertexCount = 0;
};

enum class Pivot {Center, BottomLeft};

namespace Mesh2D {
    // A textured quad in the XY plane, centred or bottom-left anchored.
    inline MeshData quad(float width, float height, Pivot pivot = Pivot::Center) noexcept {
        const float x0 = (pivot == Pivot::Center) ? -width * 0.5f : 0.0f;
        const float y0 = (pivot == Pivot::Center) ? -height * 0.5f : 0.0f;
        const float x1 = x0 + width;
        const float y1 = y0 + height;

        MeshData data;
        data.layout = VertexLayouts::pos2_uv2();
        data.vertices = {
            x0, y0,  0.0f, 0.0f,
            x1, y0,  1.0f, 0.0f,
            x1, y1,  1.0f, 1.0f,
            x0, y1,  0.0f, 1.0f,
        };
        data.indices = {0, 1, 2, 0, 2, 3};
        return data;
    }

    // A filled circle as a triangle fan around a centre vertex.
    inline MeshData circle(float radius, int segments = 32) noexcept {
        segments = std::max(segments, 3);

        MeshData data;
        data.layout = VertexLayouts::pos2_uv2();
        data.vertices = {0.0f, 0.0f, 0.5f, 0.5f};

        const float step = 6.28318530718f / static_cast<float>(segments);
        for (int i = 0; i < segments; ++i) {
            const float angle = step * static_cast<float>(i);
            const float cx = std::cos(angle);
            const float sy = std::sin(angle);
            data.vertices.push_back(cx * radius);
            data.vertices.push_back(sy * radius);
            data.vertices.push_back(cx * 0.5f + 0.5f);
            data.vertices.push_back(sy * 0.5f + 0.5f);
        }
        for (int i = 0; i < segments; ++i) {
            data.indices.push_back(0);
            data.indices.push_back(static_cast<unsigned int>(1 + i));
            data.indices.push_back(static_cast<unsigned int>(1 + (i + 1) % segments));
        }
        return data;
    }

    // A rectangle outline drawn as four lines.
    inline MeshData lineRect(float width, float height, Pivot pivot = Pivot::Center) noexcept {
        const float x0 = (pivot == Pivot::Center) ? -width * 0.5f : 0.0f;
        const float y0 = (pivot == Pivot::Center) ? -height * 0.5f : 0.0f;
        const float x1 = x0 + width;
        const float y1 = y0 + height;

        MeshData data;
        data.layout = VertexLayouts::pos2();
        data.topology = GL_LINES;
        data.vertices = {x0, y0,  x1, y0,  x1, y1,  x0, y1};
        data.indices = {0, 1, 1, 2, 2, 3, 3, 0};
        return data;
    }
}

namespace Mesh3D {

    // A centred quad facing +Z, with UVs and normals.
    inline MeshData quad(float width, float height) noexcept {
        const float hw = width * 0.5f;
        const float hh = height * 0.5f;

        MeshData data;
        data.layout = VertexLayouts::pos3_uv2_nrm3();
        data.vertices = {
            -hw, -hh, 0.0f,  0.0f, 0.0f,  0.0f, 0.0f, 1.0f,
             hw, -hh, 0.0f,  1.0f, 0.0f,  0.0f, 0.0f, 1.0f,
             hw,  hh, 0.0f,  1.0f, 1.0f,  0.0f, 0.0f, 1.0f,
            -hw,  hh, 0.0f,  0.0f, 1.0f,  0.0f, 0.0f, 1.0f,
        };
        data.indices = {0, 1, 2, 0, 2, 3};
        return data;
    }

    // A centred cube with per-face UVs and outward normals.
    inline MeshData cube(float size = 1.0f) noexcept {
        const float h = size * 0.5f;

        static const float basis[6][9] = {
            { 1, 0, 0,   0, 1, 0,   0, 0, 1},   // +Z
            {-1, 0, 0,   0, 1, 0,   0, 0,-1},   // -Z
            { 0, 0,-1,   0, 1, 0,   1, 0, 0},   // +X
            { 0, 0, 1,   0, 1, 0,  -1, 0, 0},   // -X
            { 1, 0, 0,   0, 0,-1,   0, 1, 0},   // +Y
            { 1, 0, 0,   0, 0, 1,   0,-1, 0},   // -Y
        };
        static const float corner[4][2] = {{-1.0f, -1.0f}, {1.0f, -1.0f}, {1.0f, 1.0f}, {-1.0f, 1.0f}};

        MeshData data;
        data.layout = VertexLayouts::pos3_uv2_nrm3();
        data.vertices.reserve(24 * 8);
        data.indices.reserve(36);

        for (int face = 0; face < 6; ++face) {
            const float* right  = &basis[face][0];
            const float* up     = &basis[face][3];
            const float* normal = &basis[face][6];

            for (int c = 0; c < 4; ++c) {
                for (int axis = 0; axis < 3; ++axis) {
                    data.vertices.push_back((normal[axis] + right[axis] * corner[c][0]
                                                          + up[axis] * corner[c][1]) * h);
                }
                data.vertices.push_back(corner[c][0] * 0.5f + 0.5f);
                data.vertices.push_back(corner[c][1] * 0.5f + 0.5f);
                data.vertices.push_back(normal[0]);
                data.vertices.push_back(normal[1]);
                data.vertices.push_back(normal[2]);
            }

            const unsigned int base = static_cast<unsigned int>(face * 4);
            data.indices.insert(data.indices.end(),
                                {base, base + 1, base + 2, base, base + 2, base + 3});
        }
        return data;
    }

    // A ground-plane grid of lines spanning +/-extent on X and Z.
    inline MeshData gridLines(float extent = 10.0f, int divisions = 20) noexcept {
        divisions = std::max(divisions, 1);

        MeshData data;
        data.layout = VertexLayouts::pos3();
        data.topology = GL_LINES;

        const float step = (extent * 2.0f) / static_cast<float>(divisions);
        for (int i = 0; i <= divisions; ++i) {
            const float offset = -extent + step * static_cast<float>(i);
            data.vertices.insert(data.vertices.end(), {offset, 0.0f, -extent, offset, 0.0f, extent});
            data.vertices.insert(data.vertices.end(), {-extent, 0.0f, offset, extent, 0.0f, offset});
        }
        return data;
    }
}

#endif