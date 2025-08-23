#pragma once
#include "types.h"

#include <vector>


struct SPLTexture;

class GLTexture {
public:
    explicit GLTexture(const SPLTexture& texture);
    GLTexture(size_t width, size_t height);
    GLTexture(size_t width, size_t height, const void* rgba8888);
    GLTexture(const GLTexture& other) = delete;
    GLTexture(GLTexture&& other) noexcept;

    GLTexture& operator=(const GLTexture& other) = delete;
    GLTexture& operator=(GLTexture&& other) noexcept;

    ~GLTexture();

    void bind() const;
    static void unbind();

    u32 getHandle() const { return m_texture; }
    size_t getWidth() const { return m_width; }
    size_t getHeight() const { return m_height; }
    TextureFormat getFormat() const { return m_format; }

    void update(const void* rgba);
    void update(const SPLTexture& texture);
    void setWrapping(TextureRepeat repeat, TextureFlip flip);

private:
    void createTexture(const SPLTexture& texture);
    static std::vector<u8> toRGBA(const SPLTexture& texture);

    static std::vector<u8> convertA3I5(const u8* tex, const GXRgba* pal, size_t width, size_t height, size_t palSize);
    static std::vector<u8> convertPalette4(const u8* tex, const GXRgba* pal, size_t width, size_t height, size_t palSize, bool color0Transparent);
    static std::vector<u8> convertPalette16(const u8* tex, const GXRgba* pal, size_t width, size_t height, size_t palSize, bool color0Transparent);
    static std::vector<u8> convertPalette256(const u8* tex, const GXRgba* pal, size_t width, size_t height, size_t palSize, bool color0Transparent);
    static std::vector<u8> convertComp4x4(const u8* tex, const GXRgba* pal, size_t width, size_t height, size_t palSize);
    static std::vector<u8> convertA5I3(const u8* tex, const GXRgba* pal, size_t width, size_t height, size_t palSize);
    static std::vector<u8> convertDirect(const GXRgba* tex, size_t width, size_t height);

private:
    u32 m_texture;
    size_t m_width;
    size_t m_height;
    TextureFormat m_format;

    friend class SPLTexture;
};

struct PixelA3I5 {
    u8 color : 5;
    u8 alpha : 3;

    u8 getAlpha() const {
        return (alpha << 5) | (alpha << 2) | (alpha >> 1);
    }

    void setAlpha(u8 a) {
        alpha = (a >> 5) & 0x7;
    }
};

struct PixelA5I3 {
    u8 color : 3;
    u8 alpha : 5;

    u8 getAlpha() const {
        return (alpha << 3) | (alpha >> 2);
    }

    void setAlpha(u8 a) {
        alpha = (a >> 3) & 0x1F;
    }
};
