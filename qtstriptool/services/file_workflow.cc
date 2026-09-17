#include "services/file_workflow.h"

#include <algorithm>
#include <utility>

namespace striptool {

ConfigResult FileWorkflow::open(const std::filesystem::path& path,
                                StripToolModel& model) {
  StripToolModel candidate = makeDefaultModel();
  ConfigResult result = readConfigurationFile(path, candidate);
  if (result.success) {
    candidate.filename = path.string();
    candidate.title = path.filename().string();
    model = std::move(candidate);
  }
  return result;
}

bool FileWorkflow::save(const std::filesystem::path& path, StripToolModel& model,
                        std::string* error) {
  if (!writeConfigurationFile(path, model, error)) return false;
  model.filename = path.string();
  model.title = path.filename().string();
  return true;
}

void FileWorkflow::restoreDefaults(StripToolModel& model) {
  model = makeDefaultModel();
}

std::vector<std::string> FileWorkflow::addRecent(
    const std::vector<std::string>& recent, const std::string& path,
    std::size_t maximum) {
  std::vector<std::string> result;
  if (!path.empty()) result.push_back(path);
  for (const auto& item : recent)
    if (!item.empty() && item != path && result.size() < maximum)
      result.push_back(item);
  if (result.size() > maximum) result.resize(maximum);
  return result;
}

}  // namespace striptool
