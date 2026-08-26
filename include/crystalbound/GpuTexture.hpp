#pragma once

#include <cstdint>

#include "crystalbound/Rendering.hpp"

namespace crystalbound {

class GpuTexture {
public:
    explicit GpuTexture(const TextureImage& image);
    ~GpuTexture();

    GpuTexture(const GpuTexture&) = delete;
    GpuTexture& operator=(const GpuTexture&) = delete;
    GpuTexture(GpuTexture&&) = delete;
    GpuTexture& operator=(GpuTexture&&) = delete;

    void bind(unsigned int texture_unit) const;
    [[nodiscard]] std::uint64_t fingerprint() const noexcept;

private:
    unsigned int texture_id_{};
    std::uint64_t fingerprint_{};
};

}  // namespace crystalbound
