#pragma once

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "engine_config.h"
#include "logs.h"
#include "buffer.h"
#include "error_handling/gl_error_handler.h"

#if ENGINE_HAS_VERTEX_ARRAY

struct VertexAttribute {
    unsigned int location = 0;
    int components = 3;
    unsigned int type = GL_FLOAT;
    bool normalised = false;
    size_t offset = 0;

    // Per-attribute instancing step. Leave at 0 to inherit the owning
    // VertexBufferBinding::divisor; whichever is non-zero wins, on both paths.
    unsigned int divisor = 0;

    // True for non-normalised integer types, which need the I-variant pointers.
    bool isInteger() const noexcept {
        switch (type) {
            case GL_BYTE: case GL_UNSIGNED_BYTE:
            case GL_SHORT: case GL_UNSIGNED_SHORT:
            case GL_INT: case GL_UNSIGNED_INT:
                return !normalised;
            default:
                return false;
        }
    }

    // True for double-precision attributes, which need the L-variant pointers.
    bool isDouble() const noexcept {return type == GL_DOUBLE;}
};

struct VertexBufferBinding {
    const Buffer* buffer = nullptr;
    size_t stride = 0;
    size_t offset = 0;
    std::vector<VertexAttribute> attributes;

    // Instancing step for every attribute in this binding that leaves its own
    // divisor at 0. This is the field the GL 4.3 separate-format path uses.
    unsigned int divisor = 0;
};

struct VertexArrayConfig {
    std::vector<VertexBufferBinding> vertexBuffers;
    const Buffer* indexBuffer = nullptr;
    unsigned int indexType = GL_UNSIGNED_INT;

    #if ENGINE_HAS_SEPARATE_ATTRIB
        bool separateAttribFormat = false;
    #endif
};

