#include "crystalbound/GpuTexture.hpp"

#include <limits>
#include <stdexcept>
#include <string>

#include <glad/gl.h>

namespace crystalbound {
namespace {

[[nodiscard]] int internal_format(const TextureFormat format) noexcept
{
    return format == TextureFormat::r8_linear ? GL_R8 : GL_SRGB8;
}

[[nodiscard]] unsigned int source_format(const TextureFormat format) noexcept
{
    return format == TextureFormat::r8_linear ? GL_RED : GL_RGB;
}

}  // namespace

GpuTexture::GpuTexture(const TextureImage& image)
    : fingerprint_(texture_byte_fingerprint(image))
{
    validate_texture_image(image);
    if (image.width > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max())
        || image.height > static_cast<std::uint32_t>(std::numeric_limits<GLsizei>::max())) {
        throw std::overflow_error("Texture dimensions exceed OpenGL limits.");
    }

    int previous_unpack_alignment{};
    glGetIntegerv(GL_UNPACK_ALIGNMENT, &previous_unpack_alignment);
    try {
        glGenTextures(1, &texture_id_);
        if (texture_id_ == 0U) {
            throw std::runtime_error("OpenGL could not allocate a texture object.");
        }
        glBindTexture(GL_TEXTURE_2D, texture_id_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexImage2D(GL_TEXTURE_2D, 0, internal_format(image.format),
            static_cast<GLsizei>(image.width), static_cast<GLsizei>(image.height), 0,
            source_format(image.format), GL_UNSIGNED_BYTE, image.bytes.data());
        glPixelStorei(GL_UNPACK_ALIGNMENT, previous_unpack_alignment);
        glBindTexture(GL_TEXTURE_2D, 0);

        const unsigned int error{glGetError()};
        if (error != GL_NO_ERROR) {
            throw std::runtime_error(
                "OpenGL texture upload failed with error " + std::to_string(error) + '.');
        }
    } catch (...) {
        glPixelStorei(GL_UNPACK_ALIGNMENT, previous_unpack_alignment);
        glBindTexture(GL_TEXTURE_2D, 0);
        if (texture_id_ != 0U) {
            glDeleteTextures(1, &texture_id_);
            texture_id_ = 0U;
        }
        throw;
    }
}

GpuTexture::~GpuTexture()
{
    if (texture_id_ != 0U) {
        glDeleteTextures(1, &texture_id_);
    }
}

void GpuTexture::bind(const unsigned int texture_unit) const
{
    if (texture_unit >= 32U) {
        throw std::invalid_argument("Texture unit is outside Crystalbound's bounded range.");
    }
    glActiveTexture(GL_TEXTURE0 + texture_unit);
    glBindTexture(GL_TEXTURE_2D, texture_id_);
}

std::uint64_t GpuTexture::fingerprint() const noexcept
{
    return fingerprint_;
}

}  // namespace crystalbound
