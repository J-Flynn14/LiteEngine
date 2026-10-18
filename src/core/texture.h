#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <functional>
#include <utility>
#include <memory>
#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>

// On Windows stb_image opens files with ANSI fopen unless this is defined in
// the implementation TU too; without it non-ASCII paths fail to load.
#if defined(_WIN32) && !defined(STBI_WINDOWS_UTF8)
    #define STBI_WINDOWS_UTF8
#endif
#include <stb_image/stb_image.h>

#include "engine_config.h"
#include "logs.h"
#include "paths.h"
#include "error_handling/gl_error_handler.h"

// GL 3.0 renamed GL_COMPARE_R_TO_TEXTURE to GL_COMPARE_REF_TO_TEXTURE;
// the value is identical, only the token differs per header generation.
#if ENGINE_HAS_COMPARE_REF
    #define ENGINE_GL_COMPARE_REF GL_COMPARE_REF_TO_TEXTURE
#else
    #define ENGINE_GL_COMPARE_REF GL_COMPARE_R_TO_TEXTURE
#endif

struct PixelRect {
    int x = 0, y = 0, width = 0, height = 0;
};

// Source rectangle in normalised texture coordinates.
struct UVRect {
    float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;

    // Region of a uniform grid of cells, for tilesheets and sprite strips.
    static UVRect cell(int column, int row, int columns, int rows) noexcept {
        const float w = 1.0f / static_cast<float>(std::max(columns, 1));
        const float h = 1.0f / static_cast<float>(std::max(rows, 1));
        return {static_cast<float>(column) * w, static_cast<float>(row) * h,
                static_cast<float>(column + 1) * w, static_cast<float>(row + 1) * h};
    }
};

namespace texture_tools {
    // Converts a pixel rectangle to normalised UVs for a texture of that size.
    inline UVRect pixelToUV(const PixelRect& rect, int textureWidth, int textureHeight) noexcept {
        const float w = static_cast<float>(textureWidth);
        const float h = static_cast<float>(textureHeight);
        return {
            static_cast<float>(rect.x) / w,
            static_cast<float>(rect.y) / h,
            static_cast<float>(rect.x + rect.width) / w,
            static_cast<float>(rect.y + rect.height) / h
        };
    }

    // Converts normalised UVs back to a pixel rectangle, rounding to nearest.
    inline PixelRect uvToPixel(const UVRect& uv, int textureWidth, int textureHeight) noexcept {
        const float w = static_cast<float>(textureWidth);
        const float h = static_cast<float>(textureHeight);

        const int x0 = static_cast<int>(uv.u0 * w + 0.5f);
        const int y0 = static_cast<int>(uv.v0 * h + 0.5f);
        const int x1 = static_cast<int>(uv.u1 * w + 0.5f);
        const int y1 = static_cast<int>(uv.v1 * h + 0.5f);

        return {x0, y0, x1 - x0, y1 - y0};
    }
}

namespace texture_detail {
    // Path form stb_image expects: UTF-8 on Windows, the native narrow string elsewhere.
    // POSIX binds straight to path's own string; Windows lifetime-extends the temporary.
#if defined(_WIN32)
    inline std::string stbPath(const std::filesystem::path& p) noexcept {return Paths::toUtf8(p);}
#else
    inline const std::string& stbPath(const std::filesystem::path& p) noexcept {return p.native();}
#endif

    // Re-lays an image of cell-sized tiles with a 1px duplicated gutter ring
    // around every cell, so filtering never bleeds between neighbouring cells.
    inline std::vector<unsigned char> expandCellGutters(const unsigned char* src, int w, int h,
                                                        int cell, int channels, int& outW, int& outH) noexcept {
        const int cellsX = w / cell, cellsY = h / cell;
        const int pcell = cell + 2;
        outW = cellsX * pcell; outH = cellsY * pcell;
        std::vector<unsigned char> dst(static_cast<size_t>(outW) * outH * channels, 0);

        auto copyRow = [&](int sx, int sy, int dx, int dy, int count) {
            std::memcpy(&dst[(static_cast<size_t>(dy) * outW + dx) * channels],
                        &src[(static_cast<size_t>(sy) * w + sx) * channels],
                        static_cast<size_t>(count) * channels);
        };

        for (int cy = 0; cy < cellsY; ++cy) {
            for (int cx = 0; cx < cellsX; ++cx) {
                const int sx = cx * cell, sy = cy * cell;
                const int dx = cx * pcell + 1, dy = cy * pcell + 1;
                for (int row = 0; row < cell; ++row) {
                    copyRow(sx, sy + row, dx, dy + row, cell);
                    copyRow(sx, sy + row, dx - 1, dy + row, 1);
                    copyRow(sx + cell - 1, sy + row, dx + cell, dy + row, 1);
                }
                copyRow(sx, sy, dx, dy - 1, cell);
                copyRow(sx, sy + cell - 1, dx, dy + cell, cell);
                copyRow(sx, sy, dx - 1, dy - 1, 1);
                copyRow(sx + cell - 1, sy, dx + cell, dy - 1, 1);
                copyRow(sx, sy + cell - 1, dx - 1, dy + cell, 1);
                copyRow(sx + cell - 1, sy + cell - 1, dx + cell, dy + cell, 1);
            }
        }
        return dst;
    }

    // Premultiplies RGB by alpha in place; expects 4-channel data.
    inline void premultiply(unsigned char* data, int w, int h) noexcept {
        const size_t count = static_cast<size_t>(w) * static_cast<size_t>(h);
        for (size_t i = 0; i < count; ++i) {
            unsigned char* p = data + i * 4;
            const int a = p[3];
            p[0] = static_cast<unsigned char>((p[0] * a + 127) / 255);
            p[1] = static_cast<unsigned char>((p[1] * a + 127) / 255);
            p[2] = static_cast<unsigned char>((p[2] * a + 127) / 255);
        }
    }

    // Pixel-transfer format for a channel count (legacy luminance below GL 3.0).
    inline unsigned int externalFormat(int channels) noexcept {
        #if ENGINE_HAS_TEXTURE_RG
            switch (channels) {
                case 1:  return GL_RED;
                case 2:  return GL_RG;
                case 3:  return GL_RGB;
                default: return GL_RGBA;
            }
        #else
            switch (channels) {
                case 1:  return GL_LUMINANCE;
                case 2:  return GL_LUMINANCE_ALPHA;
                case 3:  return GL_RGB;
                default: return GL_RGBA;
            }
        #endif
    }

    // True for depth or combined depth/stencil internal formats.
    inline bool isDepthFormat(unsigned int internalFormat) noexcept {
        switch (internalFormat) {
            case GL_DEPTH_COMPONENT16:
            case GL_DEPTH_COMPONENT24:
            case GL_DEPTH_COMPONENT:
            #if ENGINE_HAS_FRAMEBUFFER
                case GL_DEPTH_COMPONENT32F:
                case GL_DEPTH24_STENCIL8:
                case GL_DEPTH32F_STENCIL8:
                case GL_DEPTH_STENCIL:
            #endif
                return true;
            default:
                return false;
        }
    }

    // True for internal formats that carry a stencil plane.
    inline bool isStencilFormat(unsigned int internalFormat) noexcept {
        switch (internalFormat) {
            #if ENGINE_HAS_FRAMEBUFFER
                case GL_DEPTH24_STENCIL8:
                case GL_DEPTH32F_STENCIL8:
                case GL_DEPTH_STENCIL:
                    return true;
            #endif
            default:
                return false;
        }
    }

    // 8-bit internal format for a channel count, optionally sRGB-encoded.
    inline unsigned int internalFormat(int channels, bool srgb) noexcept {
        if (srgb) {
            if (channels == 3) return GL_SRGB8;
            if (channels == 4) return GL_SRGB8_ALPHA8;
        }
        #if ENGINE_HAS_TEXTURE_RG
            switch (channels) {
                case 1:  return GL_R8;
                case 2:  return GL_RG8;
                case 3:  return GL_RGB8;
                default: return GL_RGBA8;
            }
        #else
            switch (channels) {
                case 1:  return GL_LUMINANCE8;
                case 2:  return GL_LUMINANCE8_ALPHA8;
                case 3:  return GL_RGB8;
                default: return GL_RGBA8;
            }
        #endif
    }

    #if GL_TARGET_VERSION >= 30
        // Float internal format for a channel count: 32F when highPrecision, else 16F.
        inline unsigned int internalFormatFloat(int channels, bool highPrecision) noexcept {
            if (highPrecision) {
                switch (channels) {
                    case 1:  return GL_R32F;
                    case 2:  return GL_RG32F;
                    case 3:  return GL_RGB32F;
                    default: return GL_RGBA32F;
                }
            }
            switch (channels) {
                case 1:  return GL_R16F;
                case 2:  return GL_RG16F;
                case 3:  return GL_RGB16F;
                default: return GL_RGBA16F;
            }
        }
    #endif

    // Bytes per 4x4 block of a block-compressed format; 0 when unrecognised.
    inline int compressedBlockBytes(unsigned int internalFormat) noexcept {
        switch (internalFormat) {
            #ifdef GL_COMPRESSED_RGB_S3TC_DXT1_EXT
                case GL_COMPRESSED_RGB_S3TC_DXT1_EXT:
            #endif
            #ifdef GL_COMPRESSED_RGBA_S3TC_DXT1_EXT
                case GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:
            #endif
            #ifdef GL_COMPRESSED_SRGB_S3TC_DXT1_EXT
                case GL_COMPRESSED_SRGB_S3TC_DXT1_EXT:
            #endif
            #ifdef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT
                case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
            #endif
            #if ENGINE_HAS_COMPRESSED_RGTC
                case GL_COMPRESSED_RED_RGTC1:
                case GL_COMPRESSED_SIGNED_RED_RGTC1:
            #endif
                return 8;
            #ifdef GL_COMPRESSED_RGBA_S3TC_DXT3_EXT
                case GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:
            #endif
            #ifdef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT
                case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
            #endif
            #ifdef GL_COMPRESSED_RGBA_S3TC_DXT5_EXT
                case GL_COMPRESSED_RGBA_S3TC_DXT5_EXT:
            #endif
            #ifdef GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT
                case GL_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
            #endif
            #if ENGINE_HAS_COMPRESSED_RGTC
                case GL_COMPRESSED_RG_RGTC2:
                case GL_COMPRESSED_SIGNED_RG_RGTC2:
            #endif
            #if ENGINE_HAS_COMPRESSED_BPTC
                case GL_COMPRESSED_RGBA_BPTC_UNORM:
                case GL_COMPRESSED_SRGB_ALPHA_BPTC_UNORM:
                case GL_COMPRESSED_RGB_BPTC_SIGNED_FLOAT:
                case GL_COMPRESSED_RGB_BPTC_UNSIGNED_FLOAT:
            #endif
                return 16;
            default:
                return 0;
        }
    }

