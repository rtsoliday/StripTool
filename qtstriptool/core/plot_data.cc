#include "core/plot_data.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <iterator>
#include <limits>

namespace striptool {
namespace {
ValueRange padConstantRange(ValueRange range) {
  if (range.minimum == range.maximum) {
    const double padding = range.minimum == 0.0 ? 1.0 : std::abs(range.minimum) * 0.05;
    range.minimum = std::max(std::numeric_limits<double>::lowest(), range.minimum - padding);
    range.maximum = std::min(std::numeric_limits<double>::max(), range.maximum + padding);
  }
  return range;
}
}

std::vector<Sample> selectSamples(
    const std::vector<Sample>& samples,
    std::chrono::system_clock::time_point start,
    std::chrono::system_clock::time_point end,
    std::size_t maximumPoints,
    ScaleMode scale) {
  if (start > end || maximumPoints == 0) return {};
  const auto first = std::lower_bound(
      samples.begin(), samples.end(), start,
      [](const Sample& sample, const auto& time) { return sample.timestamp < time; });
  const auto last = std::upper_bound(
      first, samples.end(), end,
      [](const auto& time, const Sample& sample) { return time < sample.timestamp; });
  std::vector<Sample> visible(first, last);
  for (auto& sample : visible)
    if (!std::isfinite(sample.value) ||
        (scale == ScaleMode::Log10 && !(sample.value > 0.0)))
      sample.plotable = false;
  return decimateSamples(visible, maximumPoints);
}

std::vector<Sample> selectTraceSamples(
    const std::vector<Sample>& samples,
    std::chrono::system_clock::time_point start,
    std::chrono::system_clock::time_point end,
    std::chrono::system_clock::duration bucketDuration,
    ScaleMode scale) {
  if (start > end || bucketDuration <= bucketDuration.zero()) return {};
  const auto first = std::lower_bound(
      samples.begin(), samples.end(), start,
      [](const Sample& sample, const auto& time) { return sample.timestamp < time; });
  const auto last = std::upper_bound(
      first, samples.end(), end,
      [](const auto& time, const Sample& sample) { return time < sample.timestamp; });
  const auto plotable = [scale](const Sample& sample) {
    return sample.plotable && std::isfinite(sample.value) &&
           (scale != ScaleMode::Log10 || sample.value > 0.0);
  };
  const auto bucketOf = [bucketDuration](std::chrono::system_clock::time_point time) {
    const auto ticks = time.time_since_epoch().count();
    const auto width = bucketDuration.count();
    return ticks / width - (ticks % width < 0 ? 1 : 0);
  };

  std::vector<Sample> result;
  for (auto bucketStart = first; bucketStart != last;) {
    // Compare bucket indices rather than constructing the upper boundary:
    // that boundary can overflow for the last representable clock bucket.
    const auto bucketIndex = bucketOf(bucketStart->timestamp);
    auto minimum = last, maximum = last, gap = last, bucketEnd = bucketStart;
    for (; bucketEnd != last && bucketOf(bucketEnd->timestamp) == bucketIndex; ++bucketEnd) {
      if (!plotable(*bucketEnd)) {
        if (gap == last) gap = bucketEnd;
        continue;
      }
      if (minimum == last || bucketEnd->value < minimum->value) minimum = bucketEnd;
      if (maximum == last || bucketEnd->value > maximum->value) maximum = bucketEnd;
    }
    std::array<std::vector<Sample>::const_iterator, 5> kept{};
    std::size_t count = 0;
    for (const auto it : {bucketStart, minimum, maximum, gap, std::prev(bucketEnd)})
      if (it != last) kept[count++] = it;
    std::sort(kept.begin(), kept.begin() + count);
    const auto keptEnd = std::unique(kept.begin(), kept.begin() + count);
    for (auto it = kept.begin(); it != keptEnd; ++it) {
      Sample sample = **it;
      sample.plotable = plotable(sample);
      result.push_back(sample);
    }
    bucketStart = bucketEnd;
  }
  return result;
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
    if (!sample.plotable) continue;
    const double value = plotValue(sample.value, scale);
    if (!std::isfinite(value)) continue;
    range.minimum = std::min(range.minimum, value);
    range.maximum = std::max(range.maximum, value);
  }
  if (!std::isfinite(range.minimum)) return std::nullopt;
  return padConstantRange(range);
}

std::optional<ValueRange> visibleSampleValueRange(
    const std::vector<Sample>& samples,
    std::chrono::system_clock::time_point start,
    std::chrono::system_clock::time_point end,
    ScaleMode scale) {
  if (start > end || samples.empty()) return std::nullopt;
  const auto first = std::lower_bound(samples.begin(), samples.end(), start,
      [](const Sample& sample, const auto& time) { return sample.timestamp < time; });
  const auto last = std::upper_bound(first, samples.end(), end,
      [](const auto& time, const Sample& sample) { return time < sample.timestamp; });
  ValueRange range{std::numeric_limits<double>::infinity(),
                   -std::numeric_limits<double>::infinity()};
  const auto include = [&range](double value) {
    if (!std::isfinite(value)) return;
    range.minimum = std::min(range.minimum, value);
    range.maximum = std::max(range.maximum, value);
  };
  for (auto it = first; it != last; ++it)
    if (it->plotable) include(plotValue(it->value, scale));

  // Samples are held until the next monitor update. If the visible window
  // begins between updates, include the value active at its left edge. An
  // invalid sample ends that held segment and therefore must not be crossed.
  if (first != samples.begin()) {
    const auto before = std::prev(first);
    if (before->plotable) include(plotValue(before->value, scale));
  }

  if (!std::isfinite(range.minimum)) return std::nullopt;
  return padConstantRange(range);
}

std::vector<Sample> joinHistoricalAndLive(const std::vector<Sample>& historical,
                                          const std::vector<Sample>& live) {
  std::vector<Sample> result;
  result.reserve(historical.size() + live.size());
  // Archived samples normally all precede the live ones; copy them in bulk.
  const auto split = live.empty() ? historical.end()
      : std::lower_bound(historical.begin(), historical.end(), live.front().timestamp,
          [](const Sample& sample, const auto& time) { return sample.timestamp < time; });
  result.insert(result.end(), historical.begin(), split);
  std::size_t h = static_cast<std::size_t>(split - historical.begin());
  std::size_t l = 0;
  while (h < historical.size() || l < live.size()) {
    if (h == historical.size()) {
      result.insert(result.end(), live.begin() + static_cast<std::ptrdiff_t>(l), live.end());
      break;
    }
    if (l == live.size()) result.push_back(historical[h++]);
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
