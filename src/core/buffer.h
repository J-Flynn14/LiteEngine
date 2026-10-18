#pragma once
#include "engine_config.h"

#include <cstdint>
#include <memory>
#include <utility>
#include <vector>

#include "logs.h"
#include "gl_error_handler.h"

#if ENGINE_HAS_VBO

struct BufferConfig {
    unsigned int target = GL_ARRAY_BUFFER;
    size_t size = 0;
    const void* data = nullptr;
    unsigned int usage = GL_STATIC_DRAW;

    #if ENGINE_HAS_BUFFER_STORAGE
        bool immutable = false;
        unsigned int storageFlags = GL_DYNAMIC_STORAGE_BIT;
    #endif
};

// A GL buffer object. Setup methods save and restore whatever was bound; bind(),
// bindBase() and bindRange() deliberately leave this buffer bound for the caller.
class Buffer {
    public:
        // Creates and fills a buffer object; returns null on any GL failure.
        static std::unique_ptr<Buffer> create(const BufferConfig& config) noexcept {
            if (config.size == 0) {log_error("Buffer: size must be greater than zero"); return nullptr;}

            unsigned int handle = 0;
            #if ENGINE_HAS_DSA
                glCreateBuffers(1, &handle);
            #else
                glGenBuffers(1, &handle);
            #endif
            if (!handle) {log_error("Buffer: failed to generate a handle"); return nullptr;}

            GlErrorHandler::clear();

            bool immutable = false;
            #if ENGINE_HAS_BUFFER_STORAGE
                immutable = config.immutable;
            #endif

            #if ENGINE_HAS_DSA
                #if ENGINE_HAS_BUFFER_STORAGE
                    if (immutable) {
                        glNamedBufferStorage(handle, static_cast<GLsizeiptr>(config.size),
                                             config.data, config.storageFlags);
                    } else
                #endif
                {
                    glNamedBufferData(handle, static_cast<GLsizeiptr>(config.size),
                                      config.data, config.usage);
                }
            #else
                const int previous = boundHandle(config.target);
                glBindBuffer(config.target, handle);
                #if ENGINE_HAS_BUFFER_STORAGE
                    if (immutable) {
                        glBufferStorage(config.target, static_cast<GLsizeiptr>(config.size),
                                        config.data, config.storageFlags);
                    } else
                #endif
                {
                    glBufferData(config.target, static_cast<GLsizeiptr>(config.size),
                                 config.data, config.usage);
                }
                glBindBuffer(config.target, static_cast<unsigned int>(previous));
            #endif

            if (GlErrorHandler::logErrors()) {glDeleteBuffers(1, &handle); return nullptr;}

            return std::unique_ptr<Buffer>(new Buffer(handle, config.target, config.size,
                                                      config.usage, immutable));
        }

        // Creates a buffer sized and filled from a vector of items.
        template <typename T>
        static std::unique_ptr<Buffer> from(unsigned int target, const std::vector<T>& items,
                                            unsigned int usage = GL_STATIC_DRAW) noexcept {
            BufferConfig config;
            config.target = target;
            config.size = items.size() * sizeof(T);
            config.data = items.data();
            config.usage = usage;
            return create(config);
        }

        // The GL buffer name.
        unsigned int id() const noexcept {return _handle;}
        // The target the buffer was created for.
        unsigned int target() const noexcept {return _target;}
        // Size in bytes as tracked engine-side.
        size_t size() const noexcept {return _size;}
        // True when created as immutable storage.
        bool isImmutable() const noexcept {return _immutable;}
        // True when the handle still names a live GL buffer.
        bool isValid() const noexcept {return _handle != 0 && glIsBuffer(_handle) == GL_TRUE;}

        // Binds to the creation target, and leaves it bound.
        void bind() const noexcept {glBindBuffer(_target, _handle);}
        // Binds to an arbitrary target, and leaves it bound.
        void bind(unsigned int target) const noexcept {glBindBuffer(target, _handle);}
        // Unbinds the creation target.
        void unbind() const noexcept {glBindBuffer(_target, 0);}

