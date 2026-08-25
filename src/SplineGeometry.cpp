#include "crystalbound/Geometry.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <string>
#include <utility>

#include "GeometryMath.hpp"

namespace crystalbound {
namespace {

using namespace geometry_detail;

constexpr std::size_t maximum_control_point_count{64U};
constexpr std::uint32_t minimum_ring_side_count{5U};
constexpr std::uint32_t maximum_ring_side_count{32U};
constexpr std::int32_t maximum_route_radius_millimetres{20'000};
constexpr double tangent_sample_delta{1.0e-5};
constexpr double pi{3.14159265358979323846};
constexpr double maximum_radius_curvature_product{0.75};

struct Segment {
    GeometryVector3 first_outer{};
    GeometryVector3 first{};
    GeometryVector3 second{};
    GeometryVector3 second_outer{};
};

struct RawSample {
    GeometryVector3 position{};
    std::size_t segment_index{};
    double parameter{};
};

[[nodiscard]] GeometryVector3 interpolate(
    const GeometryVector3& first,
    const GeometryVector3& second,
    const double first_knot,
    const double second_knot,
    const double parameter)
{
    const double denominator{second_knot - first_knot};
    if (!std::isfinite(denominator) || denominator <= 0.0) {
        throw GeometryError{"Centripetal Catmull-Rom knots are not strictly increasing."};
    }
    const double first_weight{(second_knot - parameter) / denominator};
    const double second_weight{(parameter - first_knot) / denominator};
    return add(multiply(first, first_weight), multiply(second, second_weight));
}

[[nodiscard]] double next_knot(
    const double current,
    const GeometryVector3& first,
    const GeometryVector3& second)
{
    // alpha = 0.5, so the knot increment is distance^(1/2).
    const double increment{std::sqrt(length(subtract(second, first)))};
    if (!std::isfinite(increment) || increment <= 0.0) {
        throw GeometryError{"Centripetal Catmull-Rom requires distinct adjacent points."};
    }
    return current + increment;
}

[[nodiscard]] GeometryVector3 evaluate_segment(const Segment& segment, const double parameter)
{
    const double knot0{0.0};
    const double knot1{next_knot(knot0, segment.first_outer, segment.first)};
    const double knot2{next_knot(knot1, segment.first, segment.second)};
    const double knot3{next_knot(knot2, segment.second, segment.second_outer)};
    const double knot_parameter{knot1 + (knot2 - knot1) * parameter};

    const GeometryVector3 a1{interpolate(
        segment.first_outer, segment.first, knot0, knot1, knot_parameter)};
    const GeometryVector3 a2{interpolate(
        segment.first, segment.second, knot1, knot2, knot_parameter)};
    const GeometryVector3 a3{interpolate(
        segment.second, segment.second_outer, knot2, knot3, knot_parameter)};
    const GeometryVector3 b1{interpolate(a1, a2, knot0, knot2, knot_parameter)};
    const GeometryVector3 b2{interpolate(a2, a3, knot1, knot3, knot_parameter)};
    const GeometryVector3 result{interpolate(b1, b2, knot1, knot2, knot_parameter)};
    if (!finite(result)) {
        throw GeometryError{"Centripetal Catmull-Rom produced a non-finite position."};
    }
    return result;
}

[[nodiscard]] GeometryVector3 tangent_at(const Segment& segment, const double parameter)
{
    const double lower{std::max(0.0, parameter - tangent_sample_delta)};
    const double upper{std::min(1.0, parameter + tangent_sample_delta)};
    return normalized(
        subtract(evaluate_segment(segment, upper), evaluate_segment(segment, lower)),
        "Spline tangent");
}

[[nodiscard]] Segment make_segment(
    const std::vector<GeometryVector3>& points,
    const std::size_t index)
{
    const GeometryVector3& first{points[index]};
    const GeometryVector3& second{points[index + 1U]};
    const GeometryVector3 first_outer = index == 0U
        ? add(first, subtract(first, second))
        : points[index - 1U];
    const GeometryVector3 second_outer = index + 2U == points.size()
        ? add(second, subtract(second, first))
        : points[index + 2U];
    return {first_outer, first, second, second_outer};
}

void append_adaptive_samples(
    const Segment& segment,
    const std::size_t segment_index,
    const double first_parameter,
    const GeometryVector3& first,
    const double second_parameter,
    const GeometryVector3& second,
    const double maximum_spacing,
    const double maximum_error,
    const std::uint32_t depth,
    const std::uint32_t maximum_depth,
    std::vector<RawSample>& output)
{
    const double middle_parameter{(first_parameter + second_parameter) * 0.5};
    const GeometryVector3 middle{evaluate_segment(segment, middle_parameter)};
    const GeometryVector3 chord_middle{multiply(add(first, second), 0.5)};
    const double chord_length{length(subtract(second, first))};
    const double chord_error{length(subtract(middle, chord_middle))};
    if (chord_length <= maximum_spacing && chord_error <= maximum_error) {
        if (output.size() >= geometry_budgets.maximum_static_vertices) {
            throw GeometryError{"Spline sampling exceeded the geometry sample budget."};
        }
        output.push_back({second, segment_index, second_parameter});
        return;
    }
    if (depth >= maximum_depth) {
        throw GeometryError{"Spline subdivision could not meet spacing and curvature error."};
    }
    append_adaptive_samples(
        segment,
        segment_index,
        first_parameter,
        first,
        middle_parameter,
        middle,
        maximum_spacing,
        maximum_error,
        depth + 1U,
        maximum_depth,
        output);
    append_adaptive_samples(
        segment,
        segment_index,
        middle_parameter,
        middle,
        second_parameter,
        second,
        maximum_spacing,
        maximum_error,
        depth + 1U,
        maximum_depth,
        output);
}

void validate_sampling_contract(const SplineSamplingContract& contract)
{
    if (contract.maximum_sample_spacing_millimetres <= 0
        || contract.maximum_chord_error_millimetres <= 0
        || contract.maximum_overshoot_millimetres < 0) {
        throw GeometryError{
            "Spline spacing and chord error must be positive and overshoot non-negative."};
    }
    if (contract.maximum_grade_millidegrees <= 0
        || contract.maximum_grade_millidegrees >= 90'000) {
        throw GeometryError{"Spline maximum grade must be between zero and 90 degrees."};
    }
    if (contract.maximum_frame_turn_millidegrees <= 0
        || contract.maximum_frame_turn_millidegrees >= 180'000) {
        throw GeometryError{"Spline maximum frame turn must be between zero and 180 degrees."};
    }
    if (contract.maximum_subdivision_depth == 0U
        || contract.maximum_subdivision_depth > 24U) {
        throw GeometryError{"Spline subdivision depth must be in the range 1..24."};
    }
}

void validate_sample_envelope(
    const std::vector<SplineSample>& samples,
    const SplineSamplingContract& contract,
    const double route_radius_metres)
{
    const double maximum_grade_radians{
        static_cast<double>(contract.maximum_grade_millidegrees) * pi / 180'000.0};
    const double maximum_turn_radians{
        static_cast<double>(contract.maximum_frame_turn_millidegrees) * pi / 180'000.0};
    for (std::size_t index{}; index < samples.size(); ++index) {
        const GeometryVector3& tangent{samples[index].tangent};
        const double horizontal{std::hypot(tangent.x, tangent.z)};
        const double grade{std::atan2(std::abs(tangent.y), horizontal)};
        if (!std::isfinite(grade) || grade > maximum_grade_radians + 1.0e-10) {
            throw GeometryError{"Spline exceeds the locked maximum grade."};
        }
        if (index == 0U) {
            continue;
        }
        const double tangent_dot{std::clamp(
            dot(samples[index - 1U].tangent, tangent), -1.0, 1.0)};
        const double turn{std::acos(tangent_dot)};
        if (!std::isfinite(turn) || turn > maximum_turn_radians + 1.0e-10) {
            throw GeometryError{"Spline curvature exceeds the frame-turn contract."};
        }
        const double step{
            samples[index].distance_metres - samples[index - 1U].distance_metres};
        if (!std::isfinite(step) || step <= 0.0
            || turn / step * route_radius_metres
                > maximum_radius_curvature_product) {
            throw GeometryError{
                "Spline curvature is too tight for its requested ring radius."};
        }
    }
}

[[nodiscard]] GeometryVector3 projected_normal(
    const GeometryVector3& candidate,
    const GeometryVector3& tangent)
{
    return subtract(candidate, multiply(tangent, dot(candidate, tangent)));
}

}  // namespace

void validate_spline_sampling_contract(const SplineSamplingContract& contract)
{
    validate_sampling_contract(contract);
}

void validate_spline_route_input(const SplineRouteInput& input)
{
    if (input.control_points.size() < 2U) {
        throw GeometryError{"A spline route requires at least two control points."};
    }
    if (input.control_points.size() > maximum_control_point_count) {
        throw GeometryError{"A spline route exceeds the control-point budget."};
    }
    for (std::size_t index{1}; index < input.control_points.size(); ++index) {
        if (input.control_points[index] == input.control_points[index - 1U]) {
            throw GeometryError{"A spline route contains identical adjacent control points."};
        }
    }
    const std::int32_t minimum_radius{std::max(
        (movement_envelope.minimum_clearance_width_millimetres
            + movement_envelope.safety_margin_millimetres + 1)
            / 2,
        (movement_envelope.minimum_clearance_height_millimetres
            + movement_envelope.safety_margin_millimetres + 1)
            / 2)};
    if (input.radius_millimetres < minimum_radius) {
        throw GeometryError{"Spline ring radius does not meet the locked traversal clearance."};
    }
    if (input.radius_millimetres > maximum_route_radius_millimetres) {
        throw GeometryError{"Spline ring radius exceeds the route geometry limit."};
    }
    if (input.ring_side_count < minimum_ring_side_count
        || input.ring_side_count > maximum_ring_side_count) {
        throw GeometryError{"Spline ring sides must be in the range 5..32."};
    }
}

std::vector<SplineSample> sample_centripetal_catmull_rom(
    const SplineRouteInput& input,
    const SplineSamplingContract& contract)
{
    validate_spline_route_input(input);
    validate_spline_sampling_contract(contract);

    std::vector<GeometryVector3> points;
    points.reserve(input.control_points.size());
    for (const IntegerPoint3& point : input.control_points) {
        points.push_back(from_millimetres(point));
    }
    GeometryVector3 minimum{points.front()};
    GeometryVector3 maximum{points.front()};
    for (const GeometryVector3& point : points) {
        minimum.x = std::min(minimum.x, point.x);
        minimum.y = std::min(minimum.y, point.y);
        minimum.z = std::min(minimum.z, point.z);
        maximum.x = std::max(maximum.x, point.x);
        maximum.y = std::max(maximum.y, point.y);
        maximum.z = std::max(maximum.z, point.z);
    }
    const double allowed_overshoot{
        static_cast<double>(contract.maximum_overshoot_millimetres) / 1'000.0};

    const double maximum_spacing{
        static_cast<double>(contract.maximum_sample_spacing_millimetres) / 1'000.0};
    const double maximum_error{
        static_cast<double>(contract.maximum_chord_error_millimetres) / 1'000.0};
    std::vector<Segment> segments;
    segments.reserve(points.size() - 1U);
    std::vector<RawSample> raw_samples;
    for (std::size_t index{}; index + 1U < points.size(); ++index) {
        const Segment segment{make_segment(points, index)};
        segments.push_back(segment);
        const GeometryVector3 first{evaluate_segment(segment, 0.0)};
        const GeometryVector3 second{evaluate_segment(segment, 1.0)};
        if (raw_samples.empty()) {
            raw_samples.push_back({first, index, 0.0});
        }
        append_adaptive_samples(
            segment,
            index,
            0.0,
            first,
            1.0,
            second,
            maximum_spacing,
            maximum_error,
            0U,
            contract.maximum_subdivision_depth,
            raw_samples);
    }

    std::vector<SplineSample> samples;
    samples.reserve(raw_samples.size());
    double distance_metres{};
    for (std::size_t index{}; index < raw_samples.size(); ++index) {
        const RawSample& raw{raw_samples[index]};
        if (raw.position.x < minimum.x - allowed_overshoot
            || raw.position.x > maximum.x + allowed_overshoot
            || raw.position.y < minimum.y - allowed_overshoot
            || raw.position.y > maximum.y + allowed_overshoot
            || raw.position.z < minimum.z - allowed_overshoot
            || raw.position.z > maximum.z + allowed_overshoot) {
            throw GeometryError{"Spline exceeds the locked overshoot allowance."};
        }
        if (index != 0U) {
            const double step{length(subtract(raw.position, raw_samples[index - 1U].position))};
            if (!std::isfinite(step) || step <= 0.0 || step > maximum_spacing + 1.0e-9) {
                throw GeometryError{"Spline produced invalid or over-spaced samples."};
            }
            distance_metres += step;
        }
        samples.push_back({
            raw.position,
            tangent_at(segments[raw.segment_index], raw.parameter),
            distance_metres,
        });
    }
    validate_sample_envelope(
        samples,
        contract,
        static_cast<double>(input.radius_millimetres) / 1'000.0);
    return samples;
}

std::vector<TransportFrame> build_parallel_transport_frames(
    const std::vector<SplineSample>& samples)
{
    if (samples.size() < 2U) {
        throw GeometryError{"Parallel transport requires at least two spline samples."};
    }
    for (std::size_t index{}; index < samples.size(); ++index) {
        if (!finite(samples[index].position_metres)
            || !std::isfinite(samples[index].distance_metres)) {
            throw GeometryError{"Parallel transport received a non-finite sample."};
        }
        if (index != 0U
            && samples[index].distance_metres <= samples[index - 1U].distance_metres) {
            throw GeometryError{"Parallel transport sample distances must increase."};
        }
    }

    std::vector<TransportFrame> frames;
    frames.reserve(samples.size());
    const GeometryVector3 first_tangent{normalized(samples.front().tangent, "Frame tangent")};
    GeometryVector3 first_normal{projected_normal({0.0, 1.0, 0.0}, first_tangent)};
    if (squared_length(first_normal) <= vector_epsilon_squared) {
        first_normal = projected_normal({1.0, 0.0, 0.0}, first_tangent);
    }
    first_normal = normalized(first_normal, "Initial frame normal");
    GeometryVector3 first_binormal{normalized(
        cross(first_tangent, first_normal), "Initial frame binormal")};
    first_normal = normalized(cross(first_binormal, first_tangent), "Initial frame normal");
    frames.push_back({
        samples.front().position_metres,
        first_tangent,
        first_normal,
        first_binormal,
    });

    for (std::size_t index{1}; index < samples.size(); ++index) {
        const GeometryVector3 tangent{normalized(samples[index].tangent, "Frame tangent")};
        GeometryVector3 normal{projected_normal(frames.back().normal, tangent)};
        if (squared_length(normal) <= vector_epsilon_squared) {
            normal = projected_normal(frames.back().binormal, tangent);
        }
        normal = normalized(normal, "Transported frame normal");
        GeometryVector3 binormal{normalized(cross(tangent, normal), "Transported binormal")};
        normal = normalized(cross(binormal, tangent), "Transported frame normal");
        if (dot(frames.back().normal, normal) < 0.0) {
            normal = multiply(normal, -1.0);
            binormal = multiply(binormal, -1.0);
        }
        frames.push_back({samples[index].position_metres, tangent, normal, binormal});
    }
    return frames;
}

}  // namespace crystalbound
