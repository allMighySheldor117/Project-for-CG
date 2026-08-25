#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

namespace crystalbound {

struct CameraMovementInput {
    float forward{};
    float right{};
    float vertical{};
    bool boosted{};
};

class Camera {
public:
    Camera();

    void move(const CameraMovementInput& input, float delta_seconds);
    void rotate(float horizontal_delta, float vertical_delta);

    [[nodiscard]] glm::mat4 view_matrix() const;
    [[nodiscard]] glm::mat4 projection_matrix(float aspect_ratio) const;

private:
    void update_basis();

    glm::vec3 position_{0.0F, 0.0F, 3.0F};
    glm::vec3 front_{0.0F, 0.0F, -1.0F};
    glm::vec3 right_{1.0F, 0.0F, 0.0F};
    glm::vec3 up_{0.0F, 1.0F, 0.0F};
    glm::vec3 world_up_{0.0F, 1.0F, 0.0F};
    float yaw_degrees_{-90.0F};
    float pitch_degrees_{};
};

}  // namespace crystalbound
