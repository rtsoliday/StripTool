#pragma once

#include "core/model.h"

#include <filesystem>
#include <istream>
#include <ostream>
#include <string>
#include <vector>

namespace striptool {

struct ConfigDiagnostic {
  std::size_t line = 0;
  std::string message;
};

struct ConfigResult {
  bool success = false;
  bool legacyFormat = false;
  std::vector<ConfigDiagnostic> diagnostics;
};

ConfigResult readConfiguration(std::istream& input, StripToolModel& model);
ConfigResult readConfigurationFile(const std::filesystem::path& path,
                                   StripToolModel& model);
bool writeConfiguration(std::ostream& output, const StripToolModel& model,
                        std::string* error = nullptr);
bool writeConfigurationFile(const std::filesystem::path& path,
                            const StripToolModel& model,
                            std::string* error = nullptr);

// Applies existing files from lowest to highest precedence. Missing optional
// layers are ignored; a malformed existing layer fails transactionally.
ConfigResult loadConfigurationLayers(
    const std::vector<std::filesystem::path>& paths, StripToolModel& model);

// Matches the legacy command-line lookup: direct paths are tried as-is;
// bare names are tried in the working directory and then each search entry.
std::filesystem::path findConfigurationFile(
    const std::filesystem::path& name,
    const std::filesystem::path& workingDirectory,
    const std::string& searchPath);

// Uses the explicit current-directory/search-path lookup first, then falls
// back to <workingDirectory>/StripTool.stp like the legacy startup sequence.
std::filesystem::path findStartupConfiguration(
    const std::string& explicitName,
    const std::filesystem::path& workingDirectory,
    const std::string& searchPath);

}  // namespace striptool
