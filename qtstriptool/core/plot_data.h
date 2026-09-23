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

std::optional<ValueRange> sampleValueRange(const std::vector<Sample>& samples,
                                           ScaleMode scale);

// Include the values where line segments cross the edges of the visible window.
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
