#include "gl_texture.h"

#include <array>
#include <set>

#include "spl/spl_resource.h"
#include "gl_util.h"

#include <GL/glew.h>


GLTexture::GLTexture(const SPLTexture& texture) 
    : m_width(texture.width), m_height(texture.height), m_format(texture.param.format) {
    createTexture(texture);
}

GLTexture::GLTexture(size_t width, size_t height) 
    : m_width(width), m_height(height), m_format(TextureFormat::Direct) {
    glCall(glGenTextures(1, &m_texture));
    glCall(glBindTexture(GL_TEXTURE_2D, m_texture));

    glCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    glCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    glCall(glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_S,
        GL_MIRRORED_REPEAT
    ));
    glCall(glTexParameteri(
        GL_TEXTURE_2D,
        GL_TEXTURE_WRAP_T,
        GL_MIRRORED_REPEAT
    ));

    glCall(glTexStorage2D(
        GL_TEXTURE_2D,
        1,
        GL_RGBA8,
        (s32)m_width,
        (s32)m_height
    ));

    glCall(glBindTexture(GL_TEXTURE_2D, 0));
}

GLTexture::GLTexture(GLTexture&& other) noexcept {
    if (this != &other) {
        m_texture = other.m_texture;
        m_width = other.m_width;
        m_height = other.m_height;
        m_format = other.m_format;

        other.m_texture = 0;
        other.m_width = 0;
        other.m_height = 0;
        other.m_format = TextureFormat::None;
    }
}

GLTexture& GLTexture::operator=(GLTexture&& other) noexcept {
    if (this != &other) {
        m_texture = other.m_texture;
        m_width = other.m_width;
        m_height = other.m_height;
        m_format = other.m_format;

        other.m_texture = 0;
        other.m_width = 0;
        other.m_height = 0;
        other.m_format = TextureFormat::None;
    }

    return *this;
}

GLTexture::~GLTexture() {
    if (m_texture == 0) { // Was moved from
        return;
    }

    glCall(glDeleteTextures(1, &m_texture));
}

void GLTexture::bind() const {
    glCall(glBindTexture(GL_TEXTURE_2D, m_texture));
}

void GLTexture::unbind() {
    glCall(glBindTexture(GL_TEXTURE_2D, 0));
}

void GLTexture::update(const void* rgba) {
    bind();
    glCall(glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        0,
        0,
        m_width,
        m_height,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        rgba
    ));
}

void GLTexture::update(const SPLTexture& texture) {
    if (m_width != texture.width || m_height != texture.height) {
        spdlog::error("Texture size mismatch: expected {}x{}, got {}x{}",
            m_width, m_height, texture.width, texture.height);
        return;
    }

    bind();

    setWrapping(texture.param.repeat, texture.param.flip);

    const auto textureData = toRGBA(texture);
    glCall(glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        0,
        0,
        (s32)m_width,
        (s32)m_height,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        textureData.data()
    ));
}

#define S_OR_ST(FLIP) ((FLIP) == TextureFlip::S || (FLIP) == TextureFlip::ST)
#define T_OR_ST(FLIP) ((FLIP) == TextureFlip::T || (FLIP) == TextureFlip::ST)

void GLTexture::setWrapping(TextureRepeat repeat, TextureFlip flip) {
    bind();

    GLint s = GL_CLAMP_TO_EDGE;
    GLint t = GL_CLAMP_TO_EDGE;

    switch (repeat) {
    case TextureRepeat::None:
        s = GL_CLAMP_TO_EDGE;
        t = GL_CLAMP_TO_EDGE;
        break;
    case TextureRepeat::S:
        s = S_OR_ST(flip) ? GL_MIRRORED_REPEAT : GL_REPEAT;
        break;
    case TextureRepeat::T:
        t = T_OR_ST(flip) ? GL_MIRRORED_REPEAT : GL_REPEAT;
        break;
    case TextureRepeat::ST:
        s = S_OR_ST(flip) ? GL_MIRRORED_REPEAT : GL_REPEAT;
        t = T_OR_ST(flip) ? GL_MIRRORED_REPEAT : GL_REPEAT;
        break;
    }

    glCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, s));
    glCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, t));
}

#undef S_OR_ST
#undef T_OR_ST