    // Byte size of one full 2D image of a block-compressed format;
    // 0 if the format isn't one compressedBlockBytes() recognises.
    inline size_t compressedImageSize(unsigned int internalFormat, int width, int height) noexcept {
        const int blockBytes = compressedBlockBytes(internalFormat);
        if (blockBytes == 0) return 0;
        const size_t blocksX = static_cast<size_t>((width + 3) / 4);
        const size_t blocksY = static_cast<size_t>((height + 3) / 4);
        return blocksX * blocksY * static_cast<size_t>(blockBytes);
    }

    // Component count for readback validation; 0 means "cannot validate",
    // which callers treat as proceed-on-trust rather than an error.
    inline int componentsForFormat(unsigned int format) noexcept {
        switch (format) {
            case GL_DEPTH_COMPONENT: case GL_STENCIL_INDEX: return 1;
            case GL_RGB: case GL_BGR: return 3;
            case GL_RGBA: case GL_BGRA: return 4;
            #if ENGINE_HAS_TEXTURE_RG
                case GL_RED: return 1;
                case GL_RG: return 2;
            #else
                case GL_LUMINANCE: return 1;
                case GL_LUMINANCE_ALPHA: return 2;
            #endif
            #if ENGINE_HAS_FRAMEBUFFER
                case GL_DEPTH_STENCIL: return 2;
            #endif
            default: return 0;
        }
    }

    // Bytes per component for readback validation; 0 means "cannot validate".
    inline int bytesForType(unsigned int type) noexcept {
        switch (type) {
            case GL_UNSIGNED_BYTE: case GL_BYTE: return 1;
            case GL_UNSIGNED_SHORT: case GL_SHORT: return 2;
            case GL_UNSIGNED_INT: case GL_INT: case GL_FLOAT: return 4;
            default: return 0;
        }
    }

    // Sampler state shared by TextureSpec and TextureLoadOptions via inheritance,
    // so the apply logic exists once. Everything here is core GL 1.4 or earlier.
    struct SamplerParams {
        int wrapS = GL_CLAMP_TO_EDGE;
        int wrapT = GL_CLAMP_TO_EDGE;
        int wrapR = GL_CLAMP_TO_EDGE;                    // relevant for Texture3D/TextureCubeMap
        int minFilter = GL_LINEAR;
        int magFilter = GL_LINEAR;
        float borderColor[4] = {0.0f, 0.0f, 0.0f, 0.0f};  // used when a wrap mode is GL_CLAMP_TO_BORDER
        int baseLevel = 0;
        int maxLevel = 1000;                              // GL's own default
        float minLod = -1000.0f;                          // GL's own default
        float maxLod = 1000.0f;                           // GL's own default
        float lodBias = 0.0f;
        bool depthCompare = false;                        // GL_TEXTURE_COMPARE_MODE = compare-to-ref
        int compareFunc = GL_LEQUAL;
        #if ENGINE_HAS_TEXTURE_SWIZZLE
            int swizzle[4] = {GL_RED, GL_GREEN, GL_BLUE, GL_ALPHA};   // identity
        #endif
        #if ENGINE_HAS_ANISOTROPY
            float maxAnisotropy = 1.0f;
        #endif
    };

    #if ENGINE_HAS_DSA
        // Applies sampler state to a texture object through DSA.
        inline void applySamplerParamsDSA(unsigned int id, const SamplerParams& p) noexcept {
            glTextureParameteri(id, GL_TEXTURE_WRAP_S, p.wrapS);
            glTextureParameteri(id, GL_TEXTURE_WRAP_T, p.wrapT);
            glTextureParameteri(id, GL_TEXTURE_WRAP_R, p.wrapR);
            glTextureParameteri(id, GL_TEXTURE_MIN_FILTER, p.minFilter);
            glTextureParameteri(id, GL_TEXTURE_MAG_FILTER, p.magFilter);
            glTextureParameterfv(id, GL_TEXTURE_BORDER_COLOR, p.borderColor);
            glTextureParameteri(id, GL_TEXTURE_BASE_LEVEL, p.baseLevel);
            glTextureParameteri(id, GL_TEXTURE_MAX_LEVEL, p.maxLevel);
            glTextureParameterf(id, GL_TEXTURE_MIN_LOD, p.minLod);
            glTextureParameterf(id, GL_TEXTURE_MAX_LOD, p.maxLod);
            glTextureParameterf(id, GL_TEXTURE_LOD_BIAS, p.lodBias);
            glTextureParameteri(id, GL_TEXTURE_COMPARE_MODE, p.depthCompare ? ENGINE_GL_COMPARE_REF : GL_NONE);
            glTextureParameteri(id, GL_TEXTURE_COMPARE_FUNC, p.compareFunc);
            #if ENGINE_HAS_TEXTURE_SWIZZLE
                glTextureParameteriv(id, GL_TEXTURE_SWIZZLE_RGBA, p.swizzle);
            #endif
            #if ENGINE_HAS_ANISOTROPY
                if (p.maxAnisotropy > 1.0f) {
                    float limit = 1.0f;
                    glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &limit);
                    glTextureParameterf(id, GL_TEXTURE_MAX_ANISOTROPY, p.maxAnisotropy < limit ? p.maxAnisotropy : limit);
                }
            #endif
        }
    #endif

    // Applies sampler state to the texture bound to `target`.
    inline void applySamplerParamsLegacy(unsigned int target, const SamplerParams& p) noexcept {
        glTexParameteri(target, GL_TEXTURE_WRAP_S, p.wrapS);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, p.wrapT);
        glTexParameteri(target, GL_TEXTURE_WRAP_R, p.wrapR);
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, p.minFilter);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, p.magFilter);
        glTexParameterfv(target, GL_TEXTURE_BORDER_COLOR, p.borderColor);
        glTexParameteri(target, GL_TEXTURE_BASE_LEVEL, p.baseLevel);
        glTexParameteri(target, GL_TEXTURE_MAX_LEVEL, p.maxLevel);
        glTexParameterf(target, GL_TEXTURE_MIN_LOD, p.minLod);
        glTexParameterf(target, GL_TEXTURE_MAX_LOD, p.maxLod);
        glTexParameterf(target, GL_TEXTURE_LOD_BIAS, p.lodBias);
        glTexParameteri(target, GL_TEXTURE_COMPARE_MODE, p.depthCompare ? ENGINE_GL_COMPARE_REF : GL_NONE);
        glTexParameteri(target, GL_TEXTURE_COMPARE_FUNC, p.compareFunc);
        #if ENGINE_HAS_TEXTURE_SWIZZLE
            glTexParameteriv(target, GL_TEXTURE_SWIZZLE_RGBA, p.swizzle);
        #endif
        #if ENGINE_HAS_ANISOTROPY
            if (p.maxAnisotropy > 1.0f) {
                float limit = 1.0f;
                glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY, &limit);
                glTexParameterf(target, GL_TEXTURE_MAX_ANISOTROPY, p.maxAnisotropy < limit ? p.maxAnisotropy : limit);
            }
        #endif
    }
}

enum class TextureType {Texture2D, Texture2DArray, TextureCubeMap, Texture3D};

struct TextureSize {int width = 0, height = 0;};

struct TextureSpec : texture_detail::SamplerParams {
    TextureType type = TextureType::Texture2D;
    int width = 1;
    int height = 1;
    int layers = 1;                          // array layer count (Texture2DArray) or depth (Texture3D)
    unsigned int internalFormat = GL_RGBA8;
    bool generateMipmaps = false;
    std::string debugLabel;                  // applied via glObjectLabel when ENGINE_HAS_DEBUG_OUTPUT

    #if ENGINE_HAS_TEXTURE_MULTISAMPLE
        // Texture2D only: samples2D > 1 makes a GL_TEXTURE_2D_MULTISAMPLE texture,
        // which plain texture() cannot sample and which ignores all sampler state.
        int samples2D = 1;
        bool fixedSampleLocations2D = true;
    #endif
};

struct TextureLoadOptions : texture_detail::SamplerParams {
    bool flipVertically = true;
    bool premultiplyAlpha = true;
    bool srgb = false;
    int forceChannels = 4;
    int gutterCell = 0;
    bool generateMipmaps = false;
    bool preferHighPrecisionFloat = false;   // use 32F instead of 16F for HDR sources
    std::string debugLabel;
};

