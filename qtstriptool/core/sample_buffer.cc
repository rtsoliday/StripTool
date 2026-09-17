#include "core/sample_buffer.h"

#include <algorithm>
#include <cmath>

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

  std::vector<Sample> result;
  result.reserve(maximumPoints);
  result.push_back(samples.front());
  const std::size_t interiorSlots = maximumPoints - 2;
  const std::size_t bucketCount = std::max<std::size_t>(1, interiorSlots / 2);
  const std::size_t interior = samples.size() - 2;
  for (std::size_t bucket = 0; bucket < bucketCount; ++bucket) {
    const std::size_t first = 1 + bucket * interior / bucketCount;
    const std::size_t last = 1 + (bucket + 1) * interior / bucketCount;
    auto minimum = samples.begin() + static_cast<std::ptrdiff_t>(first);
    auto maximum = minimum;
    for (auto it = minimum; it != samples.begin() + static_cast<std::ptrdiff_t>(last); ++it) {
      if (it->value < minimum->value) minimum = it;
      if (it->value > maximum->value) maximum = it;
    }
    if (minimum < maximum) {
      if (result.size() < maximumPoints - 1) result.push_back(*minimum);
      if (result.size() < maximumPoints - 1) result.push_back(*maximum);
    } else if (maximum < minimum) {
      if (result.size() < maximumPoints - 1) result.push_back(*maximum);
      if (result.size() < maximumPoints - 1) result.push_back(*minimum);
    } else {
      if (result.size() < maximumPoints - 1) result.push_back(*minimum);
    }
  }
  result.push_back(samples.back());
  return result;
}

}  // namespace striptool