// A GL vertex array object. create() restores the bindings it disturbs; the draw
// and attribute methods deliberately leave this VAO bound for the caller.
class VertexArray {
    public:
        // Builds a VAO from vertex-buffer bindings and an optional index buffer.
        static std::unique_ptr<VertexArray> create(const VertexArrayConfig& config) noexcept {
            if (config.vertexBuffers.empty()) {
                log_error("VertexArray: no vertex buffers given");
                return nullptr;
            }

            unsigned int handle = 0;
            glGenVertexArrays(1, &handle);
            if (!handle) {log_error("VertexArray: failed to generate a handle"); return nullptr;}

            GlErrorHandler::clear();

            int previousArray = 0;
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previousArray);
            int previousArrayBuffer = 0;
            glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &previousArrayBuffer);
            glBindVertexArray(handle);

            const auto restoreBindings = [&] {
                glBindVertexArray(static_cast<unsigned int>(previousArray));
                glBindBuffer(GL_ARRAY_BUFFER, static_cast<unsigned int>(previousArrayBuffer));
            };
            const auto abort = [&](const char* message) -> std::unique_ptr<VertexArray> {
                log_error(message);
                restoreBindings();
                glDeleteVertexArrays(1, &handle);
                return nullptr;
            };

            size_t indexCount = 0;
            for (const VertexBufferBinding& binding : config.vertexBuffers) {
                if (!binding.buffer) return abort("VertexArray: a binding has no buffer");
                if (binding.stride == 0) return abort("VertexArray: stride must be greater than zero");

                #if ENGINE_HAS_SEPARATE_ATTRIB
                if (config.separateAttribFormat) {
                    const unsigned int slot = static_cast<unsigned int>(&binding - config.vertexBuffers.data());
                    glBindVertexBuffer(slot, binding.buffer->id(),
                                       static_cast<GLintptr>(binding.offset),
                                       static_cast<GLsizei>(binding.stride));
                    for (const VertexAttribute& attribute : binding.attributes) {
                        glEnableVertexAttribArray(attribute.location);
                        if (attribute.isDouble()) {
                            glVertexAttribLFormat(attribute.location, attribute.components,
                                                  attribute.type, static_cast<GLuint>(attribute.offset));
                        } else if (attribute.isInteger()) {
                            glVertexAttribIFormat(attribute.location, attribute.components,
                                                  attribute.type, static_cast<GLuint>(attribute.offset));
                        } else {
                            glVertexAttribFormat(attribute.location, attribute.components,
                                                 attribute.type,
                                                 attribute.normalised ? GL_TRUE : GL_FALSE,
                                                 static_cast<GLuint>(attribute.offset));
                        }
                        glVertexAttribBinding(attribute.location, slot);
                    }
                    // The separate-format path carries one divisor per binding slot, so
                    // per-attribute divisors within a binding must agree.
                    const unsigned int slotDivisor = resolveBindingDivisor(binding);
                    if (slotDivisor > 0) glVertexBindingDivisor(slot, slotDivisor);
                    continue;
                }
                #endif

                glBindBuffer(GL_ARRAY_BUFFER, binding.buffer->id());
                for (const VertexAttribute& attribute : binding.attributes) {
                    glEnableVertexAttribArray(attribute.location);

                    const void* pointer =
                        reinterpret_cast<const void*>(binding.offset + attribute.offset);

                    #if ENGINE_HAS_DOUBLE_ATTRIB
                        if (attribute.isDouble()) {
                            glVertexAttribLPointer(attribute.location, attribute.components,
                                                   attribute.type,
                                                   static_cast<GLsizei>(binding.stride), pointer);
                        } else
                    #endif
                    #if ENGINE_HAS_INTEGER_ATTRIB
                        if (attribute.isInteger()) {
                            glVertexAttribIPointer(attribute.location, attribute.components,
                                                   attribute.type,
                                                   static_cast<GLsizei>(binding.stride), pointer);
                        } else
                    #endif
                    {
                        glVertexAttribPointer(attribute.location, attribute.components,
                                              attribute.type,
                                              attribute.normalised ? GL_TRUE : GL_FALSE,
                                              static_cast<GLsizei>(binding.stride), pointer);
                    }

                    const unsigned int divisor = attribute.divisor > 0 ? attribute.divisor
                                                                       : binding.divisor;
                    #if ENGINE_HAS_INSTANCING
                        if (divisor > 0) glVertexAttribDivisor(attribute.location, divisor);
                    #else
                        if (divisor > 0) log_warn("VertexArray: attribute divisors need GL 3.3");
                    #endif
                }
            }

            if (config.indexBuffer) {
                glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, config.indexBuffer->id());
                indexCount = config.indexBuffer->size() / indexTypeSize(config.indexType);
            }

            restoreBindings();

            if (GlErrorHandler::logErrors()) {glDeleteVertexArrays(1, &handle); return nullptr;}

            return std::unique_ptr<VertexArray>(
                new VertexArray(handle, config.indexBuffer != nullptr, config.indexType, indexCount));
        }

        // The GL vertex-array name.
        unsigned int id() const noexcept {return _handle;}
        // True when built with an index buffer.
        bool hasIndices() const noexcept {return _hasIndices;}
        // Index count derived from the index buffer's size.
        size_t indexCount() const noexcept {return _indexCount;}
        // True when the handle still names a live GL vertex array.
        bool isValid() const noexcept {return _handle != 0 && glIsVertexArray(_handle) == GL_TRUE;}

        // Binds this vertex array, and leaves it bound.
        void bind() const noexcept {glBindVertexArray(_handle);}
        // Unbinds any vertex array.
        static void unbind() noexcept {glBindVertexArray(0);}

        // ---- drawing ----

        // Non-indexed draw of `count` vertices starting at `first`.
        void draw(unsigned int mode, int first, int count) const noexcept {
            bind();
            glDrawArrays(mode, first, count);
        }

        // Indexed draw; count < 0 draws every index.
        void drawIndexed(unsigned int mode, int count = -1, size_t byteOffset = 0) const noexcept {
            if (!_hasIndices) {log_error("drawIndexed called on a VertexArray with no index buffer"); return;}
            bind();
            const int actual = count >= 0 ? count : static_cast<int>(_indexCount);
            glDrawElements(mode, actual, _indexType, reinterpret_cast<const void*>(byteOffset));
        }

        // Indexed draw with a declared start..end vertex range hint.
        void drawIndexedRange(unsigned int mode, unsigned int start, unsigned int end,
                              int count = -1, size_t byteOffset = 0) const noexcept {
            if (!_hasIndices) {log_error("drawIndexedRange needs an index buffer"); return;}
            bind();
            const int actual = count >= 0 ? count : static_cast<int>(_indexCount);
            glDrawRangeElements(mode, start, end, actual, _indexType,
                                reinterpret_cast<const void*>(byteOffset));
        }

        #if ENGINE_HAS_BASE_VERTEX
            // Indexed draw with a constant added to every index.
            void drawIndexedBaseVertex(unsigned int mode, int baseVertex,
                                       int count = -1, size_t byteOffset = 0) const noexcept {
                if (!_hasIndices) {log_error("drawIndexedBaseVertex needs an index buffer"); return;}
                bind();
                const int actual = count >= 0 ? count : static_cast<int>(_indexCount);
                glDrawElementsBaseVertex(mode, actual, _indexType,
                                         reinterpret_cast<const void*>(byteOffset),
                                         baseVertex);
            }
        #endif

        #if ENGINE_HAS_MULTI_DRAW
            // Several non-indexed draws in one call.
            void multiDrawArrays(unsigned int mode, const int* firsts,
                                 const int* counts, int drawCount) const noexcept {
                bind();
                glMultiDrawArrays(mode, firsts, counts, drawCount);
            }

            // Several indexed draws in one call.
            void multiDrawIndexed(unsigned int mode, const int* counts,
                                  const void* const* offsets, int drawCount) const noexcept {
                if (!_hasIndices) {log_error("multiDrawIndexed needs an index buffer"); return;}
                bind();
                glMultiDrawElements(mode, counts, _indexType, offsets, drawCount);
            }
        #endif

        #if ENGINE_HAS_DRAW_INDIRECT
            // Non-indexed draw whose parameters come from GL_DRAW_INDIRECT_BUFFER.
            void drawIndirect(unsigned int mode, size_t byteOffset = 0) const noexcept {
                bind();
                glDrawArraysIndirect(mode, reinterpret_cast<const void*>(byteOffset));
            }

            // Indexed draw whose parameters come from GL_DRAW_INDIRECT_BUFFER.
            void drawIndexedIndirect(unsigned int mode, size_t byteOffset = 0) const noexcept {
                if (!_hasIndices) {log_error("drawIndexedIndirect needs an index buffer"); return;}
                bind();
                glDrawElementsIndirect(mode, _indexType, reinterpret_cast<const void*>(byteOffset));
            }
        #endif

        #if ENGINE_HAS_INSTANCED_DRAW
            // Non-indexed instanced draw (GL 3.1+; per-attribute divisors need 3.3).
            void drawInstanced(unsigned int mode, int first, int count, int instances) const noexcept {
                bind();
                glDrawArraysInstanced(mode, first, count, instances);
            }

            // Indexed instanced draw; count < 0 draws every index.
            void drawIndexedInstanced(unsigned int mode, int instances,
                                      int count = -1, size_t byteOffset = 0) const noexcept {
                if (!_hasIndices) {log_error("drawIndexedInstanced needs an index buffer"); return;}
                bind();
                const int actual = count >= 0 ? count : static_cast<int>(_indexCount);
                glDrawElementsInstanced(mode, actual, _indexType,
                                        reinterpret_cast<const void*>(byteOffset), instances);
            }

            #if ENGINE_HAS_BASE_VERTEX
                // Indexed instanced draw with a constant added to every index.
                void drawIndexedInstancedBaseVertex(unsigned int mode, int instances, int baseVertex,
                                                    int count = -1, size_t byteOffset = 0) const noexcept {
                    if (!_hasIndices) {log_error("drawIndexedInstancedBaseVertex needs an index buffer"); return;}
                    bind();
                    const int actual = count >= 0 ? count : static_cast<int>(_indexCount);
                    glDrawElementsInstancedBaseVertex(mode, actual, _indexType,
                                                      reinterpret_cast<const void*>(byteOffset),
                                                      instances, baseVertex);
                }
            #endif
        #endif

        // ---- attribute control after creation ----

        // Enables one attribute location on this VAO, and leaves it bound.
        void enableAttribute(unsigned int location) const noexcept {
            bind();
            glEnableVertexAttribArray(location);
        }

        // Disables one attribute location on this VAO, and leaves it bound.
        void disableAttribute(unsigned int location) const noexcept {
            bind();
            glDisableVertexAttribArray(location);
        }

        #if ENGINE_HAS_SEPARATE_ATTRIB
            // Rebinds the vertex buffer feeding a binding slot.
            void setVertexBuffer(unsigned int slot, const Buffer& buffer,
                                 size_t offset, size_t stride) const noexcept {
                bind();
                glBindVertexBuffer(slot, buffer.id(), static_cast<GLintptr>(offset),
                                   static_cast<GLsizei>(stride));
            }

            // Sets the instancing divisor of a binding slot.
            void setBindingDivisor(unsigned int slot, unsigned int divisor) const noexcept {
                bind();
                glVertexBindingDivisor(slot, divisor);
            }
        #endif

        ~VertexArray() noexcept {if (_handle) glDeleteVertexArrays(1, &_handle);}

        VertexArray(const VertexArray&) = delete;
        VertexArray& operator=(const VertexArray&) = delete;

        // Movable: the GL name is the only owned resource and it transfers whole.
        VertexArray(VertexArray&& other) noexcept
            : _handle(std::exchange(other._handle, 0)),
              _hasIndices(other._hasIndices), _indexType(other._indexType),
              _indexCount(other._indexCount) {}

        // Deletes this array's name before adopting the other's.
        VertexArray& operator=(VertexArray&& other) noexcept {
            if (this != &other) {
                if (_handle) glDeleteVertexArrays(1, &_handle);
                _handle = std::exchange(other._handle, 0);
                _hasIndices = other._hasIndices;
                _indexType = other._indexType;
                _indexCount = other._indexCount;
            }
            return *this;
        }
    private:
        VertexArray(unsigned int handle, bool hasIndices, unsigned int indexType, size_t indexCount) noexcept
            : _handle(handle), _hasIndices(hasIndices),
              _indexType(indexType), _indexCount(indexCount) {}

        // One divisor for a whole binding slot: the binding's own if set, else the
        // first attribute that declares one. Disagreeing attributes are reported.
        static unsigned int resolveBindingDivisor(const VertexBufferBinding& binding) noexcept {
            if (binding.divisor > 0) return binding.divisor;

            unsigned int resolved = 0;
            for (const VertexAttribute& attribute : binding.attributes) {
                if (attribute.divisor == 0) continue;
                if (resolved == 0) {resolved = attribute.divisor; continue;}
                if (resolved != attribute.divisor) {
                    log_warn("VertexArray: separate attribute format carries one divisor per binding; "
                             "attribute ", attribute.location, " asked for ", attribute.divisor,
                             " but the slot already uses ", resolved);
                }
            }
            return resolved;
        }

        // Byte size of one index of the given type.
        static size_t indexTypeSize(unsigned int type) noexcept {
            switch (type) {
                case GL_UNSIGNED_BYTE:  return 1;
                case GL_UNSIGNED_SHORT: return 2;
                default:                return 4;
            }
        }

        unsigned int _handle = 0;
        bool _hasIndices = false;
        unsigned int _indexType = GL_UNSIGNED_INT;
        size_t _indexCount = 0;
};

#endif