class Texture {
    public:
        // Loads a 2D texture from an image file (LDR or HDR); null on failure.
        static std::unique_ptr<Texture> create(const std::filesystem::path& path,
                                               const TextureLoadOptions& options = {}) noexcept {
            if (options.forceChannels < 0 || options.forceChannels > 4) {
                log_error("Texture: forceChannels must be 0-4, got ", options.forceChannels);
                return nullptr;
            }

            const auto& pathString = texture_detail::stbPath(path);

            const bool hdr = stbi_is_hdr(pathString.c_str()) != 0;
            stbi_set_flip_vertically_on_load(options.flipVertically ? 1 : 0);

            if (hdr) {
                #if GL_TARGET_VERSION >= 30
                    if (options.gutterCell > 0 || options.premultiplyAlpha) {
                        log_warn("Texture: gutterCell/premultiplyAlpha are ignored for HDR sources (", path, ")");
                    }
                    int width = 0, height = 0, fileChannels = 0;
                    float* data = stbi_loadf(pathString.c_str(), &width, &height, &fileChannels, options.forceChannels);
                    if (!data) {
                        const char* reason = stbi_failure_reason();
                        log_error("Texture: failed to load ", path, ": ", reason ? reason : "unknown");
                        return nullptr;
                    }
                    const int channels = options.forceChannels > 0 ? options.forceChannels : fileChannels;
                    return createFromHDR(path, data, width, height, channels, options);
                #else
                    log_error("Texture: HDR sources need GL 3.0 float formats (", path, ")");
                    return nullptr;
                #endif
            }

            int width = 0, height = 0, fileChannels = 0;
            unsigned char* data = stbi_load(pathString.c_str(), &width, &height, &fileChannels,
                                            options.forceChannels);
            if (!data) {
                const char* reason = stbi_failure_reason();
                log_error("Texture: failed to load ", path, ": ", reason ? reason : "unknown");
                return nullptr;
            }

            const int channels = options.forceChannels > 0 ? options.forceChannels : fileChannels;

            if (options.premultiplyAlpha) {
                if (channels == 4) {
                    texture_detail::premultiply(data, width, height);
                } else {
                    log_warn("Texture: premultiplyAlpha ignored, ", path, " has ", channels, " channel(s)");
                }
            }

            const unsigned char* upload = data;
            std::vector<unsigned char> padded;
            int cellPitch = 0;

            if (options.gutterCell > 0) {
                const int cell = options.gutterCell;
                if (cell > width || cell > height) {
                    log_error("Texture: gutterCell ", cell, " exceeds the image (",
                              width, "x", height, ") in ", path);
                    stbi_image_free(data);
                    return nullptr;
                }
                if (width % cell != 0 || height % cell != 0) {
                    log_error("Texture: ", width, "x", height, " is not a whole number of ",
                              cell, "px cells in ", path);
                    stbi_image_free(data);
                    return nullptr;
                }
                int pw = 0, ph = 0;
                padded = texture_detail::expandCellGutters(data, width, height, cell, channels, pw, ph);
                upload = padded.data();
                width = pw;
                height = ph;
                cellPitch = cell + 2;
            }

            const unsigned int internal = texture_detail::internalFormat(channels, options.srgb);
            const unsigned int external = texture_detail::externalFormat(channels);

            // GL_UNPACK_ALIGNMENT is context state describing the client buffer,
            // not texture state, so DSA does not remove the need to save/restore it.
            int previousAlignment = 4;
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            GlErrorHandler::clear();

            unsigned int id = 0;

            #if ENGINE_HAS_DSA
                glCreateTextures(GL_TEXTURE_2D, 1, &id);
                texture_detail::applySamplerParamsDSA(id, options);

                // ARB_texture_storage is a hard dependency of DSA, so this path
                // always has immutable storage available; no non-storage fallback.
                const int levels = options.generateMipmaps ? mipLevels(width, height) : 1;
                glTextureStorage2D(id, levels, internal, width, height);
                glTextureSubImage2D(id, 0, 0, 0, width, height, external, GL_UNSIGNED_BYTE, upload);
                if (options.generateMipmaps) glGenerateTextureMipmap(id);
            #else
                int previous = 0;
                glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);

                glGenTextures(1, &id);
                glBindTexture(GL_TEXTURE_2D, id);
                texture_detail::applySamplerParamsLegacy(GL_TEXTURE_2D, options);

