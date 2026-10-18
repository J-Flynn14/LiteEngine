#pragma once

#include <memory>
#include <utility>
#include <vector>

#include "engine_config.h"
#include "logs.h"
#include "texture.h"
#include "error_handling/gl_error_handler.h"

#if ENGINE_HAS_FRAMEBUFFER

class Renderbuffer {
    public:
        // Creates a renderbuffer, multisampled when samples > 1; null on failure.
        static std::unique_ptr<Renderbuffer> create(int width, int height,
                                                    unsigned int internalFormat, int samples = 1) noexcept {
            if (width <= 0 || height <= 0) {
                log_error("Renderbuffer needs positive dimensions, got ", width, "x", height);
                return nullptr;
            }

            unsigned int handle = 0;
            glGenRenderbuffers(1, &handle);
            if (!handle) {log_error("Renderbuffer: failed to generate a handle"); return nullptr;}

            int previous = 0;
            glGetIntegerv(GL_RENDERBUFFER_BINDING, &previous);
            glBindRenderbuffer(GL_RENDERBUFFER, handle);
            GlErrorHandler::clear();

            #if ENGINE_HAS_MULTISAMPLE_RB
                if (samples > 1) {
                    glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples,
                                                     internalFormat, width, height);
                } else
            #else
                if (samples > 1) log_warn("Renderbuffer: multisampling needs GL 3.0");
            #endif
            {
                glRenderbufferStorage(GL_RENDERBUFFER, internalFormat, width, height);
            }

            glBindRenderbuffer(GL_RENDERBUFFER, static_cast<unsigned int>(previous));

            if (GlErrorHandler::logErrors()) {glDeleteRenderbuffers(1, &handle); return nullptr;}

            return std::unique_ptr<Renderbuffer>(
                new Renderbuffer(handle, width, height, internalFormat, samples));
        }

        // The GL renderbuffer name.
        unsigned int id() const noexcept {return _handle;}
        // Width in pixels.
        int width() const noexcept {return _width;}
        // Height in pixels.
        int height() const noexcept {return _height;}
        // The internal format the storage was created with.
        unsigned int internalFormat() const noexcept {return _internalFormat;}
        // Sample count; 1 means single-sampled.
        int samples() const noexcept {return _samples;}
        // True when the handle still names a live GL renderbuffer.
        bool isValid() const noexcept {return _handle != 0 && glIsRenderbuffer(_handle) == GL_TRUE;}

        // Binds this renderbuffer.
        void bind() const noexcept {glBindRenderbuffer(GL_RENDERBUFFER, _handle);}
        // Unbinds any renderbuffer.
        static void unbind() noexcept {glBindRenderbuffer(GL_RENDERBUFFER, 0);}

        // Reallocates storage at a new size, keeping format and sample count.
        bool resize(int width, int height) noexcept {
            if (width == _width && height == _height) return true;
            auto replacement = create(width, height, _internalFormat, _samples);
            if (!replacement) return false;
            *this = std::move(*replacement);
            return true;
        }

        ~Renderbuffer() noexcept {if (_handle) glDeleteRenderbuffers(1, &_handle);}

        Renderbuffer(const Renderbuffer&) = delete;
        Renderbuffer& operator=(const Renderbuffer&) = delete;

        Renderbuffer(Renderbuffer&& other) noexcept
            : _handle(std::exchange(other._handle, 0)), _width(other._width),
              _height(other._height), _internalFormat(other._internalFormat),
              _samples(other._samples) {}

        Renderbuffer& operator=(Renderbuffer&& other) noexcept {
            if (this != &other) {
                if (_handle) glDeleteRenderbuffers(1, &_handle);
                _handle = std::exchange(other._handle, 0);
                _width = other._width;
                _height = other._height;
                _internalFormat = other._internalFormat;
                _samples = other._samples;
            }
            return *this;
        }
    private:
        Renderbuffer(unsigned int handle, int width, int height,
                     unsigned int internalFormat, int samples) noexcept
            : _handle(handle), _width(width), _height(height),
              _internalFormat(internalFormat), _samples(samples) {}

        unsigned int _handle = 0;
        int _width = 0;
        int _height = 0;
        unsigned int _internalFormat = GL_DEPTH24_STENCIL8;
        int _samples = 1;
};

