#pragma once

#include <filesystem>
#include <string_view>

#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace crystalbound {

class ShaderProgram {
public:
    ShaderProgram(
        const std::filesystem::path& vertex_path,
        const std::filesystem::path& fragment_path);
    ~ShaderProgram();

    ShaderProgram(const ShaderProgram&) = delete;
    ShaderProgram& operator=(const ShaderProgram&) = delete;
    ShaderProgram(ShaderProgram&&) = delete;
    ShaderProgram& operator=(ShaderProgram&&) = delete;

    void use() const;
    void set_integer(std::string_view name, int value) const;
    void set_float(std::string_view name, float value) const;
    void set_vector(std::string_view name, const glm::vec3& value) const;
    void set_matrix(std::string_view name, const glm::mat3& value) const;
    void set_matrix(std::string_view name, const glm::mat4& value) const;

private:
    [[nodiscard]] int uniform_location(std::string_view name) const;

    unsigned int program_id_{};
};

}  // namespace crystalbound
