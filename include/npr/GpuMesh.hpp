#pragma once

#include <cstddef>

#include "npr/MeshData.hpp"

namespace npr {

class GpuMesh {
public:
    explicit GpuMesh(const MeshData& mesh);
    ~GpuMesh();

    GpuMesh(const GpuMesh&) = delete;
    GpuMesh& operator=(const GpuMesh&) = delete;
    GpuMesh(GpuMesh&&) = delete;
    GpuMesh& operator=(GpuMesh&&) = delete;

    void draw() const;

private:
    void release() noexcept;

    unsigned int vertex_array_{};
    unsigned int vertex_buffer_{};
    unsigned int element_buffer_{};
    int index_count_{};
};

}  // namespace npr