struct FramebufferAttachment {
    unsigned int point = GL_COLOR_ATTACHMENT0;
    unsigned int internalFormat = GL_RGBA8;

    // Owned attachments become renderbuffers when set, textures otherwise.
    bool asRenderbuffer = false;

    // Optional overrides for an owned texture. Width, height and internal
    // format are always taken from the framebuffer and this attachment.
    bool overrideSampler = false;
    int wrapS = GL_CLAMP_TO_EDGE;
    int wrapT = GL_CLAMP_TO_EDGE;
    int minFilter = GL_LINEAR;
    int magFilter = GL_LINEAR;

    // Depth attachments only: request GL_LINEAR plus compare-to-ref so the result
    // can be sampled as sampler2DShadow with hardware PCF. Ignored with overrideSampler.
    bool shadowSampler = false;
    int compareFunc = GL_LEQUAL;

    // Non-owning. The referenced object must outlive the framebuffer.
    Texture* externalTexture = nullptr;
    Renderbuffer* externalRenderbuffer = nullptr;

    // For layered external textures (cube/array/3D): which face or layer to attach.
    int layer = 0;

    int mipLevel = 0;
};

struct FramebufferConfig {
    int width = 0;
    int height = 0;
    int samples = 1;
    std::vector<FramebufferAttachment> attachments;
    bool hasColour = true;
};

class Framebuffer {
    public:
        // Builds a framebuffer and its owned attachments; null unless complete.
        static std::unique_ptr<Framebuffer> create(const FramebufferConfig& config) noexcept {
            if (config.width <= 0 || config.height <= 0) {
                log_error("Framebuffer needs positive dimensions, got ",
                          config.width, "x", config.height);
                return nullptr;
            }
            if (config.attachments.empty()) {
                log_error("Framebuffer: no attachments given");
                return nullptr;
            }

            unsigned int handle = 0;
            #if ENGINE_HAS_FRAMEBUFFER_DSA
                glCreateFramebuffers(1, &handle);
            #else
                glGenFramebuffers(1, &handle);
            #endif
            if (!handle) {log_error("Framebuffer: failed to generate a handle"); return nullptr;}

            std::unique_ptr<Framebuffer> framebuffer(
                new Framebuffer(handle, config.width, config.height, config.samples));

            const int previous = boundFramebuffer();
            glBindFramebuffer(GL_FRAMEBUFFER, handle);
            GlErrorHandler::clear();

            std::vector<unsigned int> drawBuffers;

            for (const FramebufferAttachment& attachment : config.attachments) {
                if (attachment.externalTexture && attachment.externalRenderbuffer) {
                    log_error("Framebuffer: an attachment names both a texture and a renderbuffer");
                    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(previous));
                    return nullptr;
                }

                if (!framebuffer->attachOne(attachment, config)) {
                    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(previous));
                    return nullptr;
                }

