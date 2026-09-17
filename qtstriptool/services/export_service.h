#pragma once

#include "core/model.h"
#include "core/sample_buffer.h"

#include <array>
#include <filesystem>
#include <iosfwd>
#include <string>
#include <vector>

namespace striptool {

using CurveSamples = std::array<std::vector<Sample>, kMaximumCurves>;

class ExportService final {
public:
  static bool writeText(std::ostream& output, const StripToolModel& model,
                        const CurveSamples& samples);
  static bool writeCsv(std::ostream& output, const StripToolModel& model,
                       const CurveSamples& samples);
  static bool writeTextFile(const std::filesystem::path& path,
                            const StripToolModel& model,
                            const CurveSamples& samples,
                            std::string* error = nullptr);
  static bool writeCsvFile(const std::filesystem::path& path,
                           const StripToolModel& model,
                           const CurveSamples& samples,
                           std::string* error = nullptr);
};

}  // namespace striptool
