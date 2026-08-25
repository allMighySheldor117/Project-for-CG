#include "crystalbound/Application.hpp"

#include <algorithm>
#include <array>
#include <filesystem>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glad/gl.h>

#include <GLFW/glfw3.h>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "crystalbound/GpuMesh.hpp"
#include "crystalbound/ResourcePaths.hpp"
#include "crystalbound/ShaderProgram.hpp"

namespace crystalbound {
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

}  // namespace

class Application::RenderResources {
public:
    RenderResources(
        const std::filesystem::path& shader_directory,
        const CaveSceneData& scene)
        : shader_program_(
              shader_directory / "normal_debug.vert",
              shader_directory / "normal_debug.frag")
    {
        pieces_.reserve(scene.mesh_pieces.size());
        for (const SceneMeshPiece& piece : scene.mesh_pieces) {
            pieces_.push_back({piece.albedo, std::make_unique<GpuMesh>(piece.mesh)});
        }
    }

    void render(const glm::mat4& view, const glm::mat4& projection)
    {
        const glm::mat4 model{1.0F};
        shader_program_.use();
        shader_program_.set_matrix("u_mvp", projection * view * model);
        for (const RenderPiece& piece : pieces_) {
            shader_program_.set_vector(
                "u_albedo",
                {piece.albedo[0], piece.albedo[1], piece.albedo[2]});
            piece.mesh->draw();
        }
#if !defined(NDEBUG)
        if (!first_frame_validated_) {
            require_no_opengl_error("the first generated cave draw");
            first_frame_validated_ = true;
        }
#endif
    }

private:
    struct RenderPiece {
        std::array<float, 3> albedo{};
        std::unique_ptr<GpuMesh> mesh{};
    };

    ShaderProgram shader_program_;
    std::vector<RenderPiece> pieces_{};
#if !defined(NDEBUG)
    bool first_frame_validated_{};
#endif
};

Application::Application(CaveGenerationResult generation)
    : generation_(std::move(generation))
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
    const GeometryVector3& position{generation_.scene.start_camera_position_metres};
    const GeometryVector3& forward{generation_.scene.start_camera_forward};
    camera_.set_pose(
        {static_cast<float>(position.x), static_cast<float>(position.y),
         static_cast<float>(position.z)},
        {static_cast<float>(forward.x), static_cast<float>(forward.y),
         static_cast<float>(forward.z)});

    initialize_window();
    initialize_opengl();

    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebuffer_size_callback);
    glfwSetCursorPosCallback(window_, cursor_position_callback);
    glfwSetKeyCallback(window_, key_callback);
    set_mouse_captured(true);

    render_resources_ = std::make_unique<RenderResources>(
        resources / "shaders", generation_.scene);
    std::cout << "Generated cave scene\n"
              << "  Mesh pieces: " << generation_.scene.mesh_pieces.size() << '\n'
              << "  Static vertices: " << generation_.scene.static_vertex_count << '\n'
              << "  Colliders: " << generation_.scene.colliders.size() << '\n'
              << "  Scene fingerprint: "
              << format_fingerprint(generation_.scene.fingerprint) << '\n';
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
        "Crystalbound - Generated Cave Scene",
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

}  // namespace crystalbound
