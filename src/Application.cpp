#include "npr/Application.hpp"

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glad/gl.h>

#include <GLFW/glfw3.h>
#include <glm/mat4x4.hpp>

#include "npr/GpuMesh.hpp"
#include "npr/ObjLoader.hpp"
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

#if !defined(NDEBUG)
void require_no_opengl_error(const char* operation)
{
    const unsigned int error = glGetError();
    if (error != GL_NO_ERROR) {
        throw std::runtime_error(
            std::string{"OpenGL error after "} + operation + ": " + std::to_string(error));
    }
}
#endif

[[nodiscard]] std::filesystem::path absolute_for_diagnostics(
    const std::filesystem::path& path)
{
    std::error_code error;
    const std::filesystem::path absolute = std::filesystem::absolute(path, error);
    return error ? path : absolute;
}

void print_model_warnings(
    const std::filesystem::path& path,
    const std::vector<std::string>& warnings)
{
    for (const std::string& warning : warnings) {
        std::cerr << "Model warning [" << path.u8string() << "]: " << warning << '\n';
    }
}

[[nodiscard]] MeshData load_model(
    const std::optional<std::filesystem::path>& requested_path,
    const std::filesystem::path& bundled_path)
{
    if (requested_path.has_value()) {
        const std::filesystem::path custom_path = absolute_for_diagnostics(*requested_path);
        try {
            ModelLoadResult result = load_obj(custom_path);
            print_model_warnings(custom_path, result.warnings);
            std::cout << "Loaded custom model: " << custom_path.u8string() << '\n';
            return std::move(result.mesh);
        } catch (const ModelLoadError& error) {
            std::cerr << "Custom model failed; using bundled Suzanne.\n"
                      << "  Path: " << custom_path.u8string() << '\n'
                      << "  Reason: " << error.what() << '\n';
        }
    }

    ModelLoadResult result;
    try {
        result = load_obj(bundled_path);
    } catch (const ModelLoadError& error) {
        throw ModelLoadError(
            "Bundled model failed [" + bundled_path.u8string() + "]: " + error.what());
    }
    print_model_warnings(bundled_path, result.warnings);
    std::cout << "Loaded bundled model: " << bundled_path.u8string() << '\n';
    return std::move(result.mesh);
}

}  // namespace

class Application::RenderResources {
public:
    RenderResources(const std::filesystem::path& shader_directory, const MeshData& mesh)
        : shader_program_(
              shader_directory / "normal_debug.vert",
              shader_directory / "normal_debug.frag"),
          mesh_(mesh)
    {
    }

    void render(const glm::mat4& view, const glm::mat4& projection)
    {
        const glm::mat4 model{1.0F};
        shader_program_.use();
        shader_program_.set_matrix("u_mvp", projection * view * model);
        mesh_.draw();
#if !defined(NDEBUG)
        if (!first_frame_validated_) {
            require_no_opengl_error("the first indexed model draw");
            first_frame_validated_ = true;
        }
#endif
    }

private:
    ShaderProgram shader_program_;
    GpuMesh mesh_;
#if !defined(NDEBUG)
    bool first_frame_validated_{};
#endif
};

Application::Application(std::optional<std::filesystem::path> model_path)
    : model_path_(std::move(model_path))
{
}

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
    const std::filesystem::path resources = resource_root();
    const MeshData model = load_model(model_path_, resources / "assets/models/suzanne.obj");

    initialize_window();
    initialize_opengl();

    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebuffer_size_callback);
    glfwSetCursorPosCallback(window_, cursor_position_callback);
    glfwSetKeyCallback(window_, key_callback);
    set_mouse_captured(true);

    render_resources_ = std::make_unique<RenderResources>(resources / "shaders", model);
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
        "Real-Time NPR Renderer - Model Pipeline",
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
