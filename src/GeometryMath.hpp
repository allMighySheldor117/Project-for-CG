#pragma once

#include <cmath>
#include <string>
#include <string_view>

#include "crystalbound/Geometry.hpp"

namespace crystalbound::geometry_detail {

inline constexpr double vector_epsilon_squared{1.0e-20};

[[nodiscard]] inline GeometryVector3 add(
    const GeometryVector3& left,
    const GeometryVector3& right) noexcept
{
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] inline GeometryVector3 subtract(
    const GeometryVector3& left,
    const GeometryVector3& right) noexcept
{
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] inline GeometryVector3 multiply(
    const GeometryVector3& value,
    const double scale) noexcept
{
    return {value.x * scale, value.y * scale, value.z * scale};
}

[[nodiscard]] inline GeometryVector3 divide(
    const GeometryVector3& value,
    const double divisor) noexcept
{
    return {value.x / divisor, value.y / divisor, value.z / divisor};
}

[[nodiscard]] inline double dot(
    const GeometryVector3& left,
    const GeometryVector3& right) noexcept
{
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] inline GeometryVector3 cross(
    const GeometryVector3& left,
    const GeometryVector3& right) noexcept
{
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] inline double squared_length(const GeometryVector3& value) noexcept
{
    return dot(value, value);
}

[[nodiscard]] inline double length(const GeometryVector3& value) noexcept
{
    return std::sqrt(squared_length(value));
}

[[nodiscard]] inline bool finite(const GeometryVector3& value) noexcept
{
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] inline GeometryVector3 normalized(
    const GeometryVector3& value,
    const std::string_view description)
{
    const double length_squared{squared_length(value)};
    if (!finite(value) || !std::isfinite(length_squared)
        || length_squared <= vector_epsilon_squared) {
        throw GeometryError{std::string{description} + " is zero-length or non-finite."};
    }
    return divide(value, std::sqrt(length_squared));
}

[[nodiscard]] inline GeometryVector3 from_millimetres(const IntegerPoint3& value) noexcept
{
    constexpr double millimetres_per_metre{1'000.0};
    return {
        static_cast<double>(value.x_millimetres) / millimetres_per_metre,
        static_cast<double>(value.y_millimetres) / millimetres_per_metre,
        static_cast<double>(value.z_millimetres) / millimetres_per_metre,
    };
}

}  // namespace crystalbound::geometry_detail
