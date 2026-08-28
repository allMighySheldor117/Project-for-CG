#pragma once

#include <cstddef>
#include <optional>
#include <vector>

namespace crystalbound {

struct FrameProfileSummary {
    std::size_t sample_count{};
    double median_milliseconds{};
    double p95_milliseconds{};
    double median_cpu_before_present_milliseconds{};
    double p95_cpu_before_present_milliseconds{};
    double median_present_milliseconds{};
    double p95_present_milliseconds{};
};

struct FrameTimingSample {
    double total_milliseconds{};
    double cpu_before_present_milliseconds{};
    double present_milliseconds{};
};

class FrameProfiler final {
public:
    FrameProfiler(
        double warmup_seconds,
        double measured_seconds,
        std::size_t expected_sample_count);

    [[nodiscard]] bool record(const FrameTimingSample& sample);
    [[nodiscard]] bool finished() const noexcept;
    [[nodiscard]] FrameProfileSummary summary() const;

private:
    double warmup_milliseconds_{};
    double measured_milliseconds_{};
    double elapsed_milliseconds_{};
    double measured_elapsed_milliseconds_{};
    bool finished_{};
    std::vector<FrameTimingSample> samples_{};
};

}  // namespace crystalbound