        #if ENGINE_HAS_BIND_BUFFER_BASE
            // True for targets with indexed binding points.
            static bool isIndexedTarget(unsigned int target) noexcept {
                switch (target) {
                    #if ENGINE_HAS_UNIFORM_BLOCKS
                        case GL_UNIFORM_BUFFER: return true;
                    #endif
                    #if ENGINE_HAS_SSBO
                        case GL_SHADER_STORAGE_BUFFER: return true;
                    #endif
                    #if ENGINE_HAS_ATOMIC_COUNTERS
                        case GL_ATOMIC_COUNTER_BUFFER: return true;
                    #endif
                    #if ENGINE_HAS_TRANSFORM_FEEDBACK
                        case GL_TRANSFORM_FEEDBACK_BUFFER: return true;
                    #endif
                    default: return false;
                }
            }

            // Binds the whole buffer to an indexed binding point of its own target.
            void bindBase(unsigned int index) const noexcept {
                if (!isIndexedTarget(_target)) {
                    log_error("Buffer::bindBase: target 0x", std::hex, _target, std::dec,
                              " has no indexed binding points");
                    return;
                }
                glBindBufferBase(_target, index, _handle);
            }

            // Binds the whole buffer to an indexed binding point of another target.
            void bindBase(unsigned int target, unsigned int index) const noexcept {
                glBindBufferBase(target, index, _handle);
            }

            // Binds a byte range of the buffer to an indexed binding point.
            void bindRange(unsigned int index, intptr_t offset, size_t length) const noexcept {
                if (!isIndexedTarget(_target)) {
                    log_error("Buffer::bindRange: target 0x", std::hex, _target, std::dec,
                              " has no indexed binding points");
                    return;
                }
                if (offset < 0 || static_cast<size_t>(offset) + length > _size) {
                    log_error("Buffer::bindRange out of range: offset ", offset, ", length ", length,
                              ", buffer size ", _size);
                    return;
                }
                glBindBufferRange(_target, index, _handle,
                                  static_cast<GLintptr>(offset), static_cast<GLsizeiptr>(length));
            }
        #endif

        // Uploads a byte range; rejects null data and out-of-range spans.
        bool subData(intptr_t offset, size_t length, const void* data) noexcept {
            if (!data) {log_error("Buffer::subData given a null pointer"); return false;}
            if (offset < 0 || static_cast<size_t>(offset) + length > _size) {
                log_error("Buffer::subData out of range: offset ", offset, ", length ", length,
                          ", buffer size ", _size);
                return false;
            }
            #if ENGINE_HAS_DSA
                glNamedBufferSubData(_handle, static_cast<GLintptr>(offset),
                                     static_cast<GLsizeiptr>(length), data);
            #else
                const int previous = boundHandle(_target);
                glBindBuffer(_target, _handle);
                glBufferSubData(_target, static_cast<GLintptr>(offset),
                                static_cast<GLsizeiptr>(length), data);
                glBindBuffer(_target, static_cast<unsigned int>(previous));
            #endif
            return true;
        }

        // Uploads a vector of items starting at a byte offset.
        template <typename T>
        bool setData(const std::vector<T>& items, intptr_t offset = 0) noexcept {
            return subData(offset, items.size() * sizeof(T), items.data());
        }

        // Reallocates mutable storage to a new size, optionally with initial data.
        bool resize(size_t newSize, const void* data = nullptr) noexcept {
            if (_immutable) {log_error("Buffer::resize called on immutable storage"); return false;}
            if (newSize == 0) {log_error("Buffer::resize needs a size greater than zero"); return false;}

            GlErrorHandler::clear();
            #if ENGINE_HAS_DSA
                glNamedBufferData(_handle, static_cast<GLsizeiptr>(newSize), data, _usage);
            #else
                const int previous = boundHandle(_target);
                glBindBuffer(_target, _handle);
                glBufferData(_target, static_cast<GLsizeiptr>(newSize), data, _usage);
                glBindBuffer(_target, static_cast<unsigned int>(previous));
            #endif
            if (glGetError() != GL_NO_ERROR) {log_error("Buffer::resize failed"); return false;}
            _size = newSize;
            return true;
        }

