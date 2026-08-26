#include "crystalbound/Camera.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include <glm/geometric.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/trigonometric.hpp>

namespace crystalbound {
namespace {

constexpr float movement_speed{2.5F};
constexpr float boost_multiplier{3.0F};
constexpr float mouse_sensitivity{0.1F};
constexpr float maximum_pitch{89.0F};
constexpr float vertical_field_of_view{60.0F};
constexpr float near_plane{0.1F};
constexpr float far_plane{100.0F};

}  // namespace

Camera::Camera()
{
    update_basis();
}

void Camera::move(const CameraMovementInput& input, const float delta_seconds)
{
    if (!std::isfinite(delta_seconds) || delta_seconds <= 0.0F) {
        return;
    }

    glm::vec3 direction = front_ * input.forward + right_ * input.right
        + world_up_ * input.vertical;
    const float direction_length = glm::length(direction);
    if (!std::isfinite(direction_length) || direction_length <= 0.0F) {
        return;
    }

    if (direction_length > 1.0F) {
        direction /= direction_length;
    }

    const float speed = movement_speed * (input.boosted ? boost_multiplier : 1.0F);
    position_ += direction * speed * delta_seconds;
}

void Camera::rotate(const float horizontal_delta, const float vertical_delta)
{
    if (!std::isfinite(horizontal_delta) || !std::isfinite(vertical_delta)) {
        return;
    }

    yaw_degrees_ += horizontal_delta * mouse_sensitivity;
    pitch_degrees_ = std::clamp(
        pitch_degrees_ + vertical_delta * mouse_sensitivity,
        -maximum_pitch,
        maximum_pitch);
    update_basis();
}

void Camera::set_pose(const glm::vec3& position, const glm::vec3& forward)
{
    if (!std::isfinite(position.x) || !std::isfinite(position.y)
        || !std::isfinite(position.z) || !std::isfinite(forward.x)
        || !std::isfinite(forward.y) || !std::isfinite(forward.z)) {
        throw std::invalid_argument("Camera pose must be finite.");
    }
    const float forward_length{glm::length(forward)};
    if (!std::isfinite(forward_length) || forward_length <= 0.0F) {
        throw std::invalid_argument("Camera forward direction must be nonzero.");
    }
    position_ = position;
    const glm::vec3 direction{forward / forward_length};
    yaw_degrees_ = glm::degrees(std::atan2(direction.z, direction.x));
    pitch_degrees_ = glm::degrees(std::asin(std::clamp(direction.y, -1.0F, 1.0F)));
    update_basis();
}

void Camera::set_position(const glm::vec3& position)
{
    if (!std::isfinite(position.x) || !std::isfinite(position.y)
        || !std::isfinite(position.z)) {
        throw std::invalid_argument("Camera position must be finite.");
    }
    position_ = position;
}

glm::mat4 Camera::view_matrix() const
{
    return glm::lookAt(position_, position_ + front_, up_);
}

glm::mat4 Camera::projection_matrix(const float aspect_ratio) const
{
    if (!std::isfinite(aspect_ratio) || aspect_ratio <= 0.0F) {
        throw std::invalid_argument("Camera aspect ratio must be finite and positive.");
    }
    return glm::perspective(
        glm::radians(vertical_field_of_view), aspect_ratio, near_plane, far_plane);
}

float Camera::yaw_degrees() const noexcept
{
    return yaw_degrees_;
}

const glm::vec3& Camera::position() const noexcept
{
    return position_;
}

void Camera::update_basis()
{
    const float yaw = glm::radians(yaw_degrees_);
    const float pitch = glm::radians(pitch_degrees_);
    const glm::vec3 direction{
        std::cos(yaw) * std::cos(pitch),
        std::sin(pitch),
        std::sin(yaw) * std::cos(pitch),
    };

    front_ = glm::normalize(direction);
    right_ = glm::normalize(glm::cross(front_, world_up_));
    up_ = glm::normalize(glm::cross(right_, front_));
}

}  // namespace crystalbound