                #if ENGINE_HAS_TEXTURE_STORAGE
                    const int levels = options.generateMipmaps ? mipLevels(width, height) : 1;
                    glTexStorage2D(GL_TEXTURE_2D, levels, internal, width, height);
                    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, external, GL_UNSIGNED_BYTE, upload);
                #else
                    glTexImage2D(GL_TEXTURE_2D, 0, static_cast<int>(internal), width, height, 0,
                                 external, GL_UNSIGNED_BYTE, upload);
                #endif

                #if ENGINE_HAS_MIPMAP
                    if (options.generateMipmaps) glGenerateMipmap(GL_TEXTURE_2D);
                #else
                    if (options.generateMipmaps) log_warn("Texture: mipmap generation needs GL 3.0");
                #endif

                glBindTexture(GL_TEXTURE_2D, static_cast<unsigned int>(previous));
            #endif

            glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);

            const GLenum error = glGetError();
            if (error != GL_NO_ERROR) {
                log_error("Texture: GL error 0x", std::hex, error, std::dec, " uploading ", path);
                glDeleteTextures(1, &id);
                stbi_image_free(data);
                return nullptr;
            }

            #if ENGINE_HAS_DEBUG_OUTPUT
                if (!options.debugLabel.empty()) {
                    glObjectLabel(GL_TEXTURE, id, static_cast<GLsizei>(options.debugLabel.size()),
                                  options.debugLabel.c_str());
                }
            #endif

            stbi_image_free(data);
            log_info("Texture loaded: ", path, " ", width, "x", height, " (", channels, " channel(s))");
            std::unique_ptr<Texture> texture(new Texture(id, width, height, channels,
                                                         options.gutterCell, cellPitch));
            texture->_internalFormat = internal;
            return texture;
        }

        // Loads a cubemap from 6 face images in GL order (+X -X +Y -Y +Z -Z);
        // faces are not flipped and must share dimensions and channel count.
        static std::unique_ptr<Texture> createCubemap(const std::array<std::filesystem::path, 6>& facePaths,
                                                             const TextureLoadOptions& options = {}) noexcept {
            if (options.forceChannels < 0 || options.forceChannels > 4) {
                log_error("Texture: forceChannels must be 0-4, got ", options.forceChannels);
                return nullptr;
            }

            stbi_set_flip_vertically_on_load(0);

            struct Face {unsigned char* data = nullptr; int width = 0, height = 0;};
            std::array<Face, 6> faces{};
            int channels = 0;

            const auto freeAll = [&] {for (Face& f : faces) if (f.data) stbi_image_free(f.data);};

            for (size_t i = 0; i < 6; ++i) {
                int w = 0, h = 0, fileChannels = 0;
                const auto& facePathString = texture_detail::stbPath(facePaths[i]);
                unsigned char* data = stbi_load(facePathString.c_str(), &w, &h, &fileChannels,
                                                options.forceChannels);
                if (!data) {
                    const char* reason = stbi_failure_reason();
                    log_error("Texture: failed to load cubemap face ", i, " (", facePaths[i], "): ",
                              reason ? reason : "unknown");
                    freeAll();
                    return nullptr;
                }
                const int faceChannels = options.forceChannels > 0 ? options.forceChannels : fileChannels;
                if (i == 0) {
                    channels = faceChannels;
                } else if (w != faces[0].width || h != faces[0].height || faceChannels != channels) {
                    log_error("Texture: cubemap face ", i, " (", facePaths[i], ") is ", w, "x", h,
                              " (", faceChannels, " channel(s)), expected ", faces[0].width, "x",
                              faces[0].height, " (", channels, " channel(s))");
                    stbi_image_free(data);
                    freeAll();
                    return nullptr;
                }
                faces[i] = {data, w, h};
            }

            const int width = faces[0].width, height = faces[0].height;
            const unsigned int internal = texture_detail::internalFormat(channels, options.srgb);
            const unsigned int external = texture_detail::externalFormat(channels);

            int previousAlignment = 4;
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            GlErrorHandler::clear();

            unsigned int id = 0;

            #if ENGINE_HAS_DSA
                glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &id);
                texture_detail::applySamplerParamsDSA(id, options);
                // Cubemap storage is 2D-shaped (6 faces implicit); DSA uploads faces
                // through the 3D-shaped SubImage call, with z = face index.
                const int levels = options.generateMipmaps ? mipLevels(width, height) : 1;
                glTextureStorage2D(id, levels, internal, width, height);
                for (unsigned int face = 0; face < 6; ++face) {
                    glTextureSubImage3D(id, 0, 0, 0, static_cast<int>(face), width, height, 1,
                                        external, GL_UNSIGNED_BYTE, faces[face].data);
                }
                if (options.generateMipmaps) glGenerateTextureMipmap(id);
            #else
                int previous = 0;
                glGetIntegerv(GL_TEXTURE_BINDING_CUBE_MAP, &previous);
                glGenTextures(1, &id);
                glBindTexture(GL_TEXTURE_CUBE_MAP, id);
                texture_detail::applySamplerParamsLegacy(GL_TEXTURE_CUBE_MAP, options);

                #if ENGINE_HAS_TEXTURE_STORAGE
                    const int levels = options.generateMipmaps ? mipLevels(width, height) : 1;
                    glTexStorage2D(GL_TEXTURE_CUBE_MAP, levels, internal, width, height);
                    for (unsigned int face = 0; face < 6; ++face) {
                        glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, 0, 0, width, height,
                                        external, GL_UNSIGNED_BYTE, faces[face].data);
                    }
                #else
                    // Without DSA or storage, the per-face target enum is used for
                    // both allocation and upload -- there is no 3D-shaped path here.
                    for (unsigned int face = 0; face < 6; ++face) {
                        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, static_cast<int>(internal),
                                    width, height, 0, external, GL_UNSIGNED_BYTE, faces[face].data);
                    }
                #endif
                #if ENGINE_HAS_MIPMAP
                    if (options.generateMipmaps) glGenerateMipmap(GL_TEXTURE_CUBE_MAP);
                #endif
                glBindTexture(GL_TEXTURE_CUBE_MAP, static_cast<unsigned int>(previous));
            #endif

            glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);

            const GLenum error = glGetError();
            freeAll();
            if (error != GL_NO_ERROR) {
                log_error("Texture: GL error 0x", std::hex, error, std::dec, " uploading cubemap");
                glDeleteTextures(1, &id);
                return nullptr;
            }

            #if ENGINE_HAS_DEBUG_OUTPUT
                if (!options.debugLabel.empty()) {
                    glObjectLabel(GL_TEXTURE, id, static_cast<GLsizei>(options.debugLabel.size()),
                                  options.debugLabel.c_str());
                }
            #endif

            log_info("Cubemap loaded: ", width, "x", height, " (", channels, " channel(s)), 6 faces");
            std::unique_ptr<Texture> texture(new Texture(id, width, height, channels, 0, 0));
            texture->_target = GL_TEXTURE_CUBE_MAP;
            texture->_type = TextureType::TextureCubeMap;
            texture->_internalFormat = internal;
            return texture;
        }

        // Allocates storage without uploading; fill with setSubData or attach
        // to a Framebuffer as a render target.
        static std::unique_ptr<Texture> createEmpty(const TextureSpec& spec) noexcept {
            if (spec.width <= 0 || spec.height <= 0) {
                log_error("Texture::createEmpty needs positive dimensions, got ",
                          spec.width, "x", spec.height);
                return nullptr;
            }
            const bool needsLayers = spec.type == TextureType::Texture2DArray
                                     || spec.type == TextureType::Texture3D;
            if (needsLayers && spec.layers <= 0) {
                log_error("Texture::createEmpty needs a positive layer/depth count for this type, got ",
                          spec.layers);
                return nullptr;
            }
            if (spec.type == TextureType::Texture2DArray) {
                #if !ENGINE_HAS_TEXTURE_ARRAY
                    log_error("Texture::createEmpty: array textures need GL 3.0 (ENGINE_HAS_TEXTURE_ARRAY)");
                    return nullptr;
                #endif
            }

            bool multisample = false;
            #if ENGINE_HAS_TEXTURE_MULTISAMPLE
                multisample = spec.samples2D > 1;
            #endif
            if (multisample && spec.type != TextureType::Texture2D) {
                log_error("Texture::createEmpty: multisampling is only supported for Texture2D");
                return nullptr;
            }

            const unsigned int target = resolveTarget(spec.type, multisample);
            const bool uses3DStorage = needsLayers;

            unsigned int id = 0;

            #if ENGINE_HAS_DSA
                glCreateTextures(target, 1, &id);
                if (!id) {log_error("Texture::createEmpty failed to generate a handle"); return nullptr;}
                GlErrorHandler::clear();

                if (!multisample) texture_detail::applySamplerParamsDSA(id, spec);

                #if ENGINE_HAS_TEXTURE_MULTISAMPLE
                    if (multisample) {
                        glTextureStorage2DMultisample(id, spec.samples2D, spec.internalFormat,
                                                      spec.width, spec.height,
                                                      spec.fixedSampleLocations2D ? GL_TRUE : GL_FALSE);
                    } else
                #endif
                {
                    const int levels = spec.generateMipmaps ? mipLevels(spec.width, spec.height) : 1;
                    if (uses3DStorage) {
                        glTextureStorage3D(id, levels, spec.internalFormat, spec.width, spec.height, spec.layers);
                    } else {
                        glTextureStorage2D(id, levels, spec.internalFormat, spec.width, spec.height);
                    }
                    // The levels are allocated but hold nothing yet; call
                    // regenerateMipmaps() once level 0 has been filled.
                }

                #if ENGINE_HAS_DEBUG_OUTPUT
                    if (!spec.debugLabel.empty()) {
                        glObjectLabel(GL_TEXTURE, id, static_cast<GLsizei>(spec.debugLabel.size()),
                                      spec.debugLabel.c_str());
                    }
                #endif

                if (GlErrorHandler::logErrors()) {glDeleteTextures(1, &id); return nullptr;}
            #else
                glGenTextures(1, &id);
                if (!id) {log_error("Texture::createEmpty failed to generate a handle"); return nullptr;}

                const int previous = boundTexture(target);
                glBindTexture(target, id);
                GlErrorHandler::clear();

                if (!multisample) texture_detail::applySamplerParamsLegacy(target, spec);

                #if ENGINE_HAS_TEXTURE_MULTISAMPLE
                    if (multisample) {
                        glTexImage2DMultisample(target, spec.samples2D, spec.internalFormat,
                                                spec.width, spec.height,
                                                spec.fixedSampleLocations2D ? GL_TRUE : GL_FALSE);
                    } else
                #endif
                {
                    #if ENGINE_HAS_TEXTURE_STORAGE
                        const int levels = spec.generateMipmaps ? mipLevels(spec.width, spec.height) : 1;
                        if (uses3DStorage) {
                            glTexStorage3D(target, levels, spec.internalFormat, spec.width, spec.height, spec.layers);
                        } else {
                            glTexStorage2D(target, levels, spec.internalFormat, spec.width, spec.height);
                        }
                    #else
                        // Depth+stencil formats need GL_DEPTH_STENCIL with a matching
                        // packed type; GL_DEPTH_COMPONENT pairs are invalid for those.
                        const bool stencil = texture_detail::isStencilFormat(spec.internalFormat);
                        const bool depth = texture_detail::isDepthFormat(spec.internalFormat);
                        unsigned int external = GL_RGBA;
                        unsigned int type = GL_UNSIGNED_BYTE;
                        #if ENGINE_HAS_FRAMEBUFFER
                            if (stencil) {
                                external = GL_DEPTH_STENCIL;
                                type = (spec.internalFormat == GL_DEPTH32F_STENCIL8)
                                           ? GL_FLOAT_32_UNSIGNED_INT_24_8_REV
                                           : GL_UNSIGNED_INT_24_8;
                            } else if (depth) {
                                external = GL_DEPTH_COMPONENT;
                                type = GL_FLOAT;
                            }
                        #else
                            (void)stencil;
                            if (depth) {
                                external = GL_DEPTH_COMPONENT;
                                type = GL_UNSIGNED_INT;
                            }
                        #endif

                        if (spec.type == TextureType::TextureCubeMap) {
                            for (unsigned int face = 0; face < 6; ++face) {
                                glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0,
                                            static_cast<int>(spec.internalFormat),
                                            spec.width, spec.height, 0, external, type, nullptr);
                            }
                        } else if (uses3DStorage) {
                            glTexImage3D(target, 0, static_cast<int>(spec.internalFormat),
                                        spec.width, spec.height, spec.layers, 0, external, type, nullptr);
                        } else {
                            glTexImage2D(target, 0, static_cast<int>(spec.internalFormat),
                                        spec.width, spec.height, 0, external, type, nullptr);
                        }
                    #endif
                    // The levels are allocated but hold nothing yet; call
                    // regenerateMipmaps() once level 0 has been filled.
                }

                glBindTexture(target, static_cast<unsigned int>(previous));

                if (GlErrorHandler::logErrors()) {glDeleteTextures(1, &id); return nullptr;}
            #endif

            std::unique_ptr<Texture> texture(new Texture(id, spec.width, spec.height, 0, 0, 0));
            texture->_target = target;
            texture->_type = spec.type;
            texture->_layers = spec.layers;
            texture->_internalFormat = spec.internalFormat;
            texture->_spec = spec;
            texture->_fromSpec = true;
            return texture;
        }

        struct Color {unsigned char r, g, b;};

        // Creates a 1x1 texture filled with a single opaque colour.
        static std::unique_ptr<Texture> createSolidColor(TextureSpec spec, const Color color) noexcept {
            spec.width = 1;
            spec.height = 1;
            if (spec.internalFormat == GL_RGBA8) spec.internalFormat = GL_RGB8;

            const unsigned char pixels[] = {color.r, color.g, color.b};

            auto texture = Texture::createEmpty(spec);
            if (!texture) return nullptr;

            texture->setData(pixels, GL_RGB, GL_UNSIGNED_BYTE);
            return texture;
        }

        // Recreates a createEmpty texture at a new size, keeping its spec.
        bool resize(int width, int height) noexcept {return resize(width, height, _layers);}

        // Recreates a createEmpty texture at a new size and layer count.
        bool resize(int width, int height, int layers) noexcept {
            if (!_fromSpec) {log_error("Texture::resize is only valid on createEmpty textures"); return false;}
            if (width == _width && height == _height && layers == _layers) return true;

            TextureSpec spec = _spec;
            spec.width = width;
            spec.height = height;
            spec.layers = layers;
            auto replacement = createEmpty(spec);
            if (!replacement) return false;

            *this = std::move(*replacement);
            return true;
        }

        // Binds this texture to a texture unit.
        void bind(unsigned int unit = 0) const noexcept {
            #if ENGINE_HAS_MULTITEXTURE
                glActiveTexture(GL_TEXTURE0 + unit);
            #else
                if (unit != 0) log_warn("Texture::bind: texture units need GL 1.3");
            #endif
            glBindTexture(_target, _texture);
        }

        // Unbinds this texture's target from a texture unit.
        void unbind(unsigned int unit = 0) const noexcept {
            #if ENGINE_HAS_MULTITEXTURE
                glActiveTexture(GL_TEXTURE0 + unit);
            #else
                (void)unit;
            #endif
            glBindTexture(_target, 0);
        }

        // ---- runtime updates ----

        // z selects the array layer (Texture2DArray/Texture3D) or cube face 0-5;
        // depth is the layer count to touch (pass 1 for a single layer/face).

        // Uploads a sub-region of one mip level; rejects null data.
        bool setSubData(int x, int y, int z, int width, int height, int depth,
                        unsigned int format, unsigned int type, const void* data, int level = 0) noexcept {
            if (!data) {log_error("Texture::setSubData given a null pointer"); return false;}
            int previousAlignment = 4;
            glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);
            glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
            #if ENGINE_HAS_DSA
                if (_type == TextureType::TextureCubeMap || _type == TextureType::Texture2DArray
                    || _type == TextureType::Texture3D) {
                    glTextureSubImage3D(_texture, level, x, y, z, width, height, depth, format, type, data);
                } else {
                    glTextureSubImage2D(_texture, level, x, y, width, height, format, type, data);
                }
            #else
                const int previous = boundTexture(_target);
                glBindTexture(_target, _texture);
                if (_type == TextureType::TextureCubeMap) {
                    glTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<unsigned int>(z), level,
                                    x, y, width, height, format, type, data);
                } else if (_type == TextureType::Texture2DArray || _type == TextureType::Texture3D) {
                    glTexSubImage3D(_target, level, x, y, z, width, height, depth, format, type, data);
                } else {
                    glTexSubImage2D(_target, level, x, y, width, height, format, type, data);
                }
                glBindTexture(_target, static_cast<unsigned int>(previous));
            #endif
            glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);
            return true;
        }

        // Replaces the whole level-0 image (or face/layer 0 of layered types).
        bool setData(const void* data, unsigned int format, unsigned int type) noexcept {
            return setSubData(0, 0, 0, _width, _height, 1, format, type, data);
        }

        // Uploads pre-compressed data into a sub-region of one mip level.
        bool setCompressedSubData(int x, int y, int z, int width, int height, int depth,
                                  unsigned int format, size_t byteCount, const void* data, int level = 0) noexcept {
            if (!data) {log_error("Texture::setCompressedSubData given a null pointer"); return false;}
            #if ENGINE_HAS_DSA
                if (_type == TextureType::TextureCubeMap || _type == TextureType::Texture2DArray
                    || _type == TextureType::Texture3D) {
                    glCompressedTextureSubImage3D(_texture, level, x, y, z, width, height, depth, format,
                                                  static_cast<GLsizei>(byteCount), data);
                } else {
                    glCompressedTextureSubImage2D(_texture, level, x, y, width, height, format,
                                                  static_cast<GLsizei>(byteCount), data);
                }
            #else
                const int previous = boundTexture(_target);
                glBindTexture(_target, _texture);
                if (_type == TextureType::TextureCubeMap) {
                    glCompressedTexSubImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<unsigned int>(z),
                                              level, x, y, width, height, format,
                                              static_cast<GLsizei>(byteCount), data);
                } else if (_type == TextureType::Texture2DArray || _type == TextureType::Texture3D) {
                    glCompressedTexSubImage3D(_target, level, x, y, z, width, height, depth, format,
                                              static_cast<GLsizei>(byteCount), data);
                } else {
                    glCompressedTexSubImage2D(_target, level, x, y, width, height, format,
                                              static_cast<GLsizei>(byteCount), data);
                }
                glBindTexture(_target, static_cast<unsigned int>(previous));
            #endif
            return true;
        }

        // Reads a mip level back to CPU memory, validating the buffer size where it can.
        // DSA cubemaps return all 6 faces and ignore cubeFace; non-DSA returns one face.
        bool getData(void* out, size_t outCapacity, unsigned int format, unsigned int type,
                    int level = 0, int cubeFace = 0) const noexcept {
            if (!out) {log_error("Texture::getData given a null pointer"); return false;}

            const int components = texture_detail::componentsForFormat(format);
            const int bytesPerComponent = texture_detail::bytesForType(type);
            if (components > 0 && bytesPerComponent > 0) {
                int mipWidth = _width >> level;   if (mipWidth < 1) mipWidth = 1;
                int mipHeight = _height >> level; if (mipHeight < 1) mipHeight = 1;
                size_t slices = 1;
                if (_type == TextureType::Texture2DArray) {
                    slices = static_cast<size_t>(_layers);
                } else if (_type == TextureType::Texture3D) {
                    int mipDepth = _layers >> level; if (mipDepth < 1) mipDepth = 1;
                    slices = static_cast<size_t>(mipDepth);
                }
                #if ENGINE_HAS_DSA
                    if (_type == TextureType::TextureCubeMap) slices = 6;
                #endif
                const size_t required = static_cast<size_t>(mipWidth) * static_cast<size_t>(mipHeight)
                                       * slices * static_cast<size_t>(components)
                                       * static_cast<size_t>(bytesPerComponent);
                if (outCapacity < required) {
                    log_error("Texture::getData: buffer is ", outCapacity, " bytes, need ", required,
                              " for level ", level, " of this texture");
                    return false;
                }
            } else {
                log_warn("Texture::getData: could not validate buffer size for this format/type "
                         "combination; proceeding on trust");
            }

            // The size check above assumes tightly packed rows, so pack alignment
            // must actually be 1 while GL writes into the destination buffer.
            int previousPack = 4;
            glGetIntegerv(GL_PACK_ALIGNMENT, &previousPack);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);

            #if ENGINE_HAS_DSA
                (void)cubeFace;
                glGetTextureImage(_texture, level, format, type, static_cast<GLsizei>(outCapacity), out);
            #else
                const int previous = boundTexture(_target);
                glBindTexture(_target, _texture);
                const unsigned int readTarget = (_type == TextureType::TextureCubeMap)
                    ? GL_TEXTURE_CUBE_MAP_POSITIVE_X + static_cast<unsigned int>(cubeFace) : _target;
                glGetTexImage(readTarget, level, format, type, out);
                glBindTexture(_target, static_cast<unsigned int>(previous));
            #endif

            glPixelStorei(GL_PACK_ALIGNMENT, previousPack);
            return true;
        }

        #if ENGINE_HAS_MIPMAP
            // Re-derives every mip level from the current level-0 data; needed
            // after createEmpty()+setData() when the spec asked for mipmaps.
            bool regenerateMipmaps() noexcept {
                #if ENGINE_HAS_DSA
                    glGenerateTextureMipmap(_texture);
                #else
                    const int previous = boundTexture(_target);
                    glBindTexture(_target, _texture);
                    glGenerateMipmap(_target);
                    glBindTexture(_target, static_cast<unsigned int>(previous));
                #endif
                return true;
            }
        #else
            // Mipmap regeneration needs GL 3.0; this stub warns and fails.
            bool regenerateMipmaps() noexcept {
                log_warn("Texture::regenerateMipmaps needs GL 3.0");
                return false;
            }
        #endif

        // ---- sampler state (runtime) ----

        // Enables or disables depth-compare sampling on this texture.
        void setCompareMode(bool enabled, int func = GL_LEQUAL) noexcept {
            _spec.depthCompare = enabled;
            _spec.compareFunc = func;
            #if ENGINE_HAS_DSA
                glTextureParameteri(_texture, GL_TEXTURE_COMPARE_MODE, enabled ? ENGINE_GL_COMPARE_REF : GL_NONE);
                glTextureParameteri(_texture, GL_TEXTURE_COMPARE_FUNC, func);
            #else
                const int previous = boundTexture(_target);
                glBindTexture(_target, _texture);
                glTexParameteri(_target, GL_TEXTURE_COMPARE_MODE, enabled ? ENGINE_GL_COMPARE_REF : GL_NONE);
                glTexParameteri(_target, GL_TEXTURE_COMPARE_FUNC, func);
                glBindTexture(_target, static_cast<unsigned int>(previous));
            #endif
        }

        // The GL bind target of this texture.
        unsigned int target() const noexcept {return _target;}
        // The engine-level texture type.
        TextureType type() const noexcept {return _type;}
        // The internal format the storage was created with.
        unsigned int internalFormat() const noexcept {return _internalFormat;}
        // True for GL_TEXTURE_2D_MULTISAMPLE textures.
        bool isMultisample() const noexcept {
            #if ENGINE_HAS_TEXTURE_MULTISAMPLE
                return _target == GL_TEXTURE_2D_MULTISAMPLE;
            #else
                return false;
            #endif
        }
        // True for cubemap textures.
        bool isCubemap() const noexcept {return _type == TextureType::TextureCubeMap;}
        // True for 2D-array textures.
        bool isArray() const noexcept {return _type == TextureType::Texture2DArray;}
        // True for 3D textures.
        bool is3D() const noexcept {return _type == TextureType::Texture3D;}
        // True when the internal format carries depth.
        bool isDepth() const noexcept {return texture_detail::isDepthFormat(_internalFormat);}

        // The GL texture name.
        unsigned int id() const noexcept {return _texture;}
        // Width in pixels.
        int width() const noexcept {return _width;}
        // Height in pixels.
        int height() const noexcept {return _height;}
        // Array layer count / 3D depth; 1 otherwise.
        int layers() const noexcept {return _layers;}
        // Channel count of the source image (0 for createEmpty textures).
        int channels() const noexcept {return _channels;}
        // Width and height together.
        TextureSize size() const noexcept {return {_width, _height};}

        // True when loaded with gutter expansion.
        bool hasGutters() const noexcept {return _cellPitch > 0;}
        // Original cell size in pixels (before gutters).
        int cellSize() const noexcept {return _cellSize;}
        // Cell-to-cell pitch in pixels, gutters included.
        int cellPitch() const noexcept {return _cellPitch > 0 ? _cellPitch : _cellSize;}
        // Cell columns available, or 0 without gutters.
        int cellsX() const noexcept {return _cellPitch > 0 ? _width / _cellPitch : 0;}
        // Cell rows available, or 0 without gutters.
        int cellsY() const noexcept {return _cellPitch > 0 ? _height / _cellPitch : 0;}

        // UVs of one cell's interior (gutters excluded); false when out of range.
        bool cellUV(int cx, int cy, UVRect& out) const noexcept {
            if (_cellPitch <= 0) {log_warn("cellUV called on a texture without gutters"); return false;}
            if (cx < 0 || cy < 0 || cx >= cellsX() || cy >= cellsY()) {
                log_warn("cellUV out of range: ", cx, ",", cy);
                return false;
            }
            const int x0 = cx * _cellPitch + 1;
            const int y0 = cy * _cellPitch + 1;
            out = texture_tools::pixelToUV({x0, y0, _cellSize, _cellSize}, _width, _height);
            return true;
        }

        ~Texture() noexcept {if (_texture) glDeleteTextures(1, &_texture);}

        Texture(const Texture&) = delete;
        Texture& operator=(const Texture&) = delete;
        Texture(Texture&& other) noexcept
            : _texture(std::exchange(other._texture, 0)),
              _target(other._target), _type(other._type), _layers(other._layers),
              _internalFormat(other._internalFormat),
              _width(other._width), _height(other._height), _channels(other._channels),
              _cellSize(other._cellSize), _cellPitch(other._cellPitch),
              _spec(other._spec), _fromSpec(other._fromSpec) {}

        Texture& operator=(Texture&& other) noexcept {
            if (this != &other) {
                if (_texture) glDeleteTextures(1, &_texture);
                _texture = std::exchange(other._texture, 0);
                _target = other._target;
                _type = other._type;
                _layers = other._layers;
                _internalFormat = other._internalFormat;
                _width = other._width;
                _height = other._height;
                _channels = other._channels;
                _cellSize = other._cellSize;
                _cellPitch = other._cellPitch;
                _spec = other._spec;
                _fromSpec = other._fromSpec;
            }
            return *this;
        }
    private:
        Texture(unsigned int texture, int width, int height, int channels, int cellSize, int cellPitch) noexcept
            : _texture(texture), _width(width), _height(height),
              _channels(channels), _cellSize(cellSize), _cellPitch(cellPitch) {}

        #if GL_TARGET_VERSION >= 30
            // Uploads decoded HDR float data as a float-format 2D texture.
            static std::unique_ptr<Texture> createFromHDR(const std::filesystem::path& path, float* data,
                                                           int width, int height, int channels,
                                                           const TextureLoadOptions& options) noexcept {
                const unsigned int internal = texture_detail::internalFormatFloat(channels, options.preferHighPrecisionFloat);
                const unsigned int external = texture_detail::externalFormat(channels);

                int previousAlignment = 4;
                glGetIntegerv(GL_UNPACK_ALIGNMENT, &previousAlignment);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
                GlErrorHandler::clear();

                unsigned int id = 0;

                #if ENGINE_HAS_DSA
                    glCreateTextures(GL_TEXTURE_2D, 1, &id);
                    texture_detail::applySamplerParamsDSA(id, options);
                    const int levels = options.generateMipmaps ? mipLevels(width, height) : 1;
                    glTextureStorage2D(id, levels, internal, width, height);
                    glTextureSubImage2D(id, 0, 0, 0, width, height, external, GL_FLOAT, data);
                    if (options.generateMipmaps) glGenerateTextureMipmap(id);
                #else
                    int previous = 0;
                    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
                    glGenTextures(1, &id);
                    glBindTexture(GL_TEXTURE_2D, id);
                    texture_detail::applySamplerParamsLegacy(GL_TEXTURE_2D, options);
                    #if ENGINE_HAS_TEXTURE_STORAGE
                        const int levels = options.generateMipmaps ? mipLevels(width, height) : 1;
                        glTexStorage2D(GL_TEXTURE_2D, levels, internal, width, height);
                        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height, external, GL_FLOAT, data);
                    #else
                        glTexImage2D(GL_TEXTURE_2D, 0, static_cast<int>(internal), width, height, 0,
                                     external, GL_FLOAT, data);
                    #endif
                    #if ENGINE_HAS_MIPMAP
                        if (options.generateMipmaps) glGenerateMipmap(GL_TEXTURE_2D);
                    #endif
                    glBindTexture(GL_TEXTURE_2D, static_cast<unsigned int>(previous));
                #endif

                glPixelStorei(GL_UNPACK_ALIGNMENT, previousAlignment);

                const GLenum error = glGetError();
                if (error != GL_NO_ERROR) {
                    log_error("Texture: GL error 0x", std::hex, error, std::dec, " uploading ", path);
                    glDeleteTextures(1, &id);
                    stbi_image_free(data);
                    return nullptr;
                }

                #if ENGINE_HAS_DEBUG_OUTPUT
                    if (!options.debugLabel.empty()) {
                        glObjectLabel(GL_TEXTURE, id, static_cast<GLsizei>(options.debugLabel.size()),
                                      options.debugLabel.c_str());
                    }
                #endif

                stbi_image_free(data);
                log_info("HDR texture loaded: ", path, " ", width, "x", height, " (", channels, " channel(s))");
                std::unique_ptr<Texture> texture(new Texture(id, width, height, channels, 0, 0));
                texture->_internalFormat = internal;
                return texture;
            }
        #endif

        // The GL bind target for a texture type (multisample overrides 2D).
        static unsigned int resolveTarget(TextureType type, bool multisample) noexcept {
            #if ENGINE_HAS_TEXTURE_MULTISAMPLE
                if (multisample) return GL_TEXTURE_2D_MULTISAMPLE;
            #else
                (void)multisample;
            #endif
            switch (type) {
                #if ENGINE_HAS_TEXTURE_ARRAY
                    case TextureType::Texture2DArray: return GL_TEXTURE_2D_ARRAY;
                #endif
                case TextureType::TextureCubeMap:     return GL_TEXTURE_CUBE_MAP;
                case TextureType::Texture3D:          return GL_TEXTURE_3D;
                default:                              return GL_TEXTURE_2D;
            }
        }

        // Number of mip levels for a full chain at the given size.
        static int mipLevels(int width, int height) noexcept {
            int size = width > height ? width : height;
            int levels = 1;
            while (size > 1) {size /= 2; ++levels;}
            return levels;
        }

        // The binding query enum for a texture target.
        static unsigned int bindingQueryFor(unsigned int target) noexcept {
            switch (target) {
                #if ENGINE_HAS_TEXTURE_MULTISAMPLE
                    case GL_TEXTURE_2D_MULTISAMPLE: return GL_TEXTURE_BINDING_2D_MULTISAMPLE;
                #endif
                case GL_TEXTURE_CUBE_MAP: return GL_TEXTURE_BINDING_CUBE_MAP;
                #if ENGINE_HAS_TEXTURE_ARRAY
                    case GL_TEXTURE_2D_ARRAY: return GL_TEXTURE_BINDING_2D_ARRAY;
                #endif
                case GL_TEXTURE_3D: return GL_TEXTURE_BINDING_3D;
                default: return GL_TEXTURE_BINDING_2D;
            }
        }

        // The texture currently bound to a target.
        static int boundTexture(unsigned int target) noexcept {
            int previous = 0;
            glGetIntegerv(bindingQueryFor(target), &previous);
            return previous;
        }

        unsigned int _texture = 0;
        unsigned int _target = GL_TEXTURE_2D;
        TextureType _type = TextureType::Texture2D;
        int _layers = 1;
        unsigned int _internalFormat = GL_RGBA8;
        int _width = 0;
        int _height = 0;
        int _channels = 0;
        int _cellSize = 0;
        int _cellPitch = 0;
        TextureSpec _spec;
        bool _fromSpec = false;
};

