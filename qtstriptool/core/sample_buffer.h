#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace striptool {

struct Sample {
  std::chrono::system_clock::time_point timestamp;
  double value = 0.0;
  std::uint16_t status = 0;
  std::uint16_t severity = 0;
  bool plotable = true;
};

class SampleBuffer {
public:
  static constexpr std::size_t kDefaultMemoryLimit = 8U * 1024U * 1024U;

  explicit SampleBuffer(std::size_t requestedSamples = 7200,
                        std::size_t memoryLimit = kDefaultMemoryLimit);

  void setCapacity(std::size_t requestedSamples,
                   std::size_t memoryLimit = kDefaultMemoryLimit);
  void append(Sample sample);
  void clear();

  std::size_t size() const { return size_; }
  std::size_t capacity() const { return storage_.size(); }
  bool empty() const { return size_ == 0; }
  std::optional<Sample> latest() const;
  std::vector<Sample> samples() const;

private:
  std::vector<Sample> storage_;
  std::size_t head_ = 0;
  std::size_t size_ = 0;
};

// Produces a chronological min/max envelope suitable for a pixel-width plot.
// The first and last samples are retained and spikes survive bucket reduction.
std::vector<Sample> decimateSamples(const std::vector<Sample>& samples,
                                    std::size_t maximumPoints);

}  // namespace striptool
