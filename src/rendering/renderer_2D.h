#pragma once
#include "renderer_core.h"

#include <functional>

#include "./lights/light_2D.h"
#include "../core/texture.h"

// Position, rotation (radians, counter-clockwise) and scale in Camera2D world units.
struct Transform2D {
    glm::vec2 position{0.0f};
    float rotation = 0.0f;
    glm::vec2 scale{1.0f};

    // Model matrix, applied as scale then rotate then translate.
    glm::mat4 matrix() const noexcept {
        glm::mat4 model = glm::translate(glm::mat4(1.0f), glm::vec3(position, 0.0f));
        if (rotation != 0.0f) {
            model = glm::rotate(model, rotation, glm::vec3(0.0f, 0.0f, 1.0f));
        }
        return glm::scale(model, glm::vec3(scale, 1.0f));
    }

    // Builds a transform at a position, with an optional uniform scale.
    static Transform2D at(const glm::vec2& position, float uniformScale = 1.0f) noexcept {
        Transform2D transform;
        transform.position = position;
        transform.scale = glm::vec2(uniformScale);
        return transform;
    }
};

// Destination rectangle for a sprite quad, in Camera2D world units.
struct QuadRect {
    float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
};

// Per-quad tint packed as RGBA8, unpacked by a normalised vertex attribute.
struct QuadColor {
    unsigned char r = 255, g = 255, b = 255, a = 255;

    // Builds a tint from 0..1 components, clamping and rounding; NaN becomes 0.
    static QuadColor fromFloats(float r, float g, float b, float a = 1.0f) noexcept {
        const auto clamp8 = [](float value) -> unsigned char {
            const float scaled = value * 255.0f + 0.5f;
            if (!(scaled > 0.0f)) return 0;
            if (scaled >= 255.0f) return 255;
            return static_cast<unsigned char>(scaled);
        };
        return {clamp8(r), clamp8(g), clamp8(b), clamp8(a)};
    }
};

// One mesh drawn in the 2D path. boundsRadius is local and centred on the origin;
// 0 never culls. The mesh is non-owning and must outlive the renderer.
struct Renderable2D {
    const Mesh* mesh = nullptr;
    Transform2D transform;
    std::shared_ptr<Material> material;
    float boundsRadius = 0.0f;
    unsigned int layers = 0xFFFFFFFFu;
};

#if ENGINE_HAS_VERTEX_ARRAY

// Two independent paths: mesh renderables through submit() and draw(), and textured
// sprite quads through submitQuad() and drawQuads().
class Renderer2D {
    public:
        // Vertex written by the quad path: position, UV, packed tint.
        struct QuadVertex {
            float x = 0.0f, y = 0.0f;
            float u = 0.0f, v = 0.0f;
            QuadColor tint;
        };

        // Layout the quad path uploads with: position at 0, UV at 1, tint at 2.
        static VertexLayout quadLayout() noexcept {
            VertexAttribute position;
            position.location = 0;
            position.components = 2;
            position.type = GL_FLOAT;
            position.offset = offsetof(QuadVertex, x);

            VertexAttribute uv;
            uv.location = 1;
            uv.components = 2;
            uv.type = GL_FLOAT;
            uv.offset = offsetof(QuadVertex, u);

            VertexAttribute tint;
            tint.location = 2;
            tint.components = 4;
            tint.type = GL_UNSIGNED_BYTE;
            tint.normalised = true;
            tint.offset = offsetof(QuadVertex, tint);

            VertexLayout layout;
            layout.attributes = {position, uv, tint};
            layout.stride = sizeof(QuadVertex);
            return layout;
        }

        // Empties both lists, ready for the next frame. The quad buffer is kept.
        void clear() noexcept {
            _items.clear();
            _quads.clear();
            _runs.clear();
        }

        // Releases the quad vertex buffer too. Call after changing the GL context.
        void releaseQuadBuffer() noexcept {
            _quadVao.reset();
            _quadBuffer.reset();
            _quadCapacity = 0;
        }

        // Adds a copy of a renderable.
        void submit(const Renderable2D& item) noexcept {_items.push_back(item);}

        // Adds a renderable built from its parts.
        void submit(const Mesh& mesh, const Transform2D& transform,
                    std::shared_ptr<Material> material = nullptr, float boundsRadius = 0.0f) noexcept {
            Renderable2D item;
            item.mesh = &mesh;
            item.transform = transform;
            item.material = std::move(material);
            item.boundsRadius = boundsRadius;
            _items.push_back(std::move(item));
        }