struct AtlasRegion {
    std::string name;
    PixelRect pixelRect;
    UVRect uvRect;
};

enum class AtlasPackingMode {MaxRects, Grid};

enum class AtlasGridOrder {Rows, Columns};

struct TextureAtlasConfig : texture_detail::SamplerParams {
    AtlasPackingMode mode = AtlasPackingMode::MaxRects;

    // ---- MaxRects mode ----
    int maxWidth = 0;
    int maxHeight = 0;

    // ---- Grid mode ----
    AtlasGridOrder gridOrder = AtlasGridOrder::Rows;
    int columns = 0;        // used when gridOrder == Rows and bestFit is off; 0 = one row
    int rows = 0;           // used when gridOrder == Columns and bestFit is off; 0 = one column
    int cellWidth = 0;      // 0 = use the widest source image
    int cellHeight = 0;     // 0 = use the tallest source image
    bool bestFit = true;    // auto-pick columns/rows for the most square sheet

    int padding = 0;

    int channels = 4;
    bool generateMipmaps = false;

    bool srgb = false;
    std::string debugLabel;
};

namespace texture_atlas_detail {

    struct RawImage {int width = 0, height = 0; std::vector<unsigned char> pixels;};

    // Loads one source image forced to the atlas-wide channel count, so the
    // packer works with one consistent byte stride throughout.
    inline bool loadImage(const std::filesystem::path& path, bool flipVertically,
                         bool premultiplyAlpha, int channels, RawImage& out) noexcept {
        stbi_set_flip_vertically_on_load(flipVertically ? 1 : 0);
        const auto& pathString = texture_detail::stbPath(path);

        int width = 0, height = 0, fileChannels = 0;
        unsigned char* data = stbi_load(pathString.c_str(), &width, &height, &fileChannels, channels);
        if (!data) {
            const char* reason = stbi_failure_reason();
            log_error("TextureAtlas: failed to load ", path, ": ", reason ? reason : "unknown");
            return false;
        }
        if (premultiplyAlpha) {
            if (channels == 4) {
                texture_detail::premultiply(data, width, height);
            } else {
                log_warn("TextureAtlas: premultiplyAlpha ignored, atlas channels = ", channels);
            }
        }

        out.width = width;
        out.height = height;
        out.pixels.assign(data, data + static_cast<size_t>(width) * static_cast<size_t>(height)
                                      * static_cast<size_t>(channels));
        stbi_image_free(data);
        return true;
    }

