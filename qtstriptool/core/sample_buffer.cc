#include "core/sample_buffer.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace striptool {

SampleBuffer::SampleBuffer(std::size_t requestedSamples, std::size_t memoryLimit) {
  setCapacity(requestedSamples, memoryLimit);
}

void SampleBuffer::setCapacity(std::size_t requestedSamples,
                               std::size_t memoryLimit) {
  const std::size_t memoryCapacity = memoryLimit / sizeof(Sample);
  const std::size_t newCapacity = std::max<std::size_t>(
      1, std::min(requestedSamples, std::max<std::size_t>(1, memoryCapacity)));
  auto current = storage_.empty() ? std::vector<Sample>{} : samples();
  if (current.size() > newCapacity)
    current.erase(current.begin(), current.end() - static_cast<std::ptrdiff_t>(newCapacity));
  storage_.assign(newCapacity, {});
  size_ = current.size();
  head_ = size_ % storage_.size();
  std::copy(current.begin(), current.end(), storage_.begin());
}

void SampleBuffer::append(Sample sample) {
  storage_[head_] = sample;
  head_ = (head_ + 1) % storage_.size();
  size_ = std::min(size_ + 1, storage_.size());
}

void SampleBuffer::clear() {
  head_ = 0;
  size_ = 0;
}

std::optional<Sample> SampleBuffer::latest() const {
  if (empty()) return std::nullopt;
  return storage_[(head_ + storage_.size() - 1) % storage_.size()];
}

std::vector<Sample> SampleBuffer::samples() const {
  std::vector<Sample> result;
  result.reserve(size_);
  const std::size_t begin = (head_ + storage_.size() - size_) % storage_.size();
  for (std::size_t i = 0; i < size_; ++i)
    result.push_back(storage_[(begin + i) % storage_.size()]);
  return result;
}

