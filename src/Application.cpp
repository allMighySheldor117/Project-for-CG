#include "npr/Application.hpp"

#include <algorithm>
#include <array>
#include <iostream>
#include <stdexcept>
#include <string>

#include <glad/gl.h>

#include <GLFW/glfw3.h>
#include <glm/mat4x4.hpp>

#include "npr/ResourcePaths.hpp"
#include "npr/ShaderProgram.hpp"

namespace npr {
namespace {

constexpr int initial_window_width{1280};
constexpr int initial_window_height{720};
constexpr double maximum_frame_delta{0.1};
constexpr float background_red{0.08F};
constexpr float background_green{0.09F};
constexpr float background_blue{0.12F};

class SmokeGeometry {
public:
    SmokeGeometry()
    {
        constexpr std::array<float, 18> vertices{
            -0.8F, -0.6F, 0.0F, 0.95F, 0.30F, 0.25F,
             0.8F, -0.6F, 0.0F, 0.25F, 0.75F, 0.95F,
             0.0F,  0.8F, 0.0F, 0.95F, 0.80F, 0.25F,
        };

        glGenVertexArrays(1, &vertex_array_);
        glGenBuffers(1, &vertex_buffer_);
        if (vertex_array_ == 0 || vertex_buffer_ == 0) {
            release();
            throw std::runtime_error("OpenGL could not allocate smoke-test geometry.");
        }

        glBindVertexArray(vertex_array_);
        glBindBuffer(GL_ARRAY_BUFFER, vertex_buffer_);
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(vertices.size() * sizeof(float)),
            vertices.data(),
            GL_STATIC_DRAW);

        constexpr int values_per_vertex{6};
        constexpr int color_offset{3};
        glVertexAttribPointer(
            0,
            3,
            GL_FLOAT,
            GL_FALSE,
            values_per_vertex * static_cast<int>(sizeof(float)),
            nullptr);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            1,
            3,
            GL_FLOAT,
            GL_FALSE,
            values_per_vertex * static_cast<int>(sizeof(float)),
            reinterpret_cast<const void*>(color_offset * sizeof(float)));
        glEnableVertexAttribArray(1);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    ~SmokeGeometry()
    {
        release();
    }

    SmokeGeometry(const SmokeGeometry&) = delete;
    SmokeGeometry& operator=(const SmokeGeometry&) = delete;
    SmokeGeometry(SmokeGeometry&&) = delete;
    SmokeGeometry& operator=(SmokeGeometry&&) = delete;

    void draw() const
    {
        glBindVertexArray(vertex_array_);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        glBindVertexArray(0);
    }

private:
    void release() noexcept
    {
        if (vertex_buffer_ != 0) {
            glDeleteBuffers(1, &vertex_buffer_);
            vertex_buffer_ = 0;
        }
        if (vertex_array_ != 0) {
            glDeleteVertexArrays(1, &vertex_array_);
            vertex_array_ = 0;
        }
    }

    unsigned int vertex_array_{};
    unsigned int vertex_buffer_{};
};

[[nodiscard]] const char* opengl_string(const unsigned int name)
{
    const auto* value = glGetString(name);
    return value == nullptr ? "<unavailable>" : reinterpret_cast<const char*>(value);
}

[[nodiscard]] Application* application_for(GLFWwindow* window)
{
    return static_cast<Application*>(glfwGetWindowUserPointer(window));
}

[[nodiscard]] bool key_is_down(GLFWwindow* window, const int key)
{
    const int state = glfwGetKey(window, key);
    return state == GLFW_PRESS || state == GLFW_REPEAT;
}

void require_no_opengl_error(const char* operation)
{
    const unsigned int error = glGetError();
    if (error != GL_NO_ERROR) {
        throw std::runtime_error(
            std::string{"OpenGL error after "} + operation + ": " + std::to_string(error));
    }
}

}  // namespace

class Application::RenderResources {
public:
    explicit RenderResources(const std::filesystem::path& shader_directory)
        : shader_program_(shader_directory / "smoke.vert", shader_directory / "smoke.frag")
    {
    }