void GLTexture::createTexture(const SPLTexture& texture) {

    // Texture creation is a 2 step process. First the texture/palette data must be converted
    // to a format that OpenGL can understand (RGBA32). Then the texture will be uploaded to the GPU.

    // Step 1: Conversion
    const auto textureData = toRGBA(texture);
    const auto repeat = (TextureRepeat)texture.param.repeat;

    // Step 2: Upload to GPU
    glCall(glGenTextures(1, &m_texture));
    glCall(glBindTexture(GL_TEXTURE_2D, m_texture));

    glCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST));
    glCall(glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST));
    setWrapping(repeat, texture.param.flip);

    // Required for glTextureView (see spl_archive.cpp)
    glCall(glTexStorage2D(
        GL_TEXTURE_2D,
        1,
        GL_RGBA8,
        (s32)m_width,
        (s32)m_height
    ));

    glCall(glTexSubImage2D(
        GL_TEXTURE_2D,
        0,
        0,
        0,
        (s32)m_width,
        (s32)m_height,
        GL_RGBA,
        GL_UNSIGNED_BYTE,
        textureData.data()
    ));

    glCall(glBindTexture(GL_TEXTURE_2D, 0));
}

std::vector<u8> GLTexture::toRGBA(const SPLTexture& texture) {
    switch ((TextureFormat)texture.param.format) {
    case TextureFormat::A3I5:
        return convertA3I5(
            texture.textureData.data(),
            (const GXRgba*)texture.paletteData.data(),
            texture.width,
            texture.height,
            texture.paletteData.size()
        );
    case TextureFormat::Palette4:
        return convertPalette4(
            texture.textureData.data(),
            (const GXRgba*)texture.paletteData.data(),
            texture.width,
            texture.height,
            texture.paletteData.size(),
            texture.param.palColor0Transparent
        );
    case TextureFormat::Palette16:
        return convertPalette16(
            texture.textureData.data(),
            (const GXRgba*)texture.paletteData.data(),
            texture.width,
            texture.height,
            texture.paletteData.size(),
            texture.param.palColor0Transparent
        );
    case TextureFormat::Palette256:
        return convertPalette256(
            texture.textureData.data(),
            (const GXRgba*)texture.paletteData.data(),
            texture.width,
            texture.height,
            texture.paletteData.size(),
            texture.param.palColor0Transparent
        );
    case TextureFormat::Comp4x4:
        return convertComp4x4(
            texture.textureData.data(),
            (const GXRgba*)texture.paletteData.data(),
            texture.width,
            texture.height,
            texture.paletteData.size()
        );
    case TextureFormat::A5I3:
        return convertA5I3(
            texture.textureData.data(),
            (const GXRgba*)texture.paletteData.data(),
            texture.width,
            texture.height,
            texture.paletteData.size()
        );
    case TextureFormat::Direct:
        return convertDirect(
            (const GXRgba*)texture.textureData.data(),
            texture.width,
            texture.height
        );
    default:
        spdlog::error("Unsupported texture format: {}", (int)texture.param.format);
        return {};
    }
}

std::vector<u8> GLTexture::convertA3I5(const u8* tex, const GXRgba* pal, size_t width, size_t height, size_t palSize) {
    std::vector<u8> texture(width * height * 4);
    const auto pixels = reinterpret_cast<const PixelA3I5*>(tex);

    for (size_t i = 0; i < width * height; i++) {
        GXRgba color = pal[pixels[i].color];

        texture[i * 4 + 0] = color.r8();
        texture[i * 4 + 1] = color.g8();
        texture[i * 4 + 2] = color.b8();
        texture[i * 4 + 3] = pixels[i].getAlpha();
    }

    return texture;
}

