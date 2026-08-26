#pragma once

#include <memory>
#include <optional>
#include <vector>

#include "crystalbound/Camera.hpp"
#include "crystalbound/CaveScene.hpp"
#include "crystalbound/CrystalCollection.hpp"
#include "crystalbound/PlayerController.hpp"

struct GLFWwindow;

namespace crystalbound {

class Application {
public:
    explicit Application(CaveGenerationResult generation);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    int run();

private:
    class RenderResources;

    void initialize();
    void initialize_window();
    void initialize_opengl();
    void run_frame_loop();
    void process_movement(float delta_seconds);
    void process_crystal_interaction();
    void render_crystal_prompt();
    void set_mouse_captured(bool captured);
    void shutdown() noexcept;

    static void glfw_error_callback(int error_code, const char* description);
    static void framebuffer_size_callback(GLFWwindow* window, int width, int height);
    static void cursor_position_callback(GLFWwindow* window, double x_position, double y_position);
    static void key_callback(
        GLFWwindow* window,
        int key,
        int scan_code,
        int action,
        int modifiers);

    GLFWwindow* window_{};
    bool glfw_initialized_{};
    bool mouse_captured_{true};
    bool mouse_sample_pending_{true};
    double last_mouse_x_{};
    double last_mouse_y_{};
    int framebuffer_width_{};
    int framebuffer_height_{};
    Camera camera_{};
    CaveGenerationResult generation_{};
    std::unique_ptr<GroundedController> controller_{};
    bool backlog_warning_emitted_{};
    bool imgui_initialized_{};
    CrystalCollectionState crystal_collection_{};
    RisingEdgeButton interaction_button_{};
    std::vector<CrystalInteractionTarget> interaction_targets_{};
    VisibilityWorld visibility_world_{};
    std::optional<FocusedCrystal> focused_crystal_{};
    std::unique_ptr<RenderResources> render_resources_{};
};

}  // namespace crystalbound