        // Records a sprite quad; the texture must outlive the next drawQuads().
        void submitQuad(const Texture& texture, const QuadRect& dst,
                        const UVRect& uv = {}, QuadColor tint = {}) noexcept {
            _quads.push_back({&texture, nullptr, dst, uv, tint});
        }

        // Records a sprite quad lit through a tangent-space normal map laid out like its
        // texture (same UVs). Both textures must outlive the next drawQuads().
        void submitNormalMappedQuad(const Texture& texture, const Texture& normalMap,
                                    const QuadRect& dst, const UVRect& uv = {},
                                    QuadColor tint = {}) noexcept {
            _quads.push_back({&texture, &normalMap, dst, uv, tint});
        }

        // Records one cell of a uniform grid, for tilesheets and sprite strips.
        void submitCell(const Texture& texture, const QuadRect& dst,
                        int column, int row, int columns, int rows,
                        QuadColor tint = {}) noexcept {
            submitQuad(texture, dst, UVRect::cell(column, row, columns, rows), tint);
        }

        // Records one normal-mapped cell of a uniform grid shared by both sheets.
        void submitNormalMappedCell(const Texture& texture, const Texture& normalMap,
                                    const QuadRect& dst, int column, int row, int columns, int rows,
                                    QuadColor tint = {}) noexcept {
            submitNormalMappedQuad(texture, normalMap, dst,
                                   UVRect::cell(column, row, columns, rows), tint);
        }

        // Sprite quads currently submitted.
        size_t quadCount() const noexcept {return _quads.size();}

        // True when drawQuads() groups quads by texture instead of keeping submission order.
        bool sortsQuadsByTexture() const noexcept {return _sortQuads;}
        // Groups quads by texture for fewer draws. Only safe when quads with different
        // textures never overlap, since it changes which one is drawn on top.
        void setSortQuadsByTexture(bool sort) noexcept {_sortQuads = sort;}

        // Renderables currently submitted.
        const std::vector<Renderable2D>& items() const noexcept {return _items;}
        // Number of renderables currently submitted.
        size_t size() const noexcept {return _items.size();}

        // Draws every renderable the context admits with the given, already bound, shader.
        void draw(const Shader& shader, const SceneContext& context) const noexcept {
            for (const Renderable2D& item : _items) {
                if (!item.mesh) continue;
                const glm::vec3 centre(item.transform.position, 0.0f);
                const float radius = item.boundsRadius
                    * renderer_tools::maxScale(glm::vec3(item.transform.scale, 0.0f));
                if (!context.isVisible(centre, radius, item.layers)) continue;

                shader.setMat4("uModel", item.transform.matrix());
                if (context.materialsRequired && item.material) item.material->upload(shader);
                item.mesh->draw();
            }
        }

        // Uploads the sprite quads as one buffer and draws one run per consecutive texture
        // pair, in submission order unless sorting is enabled. Needs a program declaring
        // uTexture, such as LightingPipeline2D::spriteShader(); normal maps bind to
        // textureUnit + 1 and need a program declaring uNormalMap, which the sprite shader does.
        void drawQuads(const Shader& shader, int textureUnit = 0) const noexcept {
            if (_quads.empty()) return;

            _order.resize(_quads.size());
            for (size_t i = 0; i < _order.size(); ++i) _order[i] = i;
            if (_sortQuads) {
                std::stable_sort(_order.begin(), _order.end(), [this](size_t a, size_t b) {
                    const QuadEntry& left = _quads[a];
                    const QuadEntry& right = _quads[b];
                    if (left.texture != right.texture) {
                        return std::less<const Texture*>{}(left.texture, right.texture);
                    }
                    return std::less<const Texture*>{}(left.normalMap, right.normalMap);
                });
            }

            _vertices.clear();
            _vertices.reserve(_order.size() * 6);
            _runs.clear();

            for (const size_t index : _order) {
                const QuadEntry& quad = _quads[index];
                if (_runs.empty() || _runs.back().texture != quad.texture
                    || _runs.back().normalMap != quad.normalMap) {
                    _runs.push_back({quad.texture, quad.normalMap, _vertices.size(), 0});
                }
                appendQuad(quad);
                _runs.back().count += 6;
            }

            if (!ensureQuadBuffer(_vertices.size())) return;
            renderer_tools::uploadDynamic(*_quadBuffer, _vertices.data(),
                                          _vertices.size() * sizeof(QuadVertex));

            if (_checkedQuadProgram != shader.id()) {
                _checkedQuadProgram = shader.id();
                _quadProgramNormalMaps = shader.uniformLocation("uUseNormalMap") != -1;
                _warnedNormalMaps = false;
                if (shader.uniformLocation("uTexture") == -1) {
                    log_error("Renderer2D::drawQuads: program ", shader.id(), " declares no uTexture, "
                              "so sprite quads will not draw; use LightingPipeline2D::spriteShader()");
                }
            }

            const int normalUnit = textureUnit + 1;
            shader.setMat4("uModel", glm::mat4(1.0f));
            shader.setInt("uTexture", textureUnit);
            if (_quadProgramNormalMaps) shader.setInt("uNormalMap", normalUnit);

            for (const Run& run : _runs) {
                if (!run.texture || run.count == 0) continue;
                run.texture->bind(static_cast<unsigned int>(textureUnit));
                if (_quadProgramNormalMaps) {
                    if (run.normalMap) run.normalMap->bind(static_cast<unsigned int>(normalUnit));
                    shader.setInt("uUseNormalMap", run.normalMap ? 1 : 0);
                } else if (run.normalMap && !_warnedNormalMaps) {
                    _warnedNormalMaps = true;
                    log_warn("Renderer2D::drawQuads: program ", shader.id(), " declares no "
                             "uUseNormalMap, so normal maps are ignored; use LightingPipeline2D::spriteShader()");
                }
                _quadVao->draw(GL_TRIANGLES, static_cast<int>(run.first),
                               static_cast<int>(run.count));
            }
        }