        #if ENGINE_HAS_MAP_BUFFER_RANGE
            // Maps a byte range for CPU access; returns null on failure.
            void* map(intptr_t offset, size_t length, GLbitfield access) noexcept {
                if (offset < 0 || static_cast<size_t>(offset) + length > _size) {
                    log_error("Buffer::map out of range"); return nullptr;
                }
                #if ENGINE_HAS_DSA
                    void* pointer = glMapNamedBufferRange(_handle, static_cast<GLintptr>(offset),
                                                          static_cast<GLsizeiptr>(length), access);
                #else
                    const int previous = boundHandle(_target);
                    glBindBuffer(_target, _handle);
                    void* pointer = glMapBufferRange(_target, static_cast<GLintptr>(offset),
                                                     static_cast<GLsizeiptr>(length), access);
                    glBindBuffer(_target, static_cast<unsigned int>(previous));
                #endif
                if (!pointer) log_error("Buffer::map failed");
                return pointer;
            }

            // Maps the whole buffer for CPU access.
            void* mapAll(GLbitfield access) noexcept {return map(0, _size, access);}

            // Flushes writes made through an explicitly-flushed mapping.
            void flushMappedRange(intptr_t offset, size_t length) noexcept {
                #if ENGINE_HAS_DSA
                    glFlushMappedNamedBufferRange(_handle, static_cast<GLintptr>(offset),
                                                  static_cast<GLsizeiptr>(length));
                #else
                    const int previous = boundHandle(_target);
                    glBindBuffer(_target, _handle);
                    glFlushMappedBufferRange(_target, static_cast<GLintptr>(offset),
                                             static_cast<GLsizeiptr>(length));
                    glBindBuffer(_target, static_cast<unsigned int>(previous));
                #endif
            }

            // Unmaps the buffer; false means the data store was lost.
            bool unmap() noexcept {
                #if ENGINE_HAS_DSA
                    return glUnmapNamedBuffer(_handle) == GL_TRUE;
                #else
                    const int previous = boundHandle(_target);
                    glBindBuffer(_target, _handle);
                    const bool ok = glUnmapBuffer(_target) == GL_TRUE;
                    glBindBuffer(_target, static_cast<unsigned int>(previous));
                    return ok;
                #endif
            }
        #endif

        // ---- readback and queries ----

        // Reads a byte range back to CPU memory; rejects out-of-range spans.
        bool getSubData(intptr_t offset, size_t length, void* out) const noexcept {
            if (!out) {log_error("Buffer::getSubData given a null pointer"); return false;}
            if (offset < 0 || static_cast<size_t>(offset) + length > _size) {
                log_error("Buffer::getSubData out of range"); return false;
            }
            #if ENGINE_HAS_DSA
                glGetNamedBufferSubData(_handle, static_cast<GLintptr>(offset),
                                        static_cast<GLsizeiptr>(length), out);
            #else
                const int previous = boundHandle(_target);
                glBindBuffer(_target, _handle);
                glGetBufferSubData(_target, static_cast<GLintptr>(offset),
                                   static_cast<GLsizeiptr>(length), out);
                glBindBuffer(_target, static_cast<unsigned int>(previous));
            #endif
            return true;
        }

        // Reads items back into a vector; empty on failure or bad offset.
        template <typename T>
        std::vector<T> read(size_t count = 0, intptr_t offset = 0) const noexcept {
            if (offset < 0 || static_cast<size_t>(offset) > _size) return {};
            const size_t items = count > 0 ? count : (_size - static_cast<size_t>(offset)) / sizeof(T);
            std::vector<T> out(items);
            if (!getSubData(offset, items * sizeof(T), out.data())) out.clear();
            return out;
        }

        // A glGetBufferParameteriv value for this buffer.
        int parameter(unsigned int name) const noexcept {
            int value = 0;
            #if ENGINE_HAS_DSA
                glGetNamedBufferParameteriv(_handle, name, &value);
            #else
                const int previous = boundHandle(_target);
                glBindBuffer(_target, _handle);
                glGetBufferParameteriv(_target, name, &value);
                glBindBuffer(_target, static_cast<unsigned int>(previous));
            #endif
            return value;
        }

        // Size in bytes as reported by the driver.
        size_t sizeFromGL() const noexcept {return static_cast<size_t>(parameter(GL_BUFFER_SIZE));}
        // True while the buffer is mapped.
        bool isMapped() const noexcept {return parameter(GL_BUFFER_MAPPED) == GL_TRUE;}

        // The CPU pointer of the current mapping, or null when unmapped.
        void* mappedPointer() const noexcept {
            void* pointer = nullptr;
            #if ENGINE_HAS_DSA
                glGetNamedBufferPointerv(_handle, GL_BUFFER_MAP_POINTER, &pointer);
            #else
                const int previous = boundHandle(_target);
                glBindBuffer(_target, _handle);
                glGetBufferPointerv(_target, GL_BUFFER_MAP_POINTER, &pointer);
                glBindBuffer(_target, static_cast<unsigned int>(previous));
            #endif
            return pointer;
        }

