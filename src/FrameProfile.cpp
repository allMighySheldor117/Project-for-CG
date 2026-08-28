#include "crystalbound/FrameProfile.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>
#include <vector>

namespace crystalbound {
namespace {

struct Percentiles {
    double median{};
    double p95{};
};

[[nodiscard]] Percentiles percentiles(std::vector<double> values)
{
    std::sort(values.begin(), values.end());
    const std::size_t middle{values.size() / 2U};
    const double median{values.size() % 2U == 0U
            ? (values[middle - 1U] + values[middle]) * 0.5
            : values[middle]};
    const std::size_t p95_rank{
        static_cast<std::size_t>(std::ceil(0.95 * static_cast<double>(values.size())))};
    return {median, values[std::max<std::size_t>(1U, p95_rank) - 1U]};
}

}  // namespace

FrameProfiler::FrameProfiler(
    const double warmup_seconds,
    const double measured_seconds,
    const std::size_t expected_sample_count)
    : warmup_milliseconds_(warmup_seconds * 1'000.0),
      measured_milliseconds_(measured_seconds * 1'000.0)
{
    if (!std::isfinite(warmup_seconds) || warmup_seconds < 0.0
        || !std::isfinite(measured_seconds) || measured_seconds <= 0.0
        || expected_sample_count == 0U) {
        throw std::invalid_argument(
            "Frame profiling requires finite non-negative warm-up, positive duration, and sample capacity.");
    }
    samples_.reserve(expected_sample_count);
}

bool FrameProfiler::record(const FrameTimingSample& sample)
{
    if (!std::isfinite(sample.total_milliseconds)
        || !std::isfinite(sample.cpu_before_present_milliseconds)
        || !std::isfinite(sample.present_milliseconds)
        || sample.total_milliseconds < 0.0
        || sample.cpu_before_present_milliseconds < 0.0
        || sample.present_milliseconds < 0.0
        || sample.cpu_before_present_milliseconds + sample.present_milliseconds
            > sample.total_milliseconds + 0.001) {
        throw std::invalid_argument(
            "Frame timing components must be finite, non-negative, and bounded by total time.");
    }
    if (finished_) {
        return true;
    }
    const double previous_elapsed{elapsed_milliseconds_};
    elapsed_milliseconds_ += sample.total_milliseconds;
    if (previous_elapsed >= warmup_milliseconds_) {
        samples_.push_back(sample);
        measured_elapsed_milliseconds_ += sample.total_milliseconds;
    }
    finished_ = measured_elapsed_milliseconds_ >= measured_milliseconds_;
    return finished_;
}

bool FrameProfiler::finished() const noexcept
{
    return finished_;
}

FrameProfileSummary FrameProfiler::summary() const
{
    if (!finished_ || samples_.empty()) {
        throw std::logic_error("Frame profile summary is unavailable before completion.");
    }
    std::vector<double> totals;
    std::vector<double> cpu_before_present;
    std::vector<double> present;
    totals.reserve(samples_.size());
    cpu_before_present.reserve(samples_.size());
    present.reserve(samples_.size());
    for (const FrameTimingSample& sample : samples_) {
        totals.push_back(sample.total_milliseconds);
        cpu_before_present.push_back(sample.cpu_before_present_milliseconds);
        present.push_back(sample.present_milliseconds);
    }
    const Percentiles total_percentiles{percentiles(std::move(totals))};
    const Percentiles cpu_percentiles{percentiles(std::move(cpu_before_present))};
    const Percentiles present_percentiles{percentiles(std::move(present))};
    return {
        samples_.size(),
        total_percentiles.median,
        total_percentiles.p95,
        cpu_percentiles.median,
        cpu_percentiles.p95,
        present_percentiles.median,
        present_percentiles.p95,
    };
}

}  // namespace crystalbound