    private:
        // One recorded sprite quad.
        struct QuadEntry {
            const Texture* texture = nullptr;
            const Texture* normalMap = nullptr;
            QuadRect dst;
            UVRect uv;
            QuadColor tint;
        };

        // A contiguous span of vertices sharing one texture and normal map.
        struct Run {
            const Texture* texture = nullptr;
            const Texture* normalMap = nullptr;
            size_t first = 0;
            size_t count = 0;
        };

        // Appends two counter-clockwise triangles, with v0 at the top of the source rectangle.
        void appendQuad(const QuadEntry& quad) const noexcept {
            const float x0 = quad.dst.x;
            const float y0 = quad.dst.y;
            const float x1 = x0 + quad.dst.width;
            const float y1 = y0 + quad.dst.height;

            const QuadVertex bottomLeft{x0, y0, quad.uv.u0, quad.uv.v1, quad.tint};
            const QuadVertex bottomRight{x1, y0, quad.uv.u1, quad.uv.v1, quad.tint};
            const QuadVertex topRight{x1, y1, quad.uv.u1, quad.uv.v0, quad.tint};
            const QuadVertex topLeft{x0, y1, quad.uv.u0, quad.uv.v0, quad.tint};

            _vertices.push_back(bottomLeft);
            _vertices.push_back(bottomRight);
            _vertices.push_back(topRight);
            _vertices.push_back(bottomLeft);
            _vertices.push_back(topRight);
            _vertices.push_back(topLeft);
        }

        // Grows the quad buffer and its vertex array; a failure releases both, never leaving
        // a vertex array that points at a freed buffer.
        bool ensureQuadBuffer(size_t vertexCount) const noexcept {
            if (_quadVao && _quadCapacity >= vertexCount) return true;

            const size_t capacity = renderer_tools::growCapacity(vertexCount, 256);
            _quadBuffer = renderer_tools::createDynamicBuffer(capacity * sizeof(QuadVertex));
            if (!_quadBuffer) {
                log_error("Renderer2D: could not create a quad vertex buffer for ",
                          vertexCount, " vertices");
                _quadVao.reset();
                _quadCapacity = 0;
                return false;
            }

            const VertexLayout layout = quadLayout();
            VertexArrayConfig vaoConfig;
            vaoConfig.vertexBuffers.push_back({_quadBuffer.get(), layout.stride, 0,
                                               layout.attributes, 0});

            _quadVao = VertexArray::create(vaoConfig);
            if (!_quadVao) {
                log_error("Renderer2D: could not create the quad vertex array");
                _quadBuffer.reset();
                _quadCapacity = 0;
                return false;
            }
            _quadCapacity = capacity;
            return true;
        }

        std::vector<Renderable2D> _items;
        std::vector<QuadEntry> _quads;
        bool _sortQuads = false;

        mutable std::vector<size_t> _order;
        mutable std::vector<QuadVertex> _vertices;
        mutable std::vector<Run> _runs;
        mutable std::unique_ptr<Buffer> _quadBuffer;
        mutable std::unique_ptr<VertexArray> _quadVao;
        mutable size_t _quadCapacity = 0;
        mutable unsigned int _checkedQuadProgram = 0;
        mutable bool _quadProgramNormalMaps = false;
        mutable bool _warnedNormalMaps = false;
};

#endif