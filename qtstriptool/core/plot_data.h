#pragma once

#include "core/model.h"
#include "core/sample_buffer.h"

#include <optional>
#include <vector>

namespace striptool {

struct ValueRange {
  double minimum = 0.0;
  double maximum = 1.0;
  bool isValid() const { return minimum < maximum; }
};

std::vector<Sample> selectSamples(
    const std::vector<Sample>& samples,
    std::chrono::system_clock::time_point start,
    std::chrono::system_clock::time_point end,
    std::size_t maximumPoints,
    ScaleMode scale = ScaleMode::Linear);

// Reduces samples for drawing with time buckets of bucketDuration, normally
// one pixel column. Buckets are aligned to multiples of bucketDuration since
// the epoch, so a sample stays in the same bucket as the view scrolls and the
// trace is stable between refreshes. Each bucket keeps its first and last
// samples, its plotable extrema and its first unplotable sample, preserving
// step levels, spikes and breaks to within one bucket.
std::vector<Sample> selectTraceSamples(
    const std::vector<Sample>& samples,
    std::chrono::system_clock::time_point start,
    std::chrono::system_clock::time_point end,
    std::chrono::system_clock::duration bucketDuration,
    ScaleMode scale = ScaleMode::Linear);

std::optional<ValueRange> sampleValueRange(const std::vector<Sample>& samples,
                                           ScaleMode scale);

// Include the value held at the left edge of the visible window. Samples use
// step semantics: a value remains active until the next update.
std::optional<ValueRange> visibleSampleValueRange(
    const std::vector<Sample>& samples,
    std::chrono::system_clock::time_point start,
    std::chrono::system_clock::time_point end,
    ScaleMode scale);

double plotValue(double value, ScaleMode scale);

// Joins sorted historical and live samples. Equal timestamps use the live
// sample, which reflects the most recently observed state.
std::vector<Sample> joinHistoricalAndLive(const std::vector<Sample>& historical,
                                          const std::vector<Sample>& live);

}  // namespace striptool
