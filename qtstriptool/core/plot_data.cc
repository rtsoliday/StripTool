#include "core/plot_data.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace striptool {

std::vector<Sample> selectSamples(
    const std::vector<Sample>& samples,
    std::chrono::system_clock::time_point start,
    std::chrono::system_clock::time_point end,
    std::size_t maximumPoints) {
  if (start > end || maximumPoints == 0) return {};
  const auto first = std::lower_bound(
      samples.begin(), samples.end(), start,
      [](const Sample& sample, const auto& time) { return sample.timestamp < time; });
  const auto last = std::upper_bound(
      first, samples.end(), end,
      [](const auto& time, const Sample& sample) { return time < sample.timestamp; });
  return decimateSamples(std::vector<Sample>(first, last), maximumPoints);
}

double plotValue(double value, ScaleMode scale) {
  if (scale == ScaleMode::Log10)
    return value > 0.0 ? std::log10(value)
                       : std::numeric_limits<double>::quiet_NaN();
  return value;
}

std::optional<ValueRange> sampleValueRange(const std::vector<Sample>& samples,
                                           ScaleMode scale) {
  ValueRange range{std::numeric_limits<double>::infinity(),
                   -std::numeric_limits<double>::infinity()};
  for (const auto& sample : samples) {
    const double value = plotValue(sample.value, scale);
    if (!std::isfinite(value)) continue;
    range.minimum = std::min(range.minimum, value);
    range.maximum = std::max(range.maximum, value);
  }
  if (!std::isfinite(range.minimum)) return std::nullopt;
  if (range.minimum == range.maximum) {
    const double padding = range.minimum == 0.0 ? 1.0 : std::abs(range.minimum) * 0.05;
    range.minimum -= padding;
    range.maximum += padding;
  }
  return range;
}

std::vector<Sample> joinHistoricalAndLive(const std::vector<Sample>& historical,
                                          const std::vector<Sample>& live) {
  std::vector<Sample> result;
  result.reserve(historical.size() + live.size());
  std::size_t h = 0;
  std::size_t l = 0;
  while (h < historical.size() || l < live.size()) {
    if (h == historical.size()) result.push_back(live[l++]);
    else if (l == live.size()) result.push_back(historical[h++]);
    else if (historical[h].timestamp < live[l].timestamp)
      result.push_back(historical[h++]);
    else if (live[l].timestamp < historical[h].timestamp)
      result.push_back(live[l++]);
    else {
      result.push_back(live[l++]);
      ++h;
    }
  }
  return result;
}

}  // namespace striptool