                if (attachment.point >= GL_COLOR_ATTACHMENT0 &&
                    attachment.point < GL_COLOR_ATTACHMENT0 + 32) {
                    drawBuffers.push_back(attachment.point);
                }
            }

            if (!config.hasColour || drawBuffers.empty()) {
                glDrawBuffer(GL_NONE);
                glReadBuffer(GL_NONE);
            } else {
                glDrawBuffers(static_cast<int>(drawBuffers.size()), drawBuffers.data());
                framebuffer->_drawBuffers = std::move(drawBuffers);
            }

            const unsigned int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(previous));

            if (status != GL_FRAMEBUFFER_COMPLETE) {
                log_error("Framebuffer incomplete: ", statusText(status));
                return nullptr;
            }

            if (GlErrorHandler::logErrors()) {
                log_error("Framebuffer: GL errors during setup");
                return nullptr;
            }

            log_info("Framebuffer created: ", config.width, "x", config.height,
                     " with ", config.attachments.size(), " attachment(s)");
            return framebuffer;
        }

        // Convenience: one colour texture plus a packed depth/stencil renderbuffer.
        static std::unique_ptr<Framebuffer> createColourDepth(int width, int height,
                                                              unsigned int colourFormat = GL_RGBA8,
                                                              int samples = 1) noexcept {
            FramebufferConfig config;
            config.width = width;
            config.height = height;
            config.samples = samples;
            FramebufferAttachment colour;
            colour.point = GL_COLOR_ATTACHMENT0;
            colour.internalFormat = colourFormat;
            FramebufferAttachment depth;
            depth.point = GL_DEPTH_STENCIL_ATTACHMENT;
            depth.internalFormat = GL_DEPTH24_STENCIL8;
            depth.asRenderbuffer = true;
            config.attachments = {colour, depth};
            return create(config);
        }

        // Convenience: a depth-only framebuffer for shadow maps.
        static std::unique_ptr<Framebuffer> createDepthOnly(int width, int height,
                                                            unsigned int depthFormat = GL_DEPTH_COMPONENT24) noexcept {
            FramebufferConfig config;
            config.width = width;
            config.height = height;
            config.hasColour = false;
            FramebufferAttachment depth;
            depth.point = GL_DEPTH_ATTACHMENT;
            depth.internalFormat = depthFormat;
            config.attachments = {depth};
            return create(config);
        }

        // The GL framebuffer name.
        unsigned int id() const noexcept {return _handle;}
        // Width in pixels.
        int width() const noexcept {return _width;}
        // Height in pixels.
        int height() const noexcept {return _height;}
        // Sample count; 1 means single-sampled.
        int samples() const noexcept {return _samples;}
        // True when this framebuffer is multisampled.
        bool isMultisample() const noexcept {return _samples > 1;}
        // True when the handle still names a live GL framebuffer.
        bool isValid() const noexcept {return _handle != 0 && glIsFramebuffer(_handle) == GL_TRUE;}

        // Binds this framebuffer and sets the viewport to its size. The caller
        // must restore the window viewport afterwards (Window::restoreViewport).
        void bind() const noexcept {
            glBindFramebuffer(GL_FRAMEBUFFER, _handle);
            glViewport(0, 0, _width, _height);
        }

        // Binds this framebuffer without touching the viewport.
        void bindRaw() const noexcept {glBindFramebuffer(GL_FRAMEBUFFER, _handle);}

        // Binds the default framebuffer; the viewport is left as bind() set it.
        static void unbind() noexcept {glBindFramebuffer(GL_FRAMEBUFFER, 0);}

        #if ENGINE_HAS_BLIT_FRAMEBUFFER
            // Binds this framebuffer for reading only.
            void bindRead() const noexcept {glBindFramebuffer(GL_READ_FRAMEBUFFER, _handle);}
            // Binds this framebuffer for drawing only.
            void bindDraw() const noexcept {glBindFramebuffer(GL_DRAW_FRAMEBUFFER, _handle);}

            // Blits this framebuffer into another, scaling into an explicit
            // destination size. The default framebuffer's size is the window's and
            // this class cannot query it, so blitting to the screen wants this.
            bool blitToRect(const Framebuffer* destination, int destWidth, int destHeight,
                            GLbitfield mask = GL_COLOR_BUFFER_BIT,
                            unsigned int filter = GL_NEAREST) const noexcept {
                if (destWidth <= 0 || destHeight <= 0) {
                    log_error("blitToRect needs positive destination dimensions, got ",
                              destWidth, "x", destHeight);
                    return false;
                }

                if (filter == GL_LINEAR && (mask & (GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT))) {
                    log_error("blitTo: depth and stencil blits must use GL_NEAREST");
                    return false;
                }

                int previousRead = 0, previousDraw = 0;
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousRead);
                glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDraw);

                glBindFramebuffer(GL_READ_FRAMEBUFFER, _handle);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, destination ? destination->_handle : 0);
                glBlitFramebuffer(0, 0, _width, _height, 0, 0, destWidth, destHeight, mask, filter);

                glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<unsigned int>(previousRead));
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<unsigned int>(previousDraw));
                return true;
            }

            // Blits this framebuffer into another (null = the default framebuffer).
            // The destination is assumed to match this framebuffer's size; when it
            // does not -- blitting to the window is the usual case -- use blitToRect.
            bool blitTo(const Framebuffer* destination,
                        GLbitfield mask = GL_COLOR_BUFFER_BIT,
                        unsigned int filter = GL_NEAREST) const noexcept {
                return blitToRect(destination,
                                  destination ? destination->_width : _width,
                                  destination ? destination->_height : _height,
                                  mask, filter);
            }
        #endif

        // ---- attachment access ----

        // The texture at an attachment point, or null.
        Texture* texture(unsigned int point = GL_COLOR_ATTACHMENT0) noexcept {
            return const_cast<Texture*>(findTexture(point));
        }

        // The texture at an attachment point, or null.
        const Texture* texture(unsigned int point = GL_COLOR_ATTACHMENT0) const noexcept {
            return findTexture(point);
        }

        // The renderbuffer at an attachment point, or null.
        Renderbuffer* renderbuffer(unsigned int point) noexcept {
            return const_cast<Renderbuffer*>(findRenderbuffer(point));
        }

        // The renderbuffer at an attachment point, or null.
        const Renderbuffer* renderbuffer(unsigned int point) const noexcept {
            return findRenderbuffer(point);
        }

        // The texture at colour attachment `index`, or null.
        Texture* colourTexture(int index = 0) noexcept {
            return texture(GL_COLOR_ATTACHMENT0 + static_cast<unsigned int>(index));
        }

        // The texture at colour attachment `index`, or null.
        const Texture* colourTexture(int index = 0) const noexcept {
            return texture(GL_COLOR_ATTACHMENT0 + static_cast<unsigned int>(index));
        }

        // The depth (or depth/stencil) texture, or null.
        Texture* depthTexture() noexcept {
            return const_cast<Texture*>(findDepthTexture());
        }

        // The depth (or depth/stencil) texture, or null.
        const Texture* depthTexture() const noexcept {return findDepthTexture();}

        // Number of colour attachments in the draw-buffer list.
        size_t colourAttachmentCount() const noexcept {return _drawBuffers.size();}

        // ---- operations ----

        // Binds this framebuffer and clears it to the given colour.
        void clear(float r = 0.0f, float g = 0.0f, float b = 0.0f, float a = 1.0f,
                   GLbitfield mask = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT) const noexcept {
            bind();
            glClearColor(r, g, b, a);
            glClear(mask);
        }

        // Resizes every owned attachment and re-checks completeness.
        bool resize(int width, int height) noexcept {
            if (width <= 0 || height <= 0) {log_error("Framebuffer::resize needs positive dimensions"); return false;}
            if (width == _width && height == _height) return true;

            for (AttachmentSlot& slot : _slots) {
                if (!slot.owned) {
                    log_warn("Framebuffer::resize: attachment 0x", std::hex, slot.point, std::dec,
                             " is external and was not resized");
                    continue;
                }
                if (slot.texture && !slot.texture->resize(width, height)) return false;
                if (slot.renderbuffer && !slot.renderbuffer->resize(width, height)) return false;
            }

            _width = width;
            _height = height;

            #if ENGINE_HAS_FRAMEBUFFER_DSA
                for (const AttachmentSlot& slot : _slots) {
                    if (slot.texture) {
                        attachTextureNamed(_handle, slot.point, *slot.texture, slot.layer, slot.mipLevel);
                    } else if (slot.renderbuffer) {
                        glNamedFramebufferRenderbuffer(_handle, slot.point, GL_RENDERBUFFER,
                                                       slot.renderbuffer->id());
                    }
                }
                const unsigned int status = glCheckNamedFramebufferStatus(_handle, GL_FRAMEBUFFER);
            #else
                const int previous = boundFramebuffer();
                glBindFramebuffer(GL_FRAMEBUFFER, _handle);
                for (const AttachmentSlot& slot : _slots) {
                    if (slot.texture) {
                        attachTextureBound(slot.point, *slot.texture, slot.layer, slot.mipLevel);
                    } else if (slot.renderbuffer) {
                        glFramebufferRenderbuffer(GL_FRAMEBUFFER, slot.point, GL_RENDERBUFFER,
                                                  slot.renderbuffer->id());
                    }
                }
                const unsigned int status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(previous));
            #endif

            if (status != GL_FRAMEBUFFER_COMPLETE) {
                log_error("Framebuffer incomplete after resize: ", statusText(status));
                return false;
            }
            return true;
        }

        // Reads the whole colour attachment back to CPU memory.
        bool readPixels(void* out, size_t outCapacity, unsigned int format = GL_RGBA,
                        unsigned int type = GL_UNSIGNED_BYTE,
                        unsigned int point = GL_COLOR_ATTACHMENT0) const noexcept {
            if (!out) {log_error("Framebuffer::readPixels given a null pointer"); return false;}
            if (isMultisample()) {
                log_error("Framebuffer::readPixels cannot read a multisample framebuffer; blit it first");
                return false;
            }
            const int components = texture_detail::componentsForFormat(format);
            const int bytesPerComponent = texture_detail::bytesForType(type);
            if (components > 0 && bytesPerComponent > 0) {
                const size_t required = static_cast<size_t>(_width) * static_cast<size_t>(_height)
                                       * static_cast<size_t>(components) * static_cast<size_t>(bytesPerComponent);
                if (outCapacity < required) {
                    log_error("Framebuffer::readPixels: buffer is ", outCapacity, " bytes, need ", required,
                              " for a ", _width, "x", _height, " image");
                    return false;
                }
            } else {
                log_warn("Framebuffer::readPixels: could not validate buffer size for this format/type "
                         "combination; proceeding on trust");
            }

            // The size check above assumes tightly packed rows, so pack alignment
            // must actually be 1 while GL writes into the destination buffer.
            int previousPack = 4;
            glGetIntegerv(GL_PACK_ALIGNMENT, &previousPack);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);

            const int previous = boundFramebuffer();
            glBindFramebuffer(GL_FRAMEBUFFER, _handle);

            // The read buffer is per-framebuffer state that outlives this call, so
            // it is restored rather than left pointing at whatever was read here.
            int previousRead = GL_COLOR_ATTACHMENT0;
            glGetIntegerv(GL_READ_BUFFER, &previousRead);
            glReadBuffer(point);
            glReadPixels(0, 0, _width, _height, format, type, out);
            glReadBuffer(static_cast<unsigned int>(previousRead));

            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(previous));

            glPixelStorei(GL_PACK_ALIGNMENT, previousPack);
            return true;
        }

        // The framebuffer's completeness status.
        unsigned int status() const noexcept {
            #if ENGINE_HAS_FRAMEBUFFER_DSA
                return glCheckNamedFramebufferStatus(_handle, GL_FRAMEBUFFER);
            #else
                const int previous = boundFramebuffer();
                glBindFramebuffer(GL_FRAMEBUFFER, _handle);
                const unsigned int result = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(previous));
                return result;
            #endif
        }

        // True when the framebuffer is complete and renderable.
        bool isComplete() const noexcept {return status() == GL_FRAMEBUFFER_COMPLETE;}

        // Human-readable form of a framebuffer status code.
        static const char* statusText(unsigned int status) noexcept {
            switch (status) {
                case GL_FRAMEBUFFER_COMPLETE: return "complete";
                case GL_FRAMEBUFFER_UNDEFINED: return "undefined";
                case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT: return "incomplete attachment";
                case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT: return "missing attachment";
                case GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER: return "incomplete draw buffer";
                case GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER: return "incomplete read buffer";
                case GL_FRAMEBUFFER_UNSUPPORTED: return "unsupported format combination";
                case GL_FRAMEBUFFER_INCOMPLETE_MULTISAMPLE: return "inconsistent sample counts";
                #ifdef GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS
                    case GL_FRAMEBUFFER_INCOMPLETE_LAYER_TARGETS: return "inconsistent layer targets";
                #endif
                default: return "unknown status";
            }
        }

        #if ENGINE_HAS_LAYERED_ATTACH
            // Attaches every layer of a texture at once, for layered rendering.
            // Do not mix layered and plain 2D attachments on one framebuffer.
            bool attachLayered(unsigned int point, const Texture& texture, int mipLevel = 0) noexcept {
                #if ENGINE_HAS_FRAMEBUFFER_DSA
                    glNamedFramebufferTexture(_handle, point, texture.id(), mipLevel);
                    const unsigned int result = glCheckNamedFramebufferStatus(_handle, GL_FRAMEBUFFER);
                #else
                    const int previous = boundFramebuffer();
                    glBindFramebuffer(GL_FRAMEBUFFER, _handle);
                    glFramebufferTexture(GL_FRAMEBUFFER, point, texture.id(), mipLevel);
                    const unsigned int result = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(previous));
                #endif

                if (result != GL_FRAMEBUFFER_COMPLETE) {
                    log_error("attachLayered left the framebuffer incomplete: ", statusText(result));
                    return false;
                }
                return true;
            }

            // Attaches one layer or face of an array, 3D or cube texture.
            bool attachLayer(unsigned int point, const Texture& texture, int layer, int mipLevel = 0) noexcept {
                #if ENGINE_HAS_FRAMEBUFFER_DSA
                    glNamedFramebufferTextureLayer(_handle, point, texture.id(), mipLevel, layer);
                    const unsigned int result = glCheckNamedFramebufferStatus(_handle, GL_FRAMEBUFFER);
                #else
                    const int previous = boundFramebuffer();
                    glBindFramebuffer(GL_FRAMEBUFFER, _handle);
                    glFramebufferTextureLayer(GL_FRAMEBUFFER, point, texture.id(), mipLevel, layer);
                    const unsigned int result = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(previous));
                #endif

                if (result != GL_FRAMEBUFFER_COMPLETE) {
                    log_error("attachLayer left the framebuffer incomplete: ", statusText(result));
                    return false;
                }
                return true;
            }
        #endif

        #if ENGINE_HAS_FB_PARAMETER
            // Creates a framebuffer with no attachments, sized by default parameters.
            static std::unique_ptr<Framebuffer> createEmpty(int width, int height,
                                                            int layers = 0, int samples = 0) noexcept {
                unsigned int handle = 0;
                #if ENGINE_HAS_FRAMEBUFFER_DSA
                    glCreateFramebuffers(1, &handle);
                #else
                    glGenFramebuffers(1, &handle);
                #endif
                if (!handle) {log_error("Framebuffer::createEmpty failed to generate a handle"); return nullptr;}

                const int previous = boundFramebuffer();
                glBindFramebuffer(GL_FRAMEBUFFER, handle);
                glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_WIDTH, width);
                glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_HEIGHT, height);
                if (layers > 0) glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_LAYERS, layers);
                if (samples > 0) glFramebufferParameteri(GL_FRAMEBUFFER, GL_FRAMEBUFFER_DEFAULT_SAMPLES, samples);

                const unsigned int result = glCheckFramebufferStatus(GL_FRAMEBUFFER);
                glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(previous));

                if (result != GL_FRAMEBUFFER_COMPLETE) {
                    log_error("Framebuffer::createEmpty incomplete: ", statusText(result));
                    glDeleteFramebuffers(1, &handle);
                    return nullptr;
                }
                return std::unique_ptr<Framebuffer>(
                    new Framebuffer(handle, width, height, samples > 0 ? samples : 1));
            }

            // Sets a default framebuffer parameter on an attachment-less framebuffer.
            void setParameter(unsigned int name, int value) const noexcept {
                const int previous = boundFramebuffer();
                glBindFramebuffer(GL_FRAMEBUFFER, _handle);
                glFramebufferParameteri(GL_FRAMEBUFFER, name, value);
                glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(previous));
            }
        #endif

        // Chooses which attachment the next readPixels or blit reads from.
        void setReadBuffer(unsigned int point) const noexcept {
            #if ENGINE_HAS_FRAMEBUFFER_DSA
                glNamedFramebufferReadBuffer(_handle, point);
            #else
                const int previous = boundFramebuffer();
                glBindFramebuffer(GL_FRAMEBUFFER, _handle);
                glReadBuffer(point);
                glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(previous));
            #endif
        }

        // Reorders or masks the colour attachments written by fragment outputs.
        void setDrawBuffers(const std::vector<unsigned int>& points) const noexcept {
            #if ENGINE_HAS_FRAMEBUFFER_DSA
                if (points.empty()) {
                    glNamedFramebufferDrawBuffer(_handle, GL_NONE);
                } else {
                    glNamedFramebufferDrawBuffers(_handle, static_cast<int>(points.size()), points.data());
                }
            #else
                const int previous = boundFramebuffer();
                glBindFramebuffer(GL_FRAMEBUFFER, _handle);
                if (points.empty()) {
                    glDrawBuffer(GL_NONE);
                } else {
                    glDrawBuffers(static_cast<int>(points.size()), points.data());
                }
                glBindFramebuffer(GL_FRAMEBUFFER, static_cast<unsigned int>(previous));
            #endif
        }

        ~Framebuffer() noexcept {if (_handle) glDeleteFramebuffers(1, &_handle);}

        Framebuffer(const Framebuffer&) = delete;
        Framebuffer& operator=(const Framebuffer&) = delete;

        Framebuffer(Framebuffer&& other) noexcept
            : _handle(std::exchange(other._handle, 0)), _width(other._width),
              _height(other._height), _samples(other._samples),
              _slots(std::move(other._slots)),
              _ownedTextures(std::move(other._ownedTextures)),
              _ownedRenderbuffers(std::move(other._ownedRenderbuffers)),
              _drawBuffers(std::move(other._drawBuffers)) {}

        Framebuffer& operator=(Framebuffer&& other) noexcept {
            if (this != &other) {
                if (_handle) glDeleteFramebuffers(1, &_handle);
                _handle = std::exchange(other._handle, 0);
                _width = other._width;
                _height = other._height;
                _samples = other._samples;
                _slots = std::move(other._slots);
                _ownedTextures = std::move(other._ownedTextures);
                _ownedRenderbuffers = std::move(other._ownedRenderbuffers);
                _drawBuffers = std::move(other._drawBuffers);
            }
            return *this;
        }
    private:
        struct AttachmentSlot {
            unsigned int point = 0;
            Texture* texture = nullptr;
            Renderbuffer* renderbuffer = nullptr;
            bool owned = false;
            int layer = 0;
            int mipLevel = 0;
        };

        Framebuffer(unsigned int handle, int width, int height, int samples) noexcept
            : _handle(handle), _width(width), _height(height), _samples(samples) {}

        // Slot lookup shared by the const and non-const attachment accessors.
        const Texture* findTexture(unsigned int point) const noexcept {
            for (const AttachmentSlot& slot : _slots) {
                if (slot.point == point) return slot.texture;
            }
            return nullptr;
        }

        // Slot lookup shared by the const and non-const attachment accessors.
        const Renderbuffer* findRenderbuffer(unsigned int point) const noexcept {
            for (const AttachmentSlot& slot : _slots) {
                if (slot.point == point) return slot.renderbuffer;
            }
            return nullptr;
        }

        // Depth attachment first, falling back to the packed depth/stencil point.
        const Texture* findDepthTexture() const noexcept {
            const Texture* found = findTexture(GL_DEPTH_ATTACHMENT);
            return found ? found : findTexture(GL_DEPTH_STENCIL_ATTACHMENT);
        }

        // The framebuffer currently bound to GL_FRAMEBUFFER.
        static int boundFramebuffer() noexcept {
            int previous = 0;
            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous);
            return previous;
        }

        #if ENGINE_HAS_FRAMEBUFFER_DSA
            // The DSA form of attachTextureBound. Layered types must go through
            // glNamedFramebufferTextureLayer: glNamedFramebufferTexture attaches
            // every layer at once and silently ignores the requested one.
            static void attachTextureNamed(unsigned int framebuffer, unsigned int point,
                                           const Texture& texture, int layer, int mipLevel) noexcept {
                switch (texture.type()) {
                    case TextureType::TextureCubeMap:
                    case TextureType::Texture2DArray:
                    case TextureType::Texture3D:
                        glNamedFramebufferTextureLayer(framebuffer, point, texture.id(), mipLevel, layer);
                        return;
                    default:
                        glNamedFramebufferTexture(framebuffer, point, texture.id(), mipLevel);
                        return;
                }
            }
        #endif

        // Attaches a texture to the bound framebuffer, routing by texture type:
        // cube faces take a face target, array/3D take a layer, 2D takes Texture2D.
        static void attachTextureBound(unsigned int point, const Texture& texture,
                                       int layer, int mipLevel) noexcept {
            switch (texture.type()) {
                case TextureType::TextureCubeMap:
                    glFramebufferTexture2D(GL_FRAMEBUFFER, point,
                                           GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<unsigned int>(layer),
                                           texture.id(), mipLevel);
                    return;
                case TextureType::Texture2DArray:
                case TextureType::Texture3D:
                    #if ENGINE_HAS_TEXTURE_ARRAY
                        glFramebufferTextureLayer(GL_FRAMEBUFFER, point, texture.id(), mipLevel, layer);
                    #else
                        log_error("Framebuffer: layered attachments need GL 3.0");
                    #endif
                    return;
                default:
                    glFramebufferTexture2D(GL_FRAMEBUFFER, point, texture.target(),
                                           texture.id(), mipLevel);
                    return;
            }
        }

        // Creates or references one attachment and records its slot.
        bool attachOne(const FramebufferAttachment& attachment, const FramebufferConfig& config) noexcept {
            AttachmentSlot slot;
            slot.point = attachment.point;
            slot.layer = attachment.layer;
            slot.mipLevel = attachment.mipLevel;

            if (attachment.externalTexture) {
                Texture* texture = attachment.externalTexture;
                if (texture->width() != config.width || texture->height() != config.height) {
                    log_warn("Framebuffer: external texture is ", texture->width(), "x",
                             texture->height(), " but the framebuffer is ",
                             config.width, "x", config.height);
                }
                attachTextureBound(attachment.point, *texture, attachment.layer, attachment.mipLevel);
                slot.texture = texture;
            } else if (attachment.externalRenderbuffer) {
                Renderbuffer* buffer = attachment.externalRenderbuffer;
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment.point, GL_RENDERBUFFER,
                                          buffer->id());
                slot.renderbuffer = buffer;
            } else if (attachment.asRenderbuffer) {
                auto buffer = Renderbuffer::create(config.width, config.height,
                                                   attachment.internalFormat, config.samples);
                if (!buffer) {
                    log_error("Framebuffer: failed to create an owned renderbuffer");
                    return false;
                }
                glFramebufferRenderbuffer(GL_FRAMEBUFFER, attachment.point, GL_RENDERBUFFER,
                                          buffer->id());
                slot.renderbuffer = buffer.get();
                slot.owned = true;
                _ownedRenderbuffers.push_back(std::move(buffer));
            } else {
                TextureSpec spec;
                spec.width = config.width;
                spec.height = config.height;
                spec.internalFormat = attachment.internalFormat;
                #if ENGINE_HAS_TEXTURE_MULTISAMPLE
                    spec.samples2D = config.samples;
                    // A multisample texture carries no sampler state at all, so any
                    // override or shadow request on one is dropped by Texture.
                    if (config.samples > 1 && (attachment.overrideSampler || attachment.shadowSampler)) {
                        log_warn("Framebuffer: attachment 0x", std::hex, attachment.point, std::dec,
                                 " is multisampled, so its sampler settings are ignored");
                    }
                #endif

                if (attachment.overrideSampler) {
                    spec.wrapS = attachment.wrapS;
                    spec.wrapT = attachment.wrapT;
                    spec.minFilter = attachment.minFilter;
                    spec.magFilter = attachment.magFilter;
                } else if (texture_detail::isDepthFormat(attachment.internalFormat)) {
                    if (attachment.shadowSampler) {
                        spec.minFilter = GL_LINEAR;
                        spec.magFilter = GL_LINEAR;
                        spec.depthCompare = true;
                        spec.compareFunc = attachment.compareFunc;
                    } else {
                        spec.minFilter = GL_NEAREST;
                        spec.magFilter = GL_NEAREST;
                    }
                }

                auto texture = Texture::createEmpty(spec);
                if (!texture) {
                    log_error("Framebuffer: failed to create an owned texture");
                    return false;
                }
                attachTextureBound(attachment.point, *texture, attachment.layer, attachment.mipLevel);
                slot.texture = texture.get();
                slot.owned = true;
                _ownedTextures.push_back(std::move(texture));
            }

            _slots.push_back(slot);
            return true;
        }

        unsigned int _handle = 0;
        int _width = 0;
        int _height = 0;
        int _samples = 1;

        std::vector<AttachmentSlot> _slots;
        std::vector<std::unique_ptr<Texture>> _ownedTextures;
        std::vector<std::unique_ptr<Renderbuffer>> _ownedRenderbuffers;
        std::vector<unsigned int> _drawBuffers;
};

#endif