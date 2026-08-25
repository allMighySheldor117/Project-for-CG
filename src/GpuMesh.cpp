#include "crystalbound/GpuMesh.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

#include <glad/gl.h>

namespace crystalbound {
namespace {

static_assert(std::is_standard_layout_v<Vertex>);
static_assert(sizeof(Vertex) == 8 * sizeof(float));
static_assert(offsetof(Vertex, normal) == 3 * sizeof(float));
static_assert(offsetof(Vertex, texture_coordinates) == 6 * sizeof(float));

#if !defined(NDEBUG)
void require_no_opengl_error()
{
    const unsigned int error = glGetError();
    if (error != GL_NO_ERROR) {
        throw std::runtime_error(
            "OpenGL error while uploading mesh data: " + std::to_string(error));
    }
}
#endif

}  // namespace

GpuMesh::GpuMesh(const MeshData& mesh)
{
    validate_mesh_data(mesh);

    const auto maximum_buffer_size = static_cast<std::size_t>(
        std::numeric_limits<GLsizeiptr>::max());
    if (mesh.vertices.size() > maximum_buffer_size / sizeof(Vertex)
        || mesh.indices.size() > maximum_buffer_size / sizeof(std::uint32_t)) {
        throw std::overflow_error("Mesh data is too large for an OpenGL buffer.");
    }
    if (mesh.indices.size() > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) {
        throw std::overflow_error("Mesh has too many indices for one OpenGL draw call.");
    }

    try {
        glGenVertexArrays(1, &vertex_array_);
        glGenBuffers(1, &vertex_buffer_);
        glGenBuffers(1, &element_buffer_);
        if (vertex_array_ == 0 || vertex_buffer_ == 0 || element_buffer_ == 0) {
            throw std::runtime_error("OpenGL could not allocate mesh objects.");
        }

        glBindVertexArray(vertex_array_);
        glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(mesh.vertices.size() * sizeof(Vertex)),
            mesh.vertices.data(),
            GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, element_buffer_);
        glBufferData(
            GL_ELEMENT_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(mesh.indices.size() * sizeof(std::uint32_t)),
            mesh.indices.data(),
            GL_STATIC_DRAW);

        glVertexAttribPointer(
            0,
            3,
            GL_FLOAT,
            GL_FALSE,
            static_cast<GLsizei>(sizeof(Vertex)),
            nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            1,
            3,
            GL_FLOAT,
            GL_FALSE,
            static_cast<GLsizei>(sizeof(Vertex)),
            reinterpret_cast<const void*>(offsetof(Vertex, normal)));
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            2,
            2,
            GL_FLOAT,
            GL_FALSE,
            static_cast<GLsizei>(sizeof(Vertex)),
            reinterpret_cast<const void*>(offsetof(Vertex, texture_coordinates)));
        glEnableVertexAttribArray(2);

        index_count_ = static_cast<int>(mesh.indices.size());
#if !defined(NDEBUG)
        require_no_opengl_error();
#endif
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    } catch (...) {
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        release();
        throw;
    }
}

GpuMesh::~GpuMesh()
{
    release();
}

void GpuMesh::draw() const
{
    glBindVertexArray(vertex_array_);
    glDrawElements(GL_TRIANGLES, index_count_, GL_UNSIGNED_INT, nullptr);
    glBindVertexArray(0);
}

void GpuMesh::release() noexcept
{
    if (element_buffer_ != 0) {
        glDeleteBuffers(1, &element_buffer_);
        element_buffer_ = 0;
    }
    if (vertex_buffer_ != 0) {
        glDeleteBuffers(1, &vertex_buffer_);
        vertex_buffer_ = 0;
    }
    if (vertex_array_ != 0) {
        glDeleteVertexArrays(1, &vertex_array_);
        vertex_array_ = 0;
    }
    index_count_ = 0;
}

}  // namespace crystalbound