std::vector<u8> GLTexture::convertPalette4(const u8* tex, const GXRgba* pal, size_t width, size_t height, size_t palSize, bool color0Transparent) {
    std::vector<u8> texture(width * height * 4);
    const auto pixels = tex;
    const u8 alpha0 = color0Transparent ? 0 : 0xFF;

    for (size_t i = 0; i < width * height; i += 4) {
        const u8 pixel = pixels[i / 4];

        u8 index = pixel & 0x3;
        texture[i * 4 + 0] = pal[index].r8();
        texture[i * 4 + 1] = pal[index].g8();
        texture[i * 4 + 2] = pal[index].b8();
        texture[i * 4 + 3] = index == 0 ? alpha0 : 0xFF;

        index = (pixel >> 2) & 0x3;
        texture[i * 4 + 4] = pal[index].r8();
        texture[i * 4 + 5] = pal[index].g8();
        texture[i * 4 + 6] = pal[index].b8();
        texture[i * 4 + 7] = index == 0 ? alpha0 : 0xFF;

        index = (pixel >> 4) & 0x3;
        texture[i * 4 + 8] = pal[index].r8();
        texture[i * 4 + 9] = pal[index].g8();
        texture[i * 4 + 10] = pal[index].b8();
        texture[i * 4 + 11] = index == 0 ? alpha0 : 0xFF;

        index = (pixel >> 6) & 0x3;
        texture[i * 4 + 12] = pal[index].r8();
        texture[i * 4 + 13] = pal[index].g8();
        texture[i * 4 + 14] = pal[index].b8();
        texture[i * 4 + 15] = index == 0 ? alpha0 : 0xFF;
    }

    return texture;
}

std::vector<u8> GLTexture::convertPalette16(const u8* tex, const GXRgba* pal, size_t width, size_t height, size_t palSize, bool color0Transparent) {
    std::vector<u8> texture(width * height * 4);
    const auto pixels = tex;
    const u8 alpha0 = color0Transparent ? 0 : 0xFF;

    for (size_t i = 0; i < width * height; i += 2) {
        const u8 pixel = pixels[i / 2];

        u8 index = pixel & 0xF;
        texture[i * 4 + 0] = pal[index].r8();
        texture[i * 4 + 1] = pal[index].g8();
        texture[i * 4 + 2] = pal[index].b8();
        texture[i * 4 + 3] = index == 0 ? alpha0 : 0xFF;

        index = (pixel >> 4) & 0xF;
        texture[i * 4 + 4] = pal[index].r8();
        texture[i * 4 + 5] = pal[index].g8();
        texture[i * 4 + 6] = pal[index].b8();
        texture[i * 4 + 7] = index == 0 ? alpha0 : 0xFF;
    }

    return texture;
}

std::vector<u8> GLTexture::convertPalette256(const u8* tex, const GXRgba* pal, size_t width, size_t height, size_t palSize, bool color0Transparent) {
    std::vector<u8> texture(width * height * 4);
    const auto pixels = tex;
    const u8 alpha0 = color0Transparent ? 0 : 0xFF;

    for (size_t i = 0; i < width * height; i++) {
        const u8 pixel = pixels[i];
        const u8 index = pixel;
        texture[i * 4 + 0] = pal[index].r8();
        texture[i * 4 + 1] = pal[index].g8();
        texture[i * 4 + 2] = pal[index].b8();
        texture[i * 4 + 3] = index == 0 ? alpha0 : 0xFF;
    }

    return texture;
}

std::vector<u8> GLTexture::convertComp4x4(const u8* tex, const GXRgba* pal, size_t width, size_t height, size_t palSize) {
    spdlog::warn("GLTexture::convertComp4x4 not implemented");
    return {};
}

std::vector<u8> GLTexture::convertA5I3(const u8* tex, const GXRgba* pal, size_t width, size_t height, size_t palSize) {
    std::vector<u8> texture(width * height * 4);
    const auto pixels = reinterpret_cast<const PixelA5I3*>(tex);

    for (size_t i = 0; i < width * height; i++) {
        GXRgba color = pal[pixels[i].color];

        texture[i * 4 + 0] = color.r8();
        texture[i * 4 + 1] = color.g8();
        texture[i * 4 + 2] = color.b8();
        texture[i * 4 + 3] = pixels[i].getAlpha();
    }

    return texture;
}

std::vector<u8> GLTexture::convertDirect(const GXRgba* tex, size_t width, size_t height) {
    std::vector<u8> texture(width * height * 4);

    for (size_t i = 0; i < width * height; i++) {
        texture[i * 4 + 0] = tex[i].r8();
        texture[i * 4 + 1] = tex[i].g8();
        texture[i * 4 + 2] = tex[i].b8();
        texture[i * 4 + 3] = tex[i].a ? 0xFF : 0x00;
    }

    return texture;
}