    // Reads a plain 2D texture back into CPU pixels at the atlas channel count.
    inline bool readTexture(const Texture& texture, int channels, RawImage& out) noexcept {
        if (texture.type() != TextureType::Texture2D || texture.isMultisample()) {
            log_error("TextureAtlas: only plain, non-multisample 2D textures can be packed");
            return false;
        }
        out.width = texture.width();
        out.height = texture.height();
        out.pixels.resize(static_cast<size_t>(out.width) * static_cast<size_t>(out.height)
                                * static_cast<size_t>(channels));
        return texture.getData(out.pixels.data(), out.pixels.size(),
                               texture_detail::externalFormat(channels), GL_UNSIGNED_BYTE);
    }

    // ---- MaxRects packer (Jylanki, "A Thousand Ways to Pack the Bin", 2010) ----
    struct FreeRect {int x = 0, y = 0, width = 0, height = 0;};

    // True when a rectangle of the given size fits inside the free rect.
    inline bool fits(const FreeRect& free, int width, int height) noexcept {
        return free.width >= width && free.height >= height;
    }

    // Does 'outer' fully contain 'inner'? Used to prune redundant free rects.
    inline bool contains(const FreeRect& outer, const FreeRect& inner) noexcept {
        return inner.x >= outer.x && inner.y >= outer.y &&
               inner.x + inner.width <= outer.x + outer.width &&
               inner.y + inner.height <= outer.y + outer.height;
    }

    // True when two free rects overlap.
    inline bool overlaps(const FreeRect& a, const FreeRect& b) noexcept {
        return a.x < b.x + b.width && a.x + a.width > b.x &&
               a.y < b.y + b.height && a.y + a.height > b.y;
    }

    // Splits every free rect around a placed rect and prunes contained leftovers.
    inline void splitAndPrune(std::vector<FreeRect>& freeRects, const FreeRect& used) noexcept {
        std::vector<FreeRect> next;
        next.reserve(freeRects.size() + 4);
        for (const FreeRect& free : freeRects) {
            if (!overlaps(free, used)) {next.push_back(free); continue;}
            if (used.x > free.x) {
                next.push_back({free.x, free.y, used.x - free.x, free.height});
            }
            if (used.x + used.width < free.x + free.width) {
                next.push_back({used.x + used.width, free.y,
                               (free.x + free.width) - (used.x + used.width), free.height});
            }
            if (used.y > free.y) {
                next.push_back({free.x, free.y, free.width, used.y - free.y});
            }
            if (used.y + used.height < free.y + free.height) {
                next.push_back({free.x, used.y + used.height, free.width,
                               (free.y + free.height) - (used.y + used.height)});
            }
        }
        freeRects.clear();
        for (size_t i = 0; i < next.size(); ++i) {
            if (next[i].width <= 0 || next[i].height <= 0) continue;
            bool redundant = false;
            for (size_t j = 0; j < next.size() && !redundant; ++j) {
                if (i != j && contains(next[j], next[i])) redundant = true;
            }
            if (!redundant) freeRects.push_back(next[i]);
        }
    }

