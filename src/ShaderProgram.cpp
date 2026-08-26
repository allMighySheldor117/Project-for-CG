#include "crystalbound/ShaderProgram.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glad/gl.h>
#include <glm/gtc/type_ptr.hpp>

namespace crystalbound {
namespace {

class ShaderHandle {
public:
    explicit ShaderHandle(const unsigned int shader) noexcept
        : shader_(shader)
    {
    }

    ~ShaderHandle()
    {
        if (shader_ != 0) {
            glDeleteShader(shader_);
        }
    }

    ShaderHandle(const ShaderHandle&) = delete;
    ShaderHandle& operator=(const ShaderHandle&) = delete;
    ShaderHandle(ShaderHandle&& other) noexcept
        : shader_(std::exchange(other.shader_, 0))
    {
    }
    ShaderHandle& operator=(ShaderHandle&&) = delete;

    [[nodiscard]] unsigned int get() const noexcept
    {
        return shader_;
    }

private:
    unsigned int shader_{};
};

class ProgramHandle {
public:
    explicit ProgramHandle(const unsigned int program) noexcept
        : program_(program)
    {
    }

    ~ProgramHandle()
    {
        if (program_ != 0) {
            glDeleteProgram(program_);
        }
    }

    ProgramHandle(const ProgramHandle&) = delete;
    ProgramHandle& operator=(const ProgramHandle&) = delete;
    ProgramHandle(ProgramHandle&&) = delete;
    ProgramHandle& operator=(ProgramHandle&&) = delete;

    [[nodiscard]] unsigned int get() const noexcept
    {
        return program_;
    }

    [[nodiscard]] unsigned int release() noexcept
    {
        const unsigned int program = program_;
        program_ = 0;
        return program;
    }

private:
    unsigned int program_{};
};

[[nodiscard]] std::string display_path(const std::filesystem::path& path)
{
    return path.u8string();
}

[[nodiscard]] std::string read_shader_source(const std::filesystem::path& path)
{
    std::ifstream stream{path, std::ios::binary};
    if (!stream) {
        throw std::runtime_error("Unable to open shader file: " + display_path(path));
    }

    std::ostringstream contents;
    contents << stream.rdbuf();
    if (stream.bad()) {
        throw std::runtime_error("Unable to read shader file: " + display_path(path));
    }
    return contents.str();
}

[[nodiscard]] std::string shader_log(const unsigned int shader)
{
    int log_length{};
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
    std::vector<char> log(static_cast<std::size_t>(std::max(log_length, 1)));
    int written{};
    glGetShaderInfoLog(shader, static_cast<int>(log.size()), &written, log.data());
    return std::string{log.data(), static_cast<std::size_t>(std::max(written, 0))};
}

[[nodiscard]] std::string program_log(const unsigned int program)
{
    int log_length{};
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
    std::vector<char> log(static_cast<std::size_t>(std::max(log_length, 1)));
    int written{};
    glGetProgramInfoLog(program, static_cast<int>(log.size()), &written, log.data());
    return std::string{log.data(), static_cast<std::size_t>(std::max(written, 0))};
}

[[nodiscard]] ShaderHandle compile_shader(
    const std::filesystem::path& path,
    const unsigned int shader_type,
    const std::string_view stage_name)
{
    const std::string source = read_shader_source(path);
    ShaderHandle shader{glCreateShader(shader_type)};
    if (shader.get() == 0) {
        throw std::runtime_error(
            "OpenGL could not create the " + std::string{stage_name}
            + " shader object for: " + display_path(path));
    }

    const char* source_pointer = source.c_str();
    glShaderSource(shader.get(), 1, &source_pointer, nullptr);
    glCompileShader(shader.get());

    int compiled{};
    glGetShaderiv(shader.get(), GL_COMPILE_STATUS, &compiled);
    if (compiled == GL_FALSE) {
        const std::string log = shader_log(shader.get());
        throw std::runtime_error(
            "Failed to compile " + std::string{stage_name} + " shader '"
            + display_path(path) + "':\n" + log);
    }
    return shader;
}

}  // namespace

ShaderProgram::ShaderProgram(
    const std::filesystem::path& vertex_path,
    const std::filesystem::path& fragment_path)
{
    const ShaderHandle vertex_shader = compile_shader(
        vertex_path, GL_VERTEX_SHADER, "vertex");
    const ShaderHandle fragment_shader = compile_shader(
        fragment_path, GL_FRAGMENT_SHADER, "fragment");

    ProgramHandle candidate_program{glCreateProgram()};
    if (candidate_program.get() == 0) {
        throw std::runtime_error(
            "OpenGL could not create a program for shaders '" + display_path(vertex_path)
            + "' and '" + display_path(fragment_path) + "'.");
    }

    glAttachShader(candidate_program.get(), vertex_shader.get());
    glAttachShader(candidate_program.get(), fragment_shader.get());
    glLinkProgram(candidate_program.get());

    int linked{};
    glGetProgramiv(candidate_program.get(), GL_LINK_STATUS, &linked);
    const std::string log = linked == GL_FALSE
        ? program_log(candidate_program.get())
        : std::string{};

    glDetachShader(candidate_program.get(), fragment_shader.get());
    glDetachShader(candidate_program.get(), vertex_shader.get());

    if (linked == GL_FALSE) {
        throw std::runtime_error(
            "Failed to link shader program from '" + display_path(vertex_path) + "' and '"
            + display_path(fragment_path) + "':\n" + log);
    }

    program_id_ = candidate_program.release();
}

ShaderProgram::~ShaderProgram()
{
    if (program_id_ != 0) {
        glDeleteProgram(program_id_);
    }
}

void ShaderProgram::use() const
{
    glUseProgram(program_id_);
}

void ShaderProgram::set_integer(const std::string_view name, const int value) const
{
    glUniform1i(uniform_location(name), value);
}

void ShaderProgram::set_float(const std::string_view name, const float value) const
{
    glUniform1f(uniform_location(name), value);
}

void ShaderProgram::set_vector(const std::string_view name, const glm::vec3& value) const
{
    glUniform3fv(uniform_location(name), 1, glm::value_ptr(value));
}

void ShaderProgram::set_matrix(const std::string_view name, const glm::mat3& value) const
{
    glUniformMatrix3fv(uniform_location(name), 1, GL_FALSE, glm::value_ptr(value));
}

void ShaderProgram::set_matrix(const std::string_view name, const glm::mat4& value) const
{
    glUniformMatrix4fv(uniform_location(name), 1, GL_FALSE, glm::value_ptr(value));
}

int ShaderProgram::uniform_location(const std::string_view name) const
{
    const auto cached{uniform_locations_.find(name)};
    if (cached != uniform_locations_.end()) {
        return cached->second;
    }
    const std::string null_terminated_name{name};
    const int location = glGetUniformLocation(program_id_, null_terminated_name.c_str());
    if (location < 0) {
        throw std::runtime_error("Shader uniform is missing or inactive: " + null_terminated_name);
    }
    uniform_locations_.emplace(null_terminated_name, location);
    return location;
}

}  // namespace crystalbound