std::vector<Sample> decimateSamples(const std::vector<Sample>& samples,
                                    std::size_t maximumPoints) {
  if (samples.size() <= maximumPoints) return samples;
  if (maximumPoints == 0) return {};
  if (maximumPoints == 1) return {samples.back()};
  if (maximumPoints == 2) return {samples.front(), samples.back()};

  const auto hasGap = std::any_of(samples.begin(), samples.end(),
                                  [](const Sample& sample) { return !sample.plotable; });
  if (hasGap) {
    // Reserve space for a break between any two retained data points. A dense
    // series of invalid samples must not consume the entire point budget and
    // make the valid parts of a curve disappear.
    std::vector<std::size_t> validIndices;
    validIndices.reserve(samples.size());
    for (std::size_t i = 0; i < samples.size(); ++i)
      if (samples[i].plotable) validIndices.push_back(i);
    if (validIndices.empty()) return {samples.front(), samples.back()};

    const std::size_t dataBudget = (maximumPoints + 1) / 2;
    std::vector<std::size_t> selected;
    if (validIndices.size() <= dataBudget) {
      selected = validIndices;
    } else if (dataBudget == 1) {
      selected.push_back(validIndices.back());
    } else {
      // Keep original indices while selecting a min/max envelope. This lets
      // the output mark every skipped invalid interval explicitly.
      selected.push_back(validIndices.front());
      if (dataBudget > 2) {
        const std::size_t bucketCount = std::max<std::size_t>(1, (dataBudget - 2) / 2);
        const std::size_t interior = validIndices.size() - 2;
        for (std::size_t bucket = 0; bucket < bucketCount; ++bucket) {
          const std::size_t first = 1 + bucket * interior / bucketCount;
          const std::size_t last = 1 + (bucket + 1) * interior / bucketCount;
          std::size_t minimum = first;
          std::size_t maximum = first;
          for (std::size_t item = first + 1; item < last; ++item) {
            if (samples[validIndices[item]].value < samples[validIndices[minimum]].value)
              minimum = item;
            if (samples[validIndices[item]].value > samples[validIndices[maximum]].value)
              maximum = item;
          }
          if (minimum > maximum) std::swap(minimum, maximum);
          if (selected.size() < dataBudget - 1) selected.push_back(validIndices[minimum]);
          if (minimum != maximum && selected.size() < dataBudget - 1)
            selected.push_back(validIndices[maximum]);
        }
      }
      selected.push_back(validIndices.back());
    }

    std::vector<Sample> result;
    result.reserve(maximumPoints);
    if (selected.front() > 0 && 2 * selected.size() <= maximumPoints)
      result.push_back(samples.front());
    for (std::size_t i = 0; i < selected.size(); ++i) {
      if (i > 0) {
        const auto gap = std::find_if(
            samples.begin() + static_cast<std::ptrdiff_t>(selected[i - 1] + 1),
            samples.begin() + static_cast<std::ptrdiff_t>(selected[i]),
            [](const Sample& sample) { return !sample.plotable; });
        if (gap != samples.begin() + static_cast<std::ptrdiff_t>(selected[i]))
          result.push_back(*gap);
      }
      result.push_back(samples[selected[i]]);
    }
    if (selected.back() + 1 < samples.size() && result.size() < maximumPoints)
      result.push_back(samples.back());
    return result;
  }

  if (maximumPoints < 6) {
    auto minimum = std::min_element(samples.begin() + 1, samples.end() - 1,
        [](const Sample& left, const Sample& right) {
          return left.value < right.value;
        });
    auto maximum = std::max_element(samples.begin() + 1, samples.end() - 1,
        [](const Sample& left, const Sample& right) {
          return left.value < right.value;
        });
    std::vector<std::size_t> selected{0,
        static_cast<std::size_t>(minimum - samples.begin()),
        static_cast<std::size_t>(maximum - samples.begin()),
        samples.size() - 1};
    std::sort(selected.begin(), selected.end());
    selected.erase(std::unique(selected.begin(), selected.end()), selected.end());
    while (selected.size() > maximumPoints) selected.erase(selected.begin() + 1);
    std::vector<Sample> result;
    result.reserve(selected.size());
    for (const std::size_t index : selected) result.push_back(samples[index]);
    return result;
  }

  std::vector<Sample> result;
  result.reserve(maximumPoints);
  const std::size_t interiorSlots = maximumPoints - 2;
  // Step traces need the update after an extremum as well as the extremum
  // itself; otherwise an isolated spike can be held across a whole bucket.
  const std::size_t bucketCount = std::max<std::size_t>(1, interiorSlots / 4);
  const std::size_t interior = samples.size() - 2;
  std::vector<std::size_t> selected{0};
  for (std::size_t bucket = 0; bucket < bucketCount; ++bucket) {
    const std::size_t first = 1 + bucket * interior / bucketCount;
    const std::size_t last = 1 + (bucket + 1) * interior / bucketCount;
    auto minimum = samples.begin() + static_cast<std::ptrdiff_t>(first);
    auto maximum = minimum;
    for (auto it = minimum; it != samples.begin() + static_cast<std::ptrdiff_t>(last); ++it) {
      if (it->value < minimum->value) minimum = it;
      if (it->value > maximum->value) maximum = it;
    }
    const std::size_t minimumIndex =
        static_cast<std::size_t>(minimum - samples.begin());
    const std::size_t maximumIndex =
        static_cast<std::size_t>(maximum - samples.begin());
    selected.push_back(minimumIndex);
    selected.push_back(maximumIndex);
    if (minimumIndex + 1 < samples.size() - 1)
      selected.push_back(minimumIndex + 1);
    if (maximumIndex + 1 < samples.size() - 1)
      selected.push_back(maximumIndex + 1);
  }
  selected.push_back(samples.size() - 1);
  std::sort(selected.begin(), selected.end());
  selected.erase(std::unique(selected.begin(), selected.end()), selected.end());
  for (const std::size_t index : selected) result.push_back(samples[index]);
  return result;
}

}  // namespace striptool
