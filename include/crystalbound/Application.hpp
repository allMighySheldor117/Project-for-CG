#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "crystalbound/Camera.hpp"
#include "crystalbound/CaveScene.hpp"
#include "crystalbound/CommandLine.hpp"
#include "crystalbound/CrystalCollection.hpp"
#include "crystalbound/ExitArch.hpp"
#include "crystalbound/GameLoop.hpp"
#include "crystalbound/PlayerController.hpp"

struct GLFWwindow;

namespace crystalbound {

class Application {
public:
    explicit Application(
        CaveGenerationResult generation,
        SeedSource seed_source = os_entropy_seed);
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
    void process_interaction(GameTimePoint now);
    void render_game_ui(GameTimePoint now);
    void render_start_ui(GameTimePoint now);
    void render_playing_ui(GameTimePoint now);
    void render_pause_ui(GameTimePoint now);
    void render_completed_ui(GameTimePoint now);
    void render_interaction_prompt();
    void render_crystal_progress();
    void apply_transition(const GameTransition& transition);
    void pause_game(GameTimePoint now);
    void rebuild_run(Seed requested_seed);
    void restart_seed();
    void new_cave();
    void play_again();
    void clear_runtime_input();
    [[nodiscard]] bool gameplay_keys_released() const;
    [[nodiscard]] SessionBestKey current_best_key() const noexcept;
    void set_mouse_captured(bool captured);
    void shutdown() noexcept;

    static void glfw_error_callback(int error_code, const char* description);
    static void framebuffer_size_callback(GLFWwindow* window, int width, int height);
    static void cursor_position_callback(GLFWwindow* window, double x_position, double y_position);
    static void window_focus_callback(GLFWwindow* window, int focused);
    static void window_iconify_callback(GLFWwindow* window, int iconified);
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
    SeedSource seed_source_{};
    std::unique_ptr<GroundedController> controller_{};
    bool backlog_warning_emitted_{};
    bool imgui_initialized_{};
    CrystalCollectionState crystal_collection_{};
    GameSession game_session_{};
    RisingEdgeButton interaction_button_{};
    std::vector<CrystalInteractionTarget> interaction_targets_{};
    VisibilityWorld visibility_world_{};
    ExitArchData exit_arch_{};
    bool focused_exit_arch_{};
    std::optional<FocusedCrystal> focused_crystal_{};
    bool movement_input_blocked_{true};
    std::string ui_error_message_{};
    std::unique_ptr<RenderResources> render_resources_{};
};

}  // namespace crystalbound