    // Best-short-side-fit MaxRects pass; false when a rect does not fit the bin.
    inline bool tryPack(const std::vector<RawImage>& images, const std::vector<size_t>& order,
                        int binWidth, int binHeight, int padding, std::vector<PixelRect>& outRects) noexcept {
        std::vector<FreeRect> freeRects = {{0, 0, binWidth, binHeight}};
        outRects.assign(images.size(), PixelRect{});

        for (size_t index : order) {
            const int w = images[index].width + 2 * padding;
            const int h = images[index].height + 2 * padding;

            int bestIndex = -1, bestShortSide = 0, bestLongSide = 0;
            for (size_t i = 0; i < freeRects.size(); ++i) {
                const FreeRect& free = freeRects[i];
                if (!fits(free, w, h)) continue;
                const int shortSide = std::min(free.width - w, free.height - h);
                const int longSide = std::max(free.width - w, free.height - h);
                if (bestIndex == -1 || shortSide < bestShortSide ||
                    (shortSide == bestShortSide && longSide < bestLongSide)) {
                    bestIndex = static_cast<int>(i);
                    bestShortSide = shortSide;
                    bestLongSide = longSide;
                }
            }
            if (bestIndex == -1) return false;

            const FreeRect chosen = freeRects[static_cast<size_t>(bestIndex)];
            outRects[index] = {chosen.x + padding, chosen.y + padding, images[index].width, images[index].height};
            splitAndPrune(freeRects, {chosen.x, chosen.y, w, h});
        }
        return true;
    }

    // GL_MAX_TEXTURE_SIZE, with a conservative fallback if the query fails.
    inline int queryMaxTextureSize() noexcept {
        GLint value = 0;
        glGetIntegerv(GL_MAX_TEXTURE_SIZE, &value);
        if (value <= 0) {
            log_warn("TextureAtlas: GL_MAX_TEXTURE_SIZE query returned ", value, "; assuming 2048");
            return 2048;
        }
        return static_cast<int>(value);
    }

    // A power-of-two width big enough for the total padded area, capped.
    inline int suggestWidth(const std::vector<RawImage>& images, int padding, int hardCap) noexcept {
        long long area = 0;
        int maxSingleWidth = 1;
        for (const RawImage& image : images) {
            const int paddedWidth = image.width + 2 * padding;
            area += static_cast<long long>(paddedWidth) * (image.height + 2 * padding);
            maxSingleWidth = std::max(maxSingleWidth, paddedWidth);
        }
        int width = 64;
        while (static_cast<long long>(width) * width < area && width < hardCap) width *= 2;
        width = std::max(width, maxSingleWidth);
        return std::min(width, hardCap);
    }

    // Packs images at a fixed width, binary-searching the smallest fitting height.
    inline bool packImages(const std::vector<RawImage>& images, int maxWidth, int maxHeightCap,
                           int padding, std::vector<PixelRect>& outRects,
                           int& atlasWidth, int& atlasHeight) noexcept {
        for (const RawImage& image : images) {
            if (image.width + 2 * padding > maxWidth) {
                log_error("TextureAtlas: a ", image.width, "x", image.height,
                          " image (plus padding) doesn't fit within a ", maxWidth, "px texture width");
                return false;
            }
            if (image.height + 2 * padding > maxHeightCap) {
                log_error("TextureAtlas: a ", image.width, "x", image.height, " image (plus padding) doesn't "
                          "fit within a ", maxHeightCap, "px texture height (GL_MAX_TEXTURE_SIZE)");
                return false;
            }
        }

        std::vector<size_t> order(images.size());
        for (size_t i = 0; i < images.size(); ++i) order[i] = i;
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return std::max(images[a].width, images[a].height) > std::max(images[b].width, images[b].height);
        });

        long long safeHeight = 0;
        for (const RawImage& image : images) safeHeight += image.height + 2 * padding;
        int high = static_cast<int>(std::min<long long>(std::max<long long>(safeHeight, 1), maxHeightCap));

        while (!tryPack(images, order, maxWidth, high, padding, outRects)) {
            if (high >= maxHeightCap) {
                log_error("TextureAtlas: images don't fit within ", maxWidth, "x", maxHeightCap,
                          " (GL_MAX_TEXTURE_SIZE) with padding ", padding, " -- split into multiple atlases");
                return false;
            }
            high = std::min(high * 2, maxHeightCap);
        }

        int low = 0;
        while (high - low > 1) {
            const int mid = low + (high - low) / 2;
            if (tryPack(images, order, maxWidth, mid, padding, outRects)) high = mid;
            else low = mid;
        }
        tryPack(images, order, maxWidth, high, padding, outRects);
        int usedWidth = 1, usedHeight = 1;
        for (const PixelRect& rect : outRects) {
            usedWidth = std::max(usedWidth, rect.x + rect.width + padding);
            usedHeight = std::max(usedHeight, rect.y + rect.height + padding);
        }
        atlasWidth = usedWidth;
        atlasHeight = usedHeight;
        return true;
    }

    // Extrudes a region's edge pixels into its padding ring to stop bleeding.
    inline void extrudeEdges(std::vector<unsigned char>& atlasPixels, int atlasWidth, int channels,
                             const PixelRect& rect, int padding) noexcept {
        if (padding <= 0) return;
        const size_t stride = static_cast<size_t>(channels);

        const auto pixelAt = [&](int x, int y) -> unsigned char* {
            return &atlasPixels[(static_cast<size_t>(y) * static_cast<size_t>(atlasWidth)
                                 + static_cast<size_t>(x)) * stride];
        };

        for (int row = 0; row < rect.height; ++row) {
            const int y = rect.y + row;
            unsigned char left[4], right[4];
            std::memcpy(left, pixelAt(rect.x, y), stride);
            std::memcpy(right, pixelAt(rect.x + rect.width - 1, y), stride);
            for (int p = 1; p <= padding; ++p) {
                std::memcpy(pixelAt(rect.x - p, y), left, stride);
                std::memcpy(pixelAt(rect.x + rect.width - 1 + p, y), right, stride);
            }
        }
        for (int col = -padding; col < rect.width + padding; ++col) {
            const int x = rect.x + col;
            unsigned char top[4], bottom[4];
            std::memcpy(top, pixelAt(x, rect.y), stride);
            std::memcpy(bottom, pixelAt(x, rect.y + rect.height - 1), stride);
            for (int p = 1; p <= padding; ++p) {
                std::memcpy(pixelAt(x, rect.y - p), top, stride);
                std::memcpy(pixelAt(x, rect.y + rect.height - 1 + p), bottom, stride);
            }
        }
    }

    // Lays images out on a uniform grid sized from the largest image or config.
    inline bool packGrid(const std::vector<RawImage>& images, const TextureAtlasConfig& config,
                         std::vector<PixelRect>& outRects, int& atlasWidth, int& atlasHeight) noexcept {
        const int count = static_cast<int>(images.size());
        if (count == 0) {log_error("TextureAtlas: grid mode given no images"); return false;}

        // A configured cell is a floor, not a ceiling: an image larger than it
        // still grows the grid, which would otherwise clip. Say so rather than
        // silently returning a sheet whose cell size is not the one asked for.
        int cellWidth = config.cellWidth;
        int cellHeight = config.cellHeight;
        for (const RawImage& image : images) {
            if ((config.cellWidth > 0 && image.width > config.cellWidth)
                || (config.cellHeight > 0 && image.height > config.cellHeight)) {
                log_warn("TextureAtlas: a ", image.width, "x", image.height, " image exceeds the ",
                         config.cellWidth, "x", config.cellHeight,
                         " cell size; the grid cell grows to fit it");
            }
            cellWidth = std::max(cellWidth, image.width);
            cellHeight = std::max(cellHeight, image.height);
        }
        if (cellWidth <= 0 || cellHeight <= 0) {
            log_error("TextureAtlas: grid mode couldn't determine a cell size");
            return false;
        }

        int columns = 0, rows = 0;
        if (config.bestFit) {
            columns = static_cast<int>(std::ceil(std::sqrt(static_cast<double>(count))));
            rows = (count + columns - 1) / columns;
        } else if (config.gridOrder == AtlasGridOrder::Rows) {
            columns = config.columns > 0 ? config.columns : count;
            rows = (count + columns - 1) / columns;
        } else {
            rows = config.rows > 0 ? config.rows : count;
            columns = (count + rows - 1) / rows;
        }

        const int pitchX = cellWidth + 2 * config.padding;
        const int pitchY = cellHeight + 2 * config.padding;

        outRects.assign(images.size(), PixelRect{});
        for (int i = 0; i < count; ++i) {
            const int col = (config.gridOrder == AtlasGridOrder::Rows) ? i % columns : i / rows;
            const int row = (config.gridOrder == AtlasGridOrder::Rows) ? i / columns : i % rows;
            outRects[static_cast<size_t>(i)] = {
                col * pitchX + config.padding,
                row * pitchY + config.padding,
                images[static_cast<size_t>(i)].width,
                images[static_cast<size_t>(i)].height
            };
        }

        atlasWidth = columns * pitchX;
        atlasHeight = rows * pitchY;
        return true;
    }
}

