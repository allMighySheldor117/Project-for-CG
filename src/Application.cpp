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
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/mat3x3.hpp>
#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include "crystalbound/GpuMesh.hpp"
#include "crystalbound/GpuTexture.hpp"
#include "crystalbound/Rendering.hpp"
#include "crystalbound/ResourcePaths.hpp"
#include "crystalbound/ShaderProgram.hpp"

namespace crystalbound {
namespace {

constexpr int initial_window_width{1280};
constexpr int initial_window_height{720};
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

void apply_render_pass_state(const RenderPassState& state)
{
    validate_render_pass_state(state);
    (state.framebuffer_srgb ? glEnable : glDisable)(GL_FRAMEBUFFER_SRGB);
    (state.depth_test ? glEnable : glDisable)(GL_DEPTH_TEST);
    glDepthMask(state.depth_write ? GL_TRUE : GL_FALSE);
    if (state.cull_mode == CullMode::back) {
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glFrontFace(GL_CCW);
    } else {
        glDisable(GL_CULL_FACE);
    }
    if (state.blend_mode == BlendMode::straight_alpha) {
        glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
        glDisable(GL_BLEND);
    }
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
        const CaveSceneData& scene,
        const Seed effective_seed)
        : shader_program_(
              shader_directory / "cave_phong.vert",
              shader_directory / "cave_phong.frag"),
          rock_texture_(generate_rock_texture(effective_seed, rock_texture_stable_id)),
          wood_texture_(generate_wood_texture(effective_seed, wood_texture_stable_id))
    {
        pieces_.reserve(scene.mesh_pieces.size());
        for (const SceneMeshPiece& piece : scene.mesh_pieces) {
            pieces_.push_back({piece.material, piece.albedo, std::make_unique<GpuMesh>(piece.mesh)});
        }
        shader_program_.use();
        shader_program_.set_integer("u_rock_texture", 0);
        shader_program_.set_integer("u_wood_texture", 1);
        if (effective_seed.value == 42U
            && (rock_texture_.fingerprint() != rock_texture_golden_fingerprint_seed_42
                || wood_texture_.fingerprint() != wood_texture_golden_fingerprint_seed_42)) {
            throw std::runtime_error("Seed 42 procedural texture fingerprints do not match the locked contract.");
        }
        std::cout << "Procedural materials uploaded once\n"
                  << "  Rock texture: " << format_fingerprint(rock_texture_.fingerprint()) << '\n'
                  << "  Wood texture: " << format_fingerprint(wood_texture_.fingerprint()) << '\n';
    }

    void render(
        const glm::mat4& view,
        const glm::mat4& projection,
        const glm::vec3& camera_position,
        const std::uint64_t stable_zone_id)
    {
        const glm::mat4 model{1.0F};
        const glm::mat3 normal_matrix{glm::inverseTranspose(glm::mat3{model})};
        const PointLight lantern{camera_lantern(
            {camera_position.x, camera_position.y, camera_position.z})};
        const FogParameters fog{fog_parameters_for_zone(stable_zone_id)};

        shader_program_.use();
        rock_texture_.bind(0U);
        wood_texture_.bind(1U);
        shader_program_.set_matrix("u_model", model);
        shader_program_.set_matrix("u_view", view);
        shader_program_.set_matrix("u_projection", projection);
        shader_program_.set_matrix("u_normal_matrix", normal_matrix);
        shader_program_.set_vector("u_camera_position", camera_position);
        shader_program_.set_integer("u_point_light_count", 1);
        shader_program_.set_vector("u_point_lights[0].position", glm::vec3{
            lantern.position_metres[0], lantern.position_metres[1], lantern.position_metres[2]});
        shader_program_.set_vector("u_point_lights[0].color", glm::vec3{
            lantern.color_linear[0], lantern.color_linear[1], lantern.color_linear[2]});
        shader_program_.set_float("u_point_lights[0].intensity", lantern.intensity);
        shader_program_.set_float(
            "u_point_lights[0].attenuation_constant", lantern.attenuation_constant);
        shader_program_.set_float(
            "u_point_lights[0].attenuation_linear", lantern.attenuation_linear);
        shader_program_.set_float(
            "u_point_lights[0].attenuation_quadratic", lantern.attenuation_quadratic);
        shader_program_.set_float("u_point_lights[0].range_metres", lantern.range_metres);
        shader_program_.set_vector("u_fog_color", glm::vec3{
            fog.color_linear[0], fog.color_linear[1], fog.color_linear[2]});
        shader_program_.set_float("u_fog_start", fog.start_distance_metres);
        shader_program_.set_float("u_fog_end", fog.end_distance_metres);

        for (const RenderPiece& piece : pieces_) {
            const MaterialParameters material{material_parameters(piece.material)};
            validate_material_parameters(material);
            shader_program_.set_integer("u_material_kind", static_cast<int>(piece.material));
            shader_program_.set_vector(
                "u_albedo",
                {piece.albedo[0], piece.albedo[1], piece.albedo[2]});
            shader_program_.set_vector("u_material_ambient", glm::vec3{
                material.ambient[0], material.ambient[1], material.ambient[2]});
            shader_program_.set_vector("u_material_diffuse", glm::vec3{
                material.diffuse[0], material.diffuse[1], material.diffuse[2]});
            shader_program_.set_vector("u_material_specular", glm::vec3{
                material.specular[0], material.specular[1], material.specular[2]});
            shader_program_.set_vector("u_material_emission", glm::vec3{
                material.emission[0], material.emission[1], material.emission[2]});
            shader_program_.set_float("u_material_shininess", material.shininess);
            shader_program_.set_float("u_texture_scale", material.texture_scale);
            shader_program_.set_float("u_triplanar_sharpness", material.triplanar_sharpness);
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
        MaterialKind material{MaterialKind::rock};
        std::array<float, 3> albedo{};
        std::unique_ptr<GpuMesh> mesh{};
    };

    ShaderProgram shader_program_;
    GpuTexture rock_texture_;
    GpuTexture wood_texture_;
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
    controller_ = std::make_unique<GroundedController>(
        build_collision_world(generation_.scene), find_start_spawn(generation_));
    const GeometryVector3 camera_position{controller_->camera_position_metres()};
    camera_.set_position({
        static_cast<float>(camera_position.x),
        static_cast<float>(camera_position.y),
        static_cast<float>(camera_position.z),
    });

    initialize_window();
    initialize_opengl();

    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebuffer_size_callback);
    glfwSetCursorPosCallback(window_, cursor_position_callback);
    glfwSetKeyCallback(window_, key_callback);
    set_mouse_captured(true);