        #if ENGINE_HAS_CLEAR_BUFFER
            // Fills the whole buffer by reinterpreting `data` in `internalFormat`.
            bool clear(unsigned int internalFormat, unsigned int format,
                       unsigned int type, const void* data = nullptr) noexcept {
                GlErrorHandler::clear();
                #if ENGINE_HAS_DSA
                    glClearNamedBufferData(_handle, internalFormat, format, type, data);
                #else
                    const int previous = boundHandle(_target);
                    glBindBuffer(_target, _handle);
                    glClearBufferData(_target, internalFormat, format, type, data);
                    glBindBuffer(_target, static_cast<unsigned int>(previous));
                #endif
                if (GlErrorHandler::logErrors()) {log_error("Buffer::clear failed"); return false;}
                return true;
            }

            // Fills a byte range by reinterpreting `data` in `internalFormat`.
            bool clearRange(unsigned int internalFormat, intptr_t offset, size_t length,
                            unsigned int format, unsigned int type, const void* data = nullptr) noexcept {
                if (offset < 0 || static_cast<size_t>(offset) + length > _size) {
                    log_error("Buffer::clearRange out of range"); return false;
                }
                GlErrorHandler::clear();
                #if ENGINE_HAS_DSA
                    glClearNamedBufferSubData(_handle, internalFormat,
                                              static_cast<GLintptr>(offset),
                                              static_cast<GLsizeiptr>(length), format, type, data);
                #else
                    const int previous = boundHandle(_target);
                    glBindBuffer(_target, _handle);
                    glClearBufferSubData(_target, internalFormat,
                                         static_cast<GLintptr>(offset),
                                         static_cast<GLsizeiptr>(length), format, type, data);
                    glBindBuffer(_target, static_cast<unsigned int>(previous));
                #endif
                if (GlErrorHandler::logErrors()) {log_error("Buffer::clearRange failed"); return false;}
                return true;
            }
        #endif

        #if ENGINE_HAS_INVALIDATE
            // Tells the driver the whole contents may be discarded.
            void invalidate() noexcept {glInvalidateBufferData(_handle);}

            // Tells the driver a byte range may be discarded.
            void invalidateRange(intptr_t offset, size_t length) noexcept {
                glInvalidateBufferSubData(_handle, static_cast<GLintptr>(offset),
                                          static_cast<GLsizeiptr>(length));
            }
        #endif

        #if ENGINE_HAS_COPY_BUFFER
            // Copies a byte range between two buffers on the GPU.
            static bool copy(const Buffer& source, Buffer& destination,
                             intptr_t sourceOffset, intptr_t destOffset, size_t length) noexcept {
                if (sourceOffset < 0 || destOffset < 0) {
                    log_error("Buffer::copy given a negative offset"); return false;
                }
                if (static_cast<size_t>(sourceOffset) + length > source.size() ||
                    static_cast<size_t>(destOffset) + length > destination.size()) {
                    log_error("Buffer::copy out of range"); return false;
                }
                #if ENGINE_HAS_DSA
                    glCopyNamedBufferSubData(source.id(), destination.id(),
                                             static_cast<GLintptr>(sourceOffset),
                                             static_cast<GLintptr>(destOffset),
                                             static_cast<GLsizeiptr>(length));
                #else
                    const int previousRead = boundHandle(GL_COPY_READ_BUFFER);
                    const int previousWrite = boundHandle(GL_COPY_WRITE_BUFFER);
                    glBindBuffer(GL_COPY_READ_BUFFER, source.id());
                    glBindBuffer(GL_COPY_WRITE_BUFFER, destination.id());
                    glCopyBufferSubData(GL_COPY_READ_BUFFER, GL_COPY_WRITE_BUFFER,
                                        static_cast<GLintptr>(sourceOffset),
                                        static_cast<GLintptr>(destOffset),
                                        static_cast<GLsizeiptr>(length));
                    glBindBuffer(GL_COPY_READ_BUFFER, static_cast<unsigned int>(previousRead));
                    glBindBuffer(GL_COPY_WRITE_BUFFER, static_cast<unsigned int>(previousWrite));
                #endif
                return true;
            }
        #endif