    void render(const glm::mat4& view, const glm::mat4& projection)
    {
        const glm::mat4 model{1.0F};
        shader_program_.use();
        shader_program_.set_matrix("u_mvp", projection * view * model);
        geometry_.draw();
        if (!first_frame_validated_) {
            require_no_opengl_error("the first smoke-test draw");
            first_frame_validated_ = true;
        }
    }

private:
    ShaderProgram shader_program_;
    SmokeGeometry geometry_;
    bool first_frame_validated_{};
};

Application::Application() = default;

Application::~Application()
{
    shutdown();
}

int Application::run()
{
    initialize();
    run_frame_loop();
    shutdown();
    return 0;
}

void Application::initialize()
{
    initialize_window();
    initialize_opengl();

    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebuffer_size_callback);
    glfwSetCursorPosCallback(window_, cursor_position_callback);
    glfwSetKeyCallback(window_, key_callback);
    set_mouse_captured(true);

    const std::filesystem::path shaders = resource_root() / "shaders";
    render_resources_ = std::make_unique<RenderResources>(shaders);
}

void Application::initialize_window()
{
    glfwSetErrorCallback(glfw_error_callback);
    if (glfwInit() != GLFW_TRUE) {
        throw std::runtime_error("GLFW initialization failed.");
    }
    glfw_initialized_ = true;

    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    window_ = glfwCreateWindow(
        initial_window_width,
        initial_window_height,
        "Real-Time NPR Renderer - Runtime Smoke Test",
        nullptr,
        nullptr);
    if (window_ == nullptr) {
        throw std::runtime_error("GLFW could not create an OpenGL 3.3 Core window.");
    }

    glfwMakeContextCurrent(window_);
    glfwSwapInterval(1);
}

void Application::initialize_opengl()
{
    const int loaded_version = gladLoadGL(glfwGetProcAddress);
    if (loaded_version == 0) {
        throw std::runtime_error("GLAD could not load OpenGL through GLFW.");
    }

    const int loaded_major = GLAD_VERSION_MAJOR(loaded_version);
    const int loaded_minor = GLAD_VERSION_MINOR(loaded_version);
    if (loaded_major < 3 || (loaded_major == 3 && loaded_minor < 3)) {
        throw std::runtime_error(
            "GLAD loaded OpenGL " + std::to_string(loaded_major) + '.'
            + std::to_string(loaded_minor) + ", but OpenGL 3.3 Core is required.");
    }

    int context_major{};
    int context_minor{};
    int profile_mask{};
    glGetIntegerv(GL_MAJOR_VERSION, &context_major);
    glGetIntegerv(GL_MINOR_VERSION, &context_minor);
    glGetIntegerv(GL_CONTEXT_PROFILE_MASK, &profile_mask);
    if (context_major < 3 || (context_major == 3 && context_minor < 3)
        || (profile_mask & GL_CONTEXT_CORE_PROFILE_BIT) == 0) {
        throw std::runtime_error(
            "The created context does not satisfy the OpenGL 3.3 Core requirement.");
    }

    std::cout << "OpenGL runtime initialized\n"
              << "  Vendor: " << opengl_string(GL_VENDOR) << '\n'
              << "  Renderer: " << opengl_string(GL_RENDERER) << '\n'
              << "  OpenGL: " << opengl_string(GL_VERSION) << '\n'
              << "  GLSL: " << opengl_string(GL_SHADING_LANGUAGE_VERSION) << '\n';

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glFrontFace(GL_CCW);
    glClearColor(background_red, background_green, background_blue, 1.0F);

    glfwGetFramebufferSize(window_, &framebuffer_width_, &framebuffer_height_);
    if (framebuffer_width_ > 0 && framebuffer_height_ > 0) {
        glViewport(0, 0, framebuffer_width_, framebuffer_height_);
    }
}

