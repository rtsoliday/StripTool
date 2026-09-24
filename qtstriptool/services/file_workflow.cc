#include "services/file_workflow.h"

#include <QSaveFile>
#include <algorithm>
#include <sstream>
#include <utility>

namespace striptool {

ConfigResult FileWorkflow::open(const std::filesystem::path& path,
                                StripToolModel& model) {
  StripToolModel candidate = makeDefaultModel();
  ConfigResult result = readConfigurationFile(path, candidate);
  if (result.success) {
    candidate.filename = path.string();
    model = std::move(candidate);
  }
  return result;
}

bool FileWorkflow::save(const std::filesystem::path& path, StripToolModel& model,
                        std::string* error) {
  std::ostringstream content;
  if (!writeConfiguration(content, model, error)) return false;
#ifdef _WIN32
  const QString name = QString::fromStdWString(path.wstring());
#else
  const QString name = QString::fromStdString(path.string());
#endif
  QSaveFile output(name);
  if (!output.open(QIODevice::WriteOnly)) {
    if (error) *error = output.errorString().toStdString();
    return false;
  }
  const std::string serialized = content.str();
  if (output.write(serialized.data(), static_cast<qint64>(serialized.size())) !=
          static_cast<qint64>(serialized.size()) || !output.commit()) {
    if (error) *error = output.errorString().toStdString();
    return false;
  }
  model.filename = path.string();
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