    render_resources_ = std::make_unique<RenderResources>(
        resources / "shaders", generation_.scene, generation_.generation.effective_seed);
    std::cout << "Generated cave scene\n"
              << "  Mesh pieces: " << generation_.scene.mesh_pieces.size() << '\n'
              << "  Static vertices: " << generation_.scene.static_vertex_count << '\n'
              << "  Colliders: " << generation_.scene.colliders.size() << '\n'
              << "  Grounded start chamber: "
              << controller_->state().safe_chamber_id.value << '\n'
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
    glfwWindowHint(GLFW_SRGB_CAPABLE, GLFW_TRUE);
#if defined(__APPLE__)
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
#endif

    window_ = glfwCreateWindow(
        initial_window_width,
        initial_window_height,
        "Crystalbound - Phong Cave Exploration",
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
    int framebuffer_color_encoding{};
    glGetFramebufferAttachmentParameteriv(
        GL_FRAMEBUFFER,
        GL_BACK_LEFT,
        GL_FRAMEBUFFER_ATTACHMENT_COLOR_ENCODING,
        &framebuffer_color_encoding);
    if (glGetError() != GL_NO_ERROR || framebuffer_color_encoding != GL_SRGB) {
        throw std::runtime_error(
            "The default framebuffer is not sRGB-capable; Crystalbound requires linear lighting output.");
    }

    std::cout << "OpenGL runtime initialized\n"
              << "  Vendor: " << opengl_string(GL_VENDOR) << '\n'
              << "  Renderer: " << opengl_string(GL_RENDERER) << '\n'
              << "  OpenGL: " << opengl_string(GL_VERSION) << '\n'
              << "  GLSL: " << opengl_string(GL_SHADING_LANGUAGE_VERSION) << '\n';

    glDepthFunc(GL_LESS);
    apply_render_pass_state(opaque_render_pass);
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
        const double elapsed = std::max(0.0, current_time - previous_time);
        previous_time = current_time;

        process_movement(static_cast<float>(elapsed));

        if (framebuffer_width_ <= 0 || framebuffer_height_ <= 0) {
            glfwWaitEventsTimeout(0.05);
            previous_time = glfwGetTime();
            continue;
        }

        apply_render_pass_state(opaque_render_pass);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        const float aspect_ratio = static_cast<float>(framebuffer_width_)
            / static_cast<float>(framebuffer_height_);
        render_resources_->render(
            camera_.view_matrix(), camera_.projection_matrix(aspect_ratio), camera_.position(),
            controller_->state().safe_chamber_id.value);
        apply_render_pass_state(ui_render_pass);

        glfwSwapBuffers(window_);
        glfwPollEvents();
    }
}

void Application::process_movement(const float delta_seconds)
{
    GroundedMovementInput input{};
    input.forward = (key_is_down(window_, GLFW_KEY_W) ? 1.0F : 0.0F)
        - (key_is_down(window_, GLFW_KEY_S) ? 1.0F : 0.0F);
    input.right = (key_is_down(window_, GLFW_KEY_D) ? 1.0F : 0.0F)
        - (key_is_down(window_, GLFW_KEY_A) ? 1.0F : 0.0F);
    input.view_yaw_degrees = camera_.yaw_degrees();
    input.jump = key_is_down(window_, GLFW_KEY_SPACE);
    input.sprint = key_is_down(window_, GLFW_KEY_LEFT_SHIFT)
        || key_is_down(window_, GLFW_KEY_RIGHT_SHIFT);
    const ControllerAdvanceResult result{controller_->advance(input, delta_seconds)};
    if (result.backlog_discarded && !backlog_warning_emitted_) {
        std::cerr << "Controller warning: discarded excess fixed-step backlog.\n";
        backlog_warning_emitted_ = true;
    }
    const GeometryVector3 camera_position{controller_->camera_position_metres()};
    camera_.set_position({
        static_cast<float>(camera_position.x),
        static_cast<float>(camera_position.y),
        static_cast<float>(camera_position.z),
    });
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