void Application::run_frame_loop()
{
    double previous_time = glfwGetTime();
    while (glfwWindowShouldClose(window_) == GLFW_FALSE) {
        const double current_time = glfwGetTime();
        const double elapsed = std::clamp(current_time - previous_time, 0.0, maximum_frame_delta);
        previous_time = current_time;

        process_movement(static_cast<float>(elapsed));

        if (framebuffer_width_ <= 0 || framebuffer_height_ <= 0) {
            glfwWaitEventsTimeout(0.05);
            previous_time = glfwGetTime();
            continue;
        }

        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        const float aspect_ratio = static_cast<float>(framebuffer_width_)
            / static_cast<float>(framebuffer_height_);
        render_resources_->render(
            camera_.view_matrix(), camera_.projection_matrix(aspect_ratio));

        glfwSwapBuffers(window_);
        glfwPollEvents();
    }
}

void Application::process_movement(const float delta_seconds)
{
    CameraMovementInput input{};
    input.forward = (key_is_down(window_, GLFW_KEY_W) ? 1.0F : 0.0F)
        - (key_is_down(window_, GLFW_KEY_S) ? 1.0F : 0.0F);
    input.right = (key_is_down(window_, GLFW_KEY_D) ? 1.0F : 0.0F)
        - (key_is_down(window_, GLFW_KEY_A) ? 1.0F : 0.0F);
    input.vertical = (key_is_down(window_, GLFW_KEY_SPACE) ? 1.0F : 0.0F)
        - ((key_is_down(window_, GLFW_KEY_LEFT_CONTROL)
               || key_is_down(window_, GLFW_KEY_RIGHT_CONTROL))
                ? 1.0F
                : 0.0F);
    input.boosted = key_is_down(window_, GLFW_KEY_LEFT_SHIFT)
        || key_is_down(window_, GLFW_KEY_RIGHT_SHIFT);
    camera_.move(input, delta_seconds);
}

void Application::set_mouse_captured(const bool captured)
{
    mouse_captured_ = captured;
    mouse_sample_pending_ = true;
    glfwSetInputMode(
        window_, GLFW_CURSOR, mouse_captured_ ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
}

void Application::shutdown() noexcept
{
    if (window_ != nullptr) {
        if (glfwGetCurrentContext() != window_) {
            glfwMakeContextCurrent(window_);
        }
        render_resources_.reset();
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    if (glfw_initialized_) {
        glfwTerminate();
        glfw_initialized_ = false;
    }
}

void Application::glfw_error_callback(const int error_code, const char* description)
{
    std::cerr << "GLFW error " << error_code << ": "
              << (description == nullptr ? "<no description>" : description) << '\n';
}

void Application::framebuffer_size_callback(GLFWwindow* window, const int width, const int height)
{
    Application* application = application_for(window);
    if (application == nullptr) {
        return;
    }
    application->framebuffer_width_ = width;
    application->framebuffer_height_ = height;
    if (width > 0 && height > 0) {
        glViewport(0, 0, width, height);
    }
}

void Application::cursor_position_callback(
    GLFWwindow* window,
    const double x_position,
    const double y_position)
{
    Application* application = application_for(window);
    if (application == nullptr || !application->mouse_captured_) {
        return;
    }

    if (application->mouse_sample_pending_) {
        application->last_mouse_x_ = x_position;
        application->last_mouse_y_ = y_position;
        application->mouse_sample_pending_ = false;
        return;
    }

    const float horizontal_delta = static_cast<float>(x_position - application->last_mouse_x_);
    const float vertical_delta = static_cast<float>(application->last_mouse_y_ - y_position);
    application->last_mouse_x_ = x_position;
    application->last_mouse_y_ = y_position;
    application->camera_.rotate(horizontal_delta, vertical_delta);
}

void Application::key_callback(
    GLFWwindow* window,
    const int key,
    const int scan_code,
    const int action,
    const int modifiers)
{
    static_cast<void>(scan_code);
    static_cast<void>(modifiers);

    Application* application = application_for(window);
    if (application != nullptr && key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
        application->set_mouse_captured(!application->mouse_captured_);
    }
}

}  // namespace npr