        ~Buffer() noexcept {if (_handle) glDeleteBuffers(1, &_handle);}

        Buffer(const Buffer&) = delete;
        Buffer& operator=(const Buffer&) = delete;

        // Movable: the GL name is the only owned resource and it transfers whole.
        Buffer(Buffer&& other) noexcept
            : _handle(std::exchange(other._handle, 0)),
              _target(other._target), _size(other._size),
              _usage(other._usage), _immutable(other._immutable) {}

        // Deletes this buffer's name before adopting the other's.
        Buffer& operator=(Buffer&& other) noexcept {
            if (this != &other) {
                if (_handle) glDeleteBuffers(1, &_handle);
                _handle = std::exchange(other._handle, 0);
                _target = other._target;
                _size = other._size;
                _usage = other._usage;
                _immutable = other._immutable;
            }
            return *this;
        }
    private:
        Buffer(unsigned int handle, unsigned int target, size_t size,
               unsigned int usage, bool immutable) noexcept
            : _handle(handle), _target(target), _size(size),
              _usage(usage), _immutable(immutable) {}

        // The buffer currently bound to a target, or 0 when the target is unknown.
        static int boundHandle(unsigned int target) noexcept {
            const GLenum query = bindingQueryFor(target);
            if (query == 0) return 0;
            int previous = 0;
            glGetIntegerv(query, &previous);
            return previous;
        }

        // The binding query enum for a target; 0 for targets without one here,
        // which makes the save/restore fall back to binding 0 (never a wrong handle).
        static GLenum bindingQueryFor(unsigned int target) noexcept {
            switch (target) {
                case GL_ARRAY_BUFFER:          return GL_ARRAY_BUFFER_BINDING;
                case GL_ELEMENT_ARRAY_BUFFER:  return GL_ELEMENT_ARRAY_BUFFER_BINDING;
                case GL_PIXEL_PACK_BUFFER:     return GL_PIXEL_PACK_BUFFER_BINDING;
                case GL_PIXEL_UNPACK_BUFFER:   return GL_PIXEL_UNPACK_BUFFER_BINDING;
                #if ENGINE_HAS_TRANSFORM_FEEDBACK
                    case GL_TRANSFORM_FEEDBACK_BUFFER: return GL_TRANSFORM_FEEDBACK_BUFFER_BINDING;
                #endif
                #if ENGINE_HAS_COPY_BUFFER
                    case GL_COPY_READ_BUFFER:  return GL_COPY_READ_BUFFER_BINDING;
                    case GL_COPY_WRITE_BUFFER: return GL_COPY_WRITE_BUFFER_BINDING;
                #endif
                #if ENGINE_HAS_TEXTURE_BUFFER
                    // GL_TEXTURE_BUFFER_BINDING is the buffer bound to the texture-buffer
                    // target; GL_TEXTURE_BINDING_BUFFER is the texture object, not this.
                    case GL_TEXTURE_BUFFER:    return GL_TEXTURE_BUFFER_BINDING;
                #endif
                #if ENGINE_HAS_UNIFORM_BLOCKS
                    case GL_UNIFORM_BUFFER:    return GL_UNIFORM_BUFFER_BINDING;
                #endif
                #if ENGINE_HAS_DRAW_INDIRECT
                    case GL_DRAW_INDIRECT_BUFFER: return GL_DRAW_INDIRECT_BUFFER_BINDING;
                #endif
                #if ENGINE_HAS_ATOMIC_COUNTERS
                    case GL_ATOMIC_COUNTER_BUFFER: return GL_ATOMIC_COUNTER_BUFFER_BINDING;
                #endif
                #if ENGINE_HAS_SSBO
                    case GL_SHADER_STORAGE_BUFFER: return GL_SHADER_STORAGE_BUFFER_BINDING;
                #endif
                #if ENGINE_HAS_COMPUTE_SHADER
                    case GL_DISPATCH_INDIRECT_BUFFER: return GL_DISPATCH_INDIRECT_BUFFER_BINDING;
                #endif
                default:                       return 0;
            }
        }

        unsigned int _handle = 0;
        unsigned int _target = GL_ARRAY_BUFFER;
        size_t _size = 0;
        unsigned int _usage = GL_STATIC_DRAW;
        bool _immutable = false;
};

#endif