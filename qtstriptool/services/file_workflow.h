#pragma once

#include "core/config.h"

#include <filesystem>
#include <string>
#include <vector>

namespace striptool {

class FileWorkflow final {
public:
  static ConfigResult open(const std::filesystem::path& path,
                           StripToolModel& model);
  static bool save(const std::filesystem::path& path, StripToolModel& model,
                   std::string* error = nullptr);
  static void restoreDefaults(StripToolModel& model);
  static std::vector<std::string> addRecent(
      const std::vector<std::string>& recent, const std::string& path,
      std::size_t maximum = 8);
};

}  // namespace striptool