class TextureAtlas {
    public:
        // Packs image files into one atlas, each with its own load options.
        static std::unique_ptr<TextureAtlas> createFromFiles(
                const std::vector<std::pair<std::filesystem::path, TextureLoadOptions>>& files,
                std::vector<std::string> names = {},
                const TextureAtlasConfig& config = {}) noexcept {
            using namespace texture_atlas_detail;
            if (files.empty()) {log_error("TextureAtlas: no file paths given"); return nullptr;}
            if (!names.empty() && names.size() != files.size()) {
                log_error("TextureAtlas: got ", names.size(), " names for ", files.size(), " files");
                return nullptr;
            }
            if (config.channels < 1 || config.channels > 4) {
                log_error("TextureAtlas: channels must be 1-4, got ", config.channels);
                return nullptr;
            }

            std::vector<RawImage> images(files.size());
            for (size_t i = 0; i < files.size(); ++i) {
                const TextureLoadOptions& options = files[i].second;
                if (!loadImage(files[i].first, options.flipVertically, options.premultiplyAlpha,
                              config.channels, images[i])) {
                    return nullptr;
                }
            }
            if (names.empty()) {
                names.resize(files.size());
                for (size_t i = 0; i < files.size(); ++i) names[i] = Paths::toUtf8(files[i].first.stem());
            }
            return buildFromImages(std::move(images), std::move(names), config);
        }

        // Convenience for the common case: every file loaded with the same options.
        static std::unique_ptr<TextureAtlas> createFromFiles(const std::vector<std::filesystem::path>& paths,
                                                              const TextureLoadOptions& options = {},
                                                              std::vector<std::string> names = {},
                                                              const TextureAtlasConfig& config = {}) noexcept {
            std::vector<std::pair<std::filesystem::path, TextureLoadOptions>> files;
            files.reserve(paths.size());
            for (const std::filesystem::path& path : paths) files.emplace_back(path, options);
            return createFromFiles(files, std::move(names), config);
        }

        // Packs existing 2D textures into one atlas via GPU readback.
        static std::unique_ptr<TextureAtlas> createFromTextures(
                const std::vector<std::reference_wrapper<const Texture>>& textures,
                std::vector<std::string> names = {},
                const TextureAtlasConfig& config = {}) noexcept {
            using namespace texture_atlas_detail;
            if (textures.empty()) {log_error("TextureAtlas: no textures given"); return nullptr;}
            if (!names.empty() && names.size() != textures.size()) {
                log_error("TextureAtlas: got ", names.size(), " names for ", textures.size(), " textures");
                return nullptr;
            }
            if (config.channels < 1 || config.channels > 4) {
                log_error("TextureAtlas: channels must be 1-4, got ", config.channels);
                return nullptr;
            }

            std::vector<RawImage> images(textures.size());
            for (size_t i = 0; i < textures.size(); ++i) {
                if (!readTexture(textures[i].get(), config.channels, images[i])) return nullptr;
            }
            return buildFromImages(std::move(images), std::move(names), config);
        }

        // The packed atlas texture.
        const Texture& texture() const noexcept {return *_texture;}
        // Atlas width in pixels.
        int width() const noexcept {return _texture->width();}
        // Atlas height in pixels.
        int height() const noexcept {return _texture->height();}
        // Number of packed regions.
        size_t regionCount() const noexcept {return _regions.size();}

        // A region by name, or null when absent.
        const AtlasRegion* region(const std::string& name) const noexcept {
            const auto found = _byName.find(name);
            return found != _byName.end() ? &_regions[found->second] : nullptr;
        }

        // A region by index, or null when out of range.
        const AtlasRegion* region(size_t index) const noexcept {
            if (index >= _regions.size()) {log_error("TextureAtlas: region index ", index, " out of range"); return nullptr;}
            return &_regions[index];
        }

        // A region's UV rectangle by name, or null when absent.
        const UVRect* uvRect(const std::string& name) const noexcept {
            const AtlasRegion* found = region(name);
            return found ? &found->uvRect : nullptr;
        }

        // A region's pixel rectangle by name, or null when absent.
        const PixelRect* pixelRect(const std::string& name) const noexcept {
            const AtlasRegion* found = region(name);
            return found ? &found->pixelRect : nullptr;
        }

        // Every region name, in packing order.
        std::vector<std::string> regionNames() const noexcept {
            std::vector<std::string> names;
            names.reserve(_regions.size());
            for (const AtlasRegion& entry : _regions) names.push_back(entry.name);
            return names;
        }

        virtual ~TextureAtlas() noexcept = default;
        TextureAtlas(const TextureAtlas&) = delete;
        TextureAtlas& operator=(const TextureAtlas&) = delete;

    protected:
        // Adopts the packed texture and regions, indexing regions by name.
        explicit TextureAtlas(std::unique_ptr<Texture> texture, std::vector<AtlasRegion> regions) noexcept
            : _texture(std::move(texture)), _regions(std::move(regions)) {
            _byName.reserve(_regions.size());
            for (size_t i = 0; i < _regions.size(); ++i) {
                if (!_byName.emplace(_regions[i].name, i).second) {
                    log_warn("TextureAtlas: duplicate region name \"", _regions[i].name, "\"; keeping the first");
                }
            }
        }

        // Builds named regions with UVs from packed pixel rectangles.
        static std::vector<AtlasRegion> makeRegions(std::vector<std::string> names,
                                                    const std::vector<PixelRect>& pixelRects,
                                                    int textureWidth, int textureHeight) noexcept {
            std::vector<AtlasRegion> regions;
            regions.reserve(pixelRects.size());
            for (size_t i = 0; i < pixelRects.size(); ++i) {
                AtlasRegion entry;
                entry.name = i < names.size() ? std::move(names[i]) : ("region_" + std::to_string(i));
                entry.pixelRect = pixelRects[i];
                entry.uvRect = texture_tools::pixelToUV(pixelRects[i], textureWidth, textureHeight);
                regions.push_back(std::move(entry));
            }
            return regions;
        }

    private:
        // Packs raw images, uploads the sheet, and assembles the atlas object.
        static std::unique_ptr<TextureAtlas> buildFromImages(std::vector<texture_atlas_detail::RawImage> images,
                                                              std::vector<std::string> names,
                                                              const TextureAtlasConfig& config) noexcept {
            using namespace texture_atlas_detail;

            if (images.empty()) {log_error("TextureAtlas: no images given"); return nullptr;}
            for (const RawImage& image : images) {
                if (image.width <= 0 || image.height <= 0) {
                    log_error("TextureAtlas: an image has non-positive dimensions");
                    return nullptr;
                }
            }
            if (names.empty()) {
                names.resize(images.size());
                for (size_t i = 0; i < images.size(); ++i) names[i] = "region_" + std::to_string(i);
            }
            if (config.padding < 0) {log_error("TextureAtlas: padding must be >= 0"); return nullptr;}

            const int hardwareMax = queryMaxTextureSize();
            std::vector<PixelRect> rects;
            int atlasWidth = 0, atlasHeight = 0;

            if (config.mode == AtlasPackingMode::Grid) {
                if (!packGrid(images, config, rects, atlasWidth, atlasHeight)) return nullptr;
                if (atlasWidth > hardwareMax || atlasHeight > hardwareMax) {
                    log_error("TextureAtlas: grid layout is ", atlasWidth, "x", atlasHeight,
                              ", which exceeds GL_MAX_TEXTURE_SIZE (", hardwareMax,
                              "); use fewer columns/rows or a smaller cell size");
                    return nullptr;
                }
            } else {
                int maxWidth = hardwareMax;
                if (config.maxWidth > 0) {
                    maxWidth = config.maxWidth;
                    if (maxWidth > hardwareMax) {
                        log_warn("TextureAtlas: maxWidth ", maxWidth, " exceeds GL_MAX_TEXTURE_SIZE (",
                                 hardwareMax, "); clamping");
                        maxWidth = hardwareMax;
                    }
                } else {
                    maxWidth = suggestWidth(images, config.padding, hardwareMax);
                }

                int maxHeightCap = hardwareMax;
                if (config.maxHeight > 0) {
                    maxHeightCap = config.maxHeight;
                    if (maxHeightCap > hardwareMax) {
                        log_warn("TextureAtlas: maxHeight ", maxHeightCap, " exceeds GL_MAX_TEXTURE_SIZE (",
                                 hardwareMax, "); clamping");
                        maxHeightCap = hardwareMax;
                    }
                }

                if (!packImages(images, maxWidth, maxHeightCap, config.padding, rects, atlasWidth, atlasHeight)) {
                    return nullptr;
                }
            }

            const size_t channels = static_cast<size_t>(config.channels);
            std::vector<unsigned char> atlasPixels(
                static_cast<size_t>(atlasWidth) * static_cast<size_t>(atlasHeight) * channels, 0);
            for (size_t i = 0; i < images.size(); ++i) {
                const RawImage& image = images[i];
                const PixelRect& rect = rects[i];
                for (int row = 0; row < image.height; ++row) {
                    const size_t srcOffset = static_cast<size_t>(row) * static_cast<size_t>(image.width) * channels;
                    const size_t dstOffset = (static_cast<size_t>(rect.y + row) * static_cast<size_t>(atlasWidth)
                                              + static_cast<size_t>(rect.x)) * channels;
                    std::memcpy(&atlasPixels[dstOffset], &image.pixels[srcOffset],
                               static_cast<size_t>(image.width) * channels);
                }
                extrudeEdges(atlasPixels, atlasWidth, config.channels, rect, config.padding);
            }

            TextureSpec spec;
            static_cast<texture_detail::SamplerParams&>(spec) = config;
            spec.width = atlasWidth;
            spec.height = atlasHeight;
            spec.internalFormat = texture_detail::internalFormat(config.channels, config.srgb);
            spec.generateMipmaps = config.generateMipmaps;
            spec.debugLabel = config.debugLabel;

            auto texture = Texture::createEmpty(spec);
            if (!texture) return nullptr;
            const unsigned int externalFormat = texture_detail::externalFormat(config.channels);
            if (!texture->setData(atlasPixels.data(), externalFormat, GL_UNSIGNED_BYTE)) return nullptr;
            if (config.generateMipmaps) texture->regenerateMipmaps();

            std::vector<AtlasRegion> regions = makeRegions(std::move(names), rects, atlasWidth, atlasHeight);

            log_info("TextureAtlas packed (", (config.mode == AtlasPackingMode::Grid ? "grid" : "MaxRects"),
                     "): ", images.size(), " region(s) into ", atlasWidth, "x", atlasHeight,
                     " (limit ", hardwareMax, ", padding ", config.padding, ")");
            return std::unique_ptr<TextureAtlas>(new TextureAtlas(std::move(texture), std::move(regions)));
        }

        std::unique_ptr<Texture> _texture;
        std::vector<AtlasRegion> _regions;
        std::unordered_map<std::string, size_t> _byName;
};