#include "services/export_service.h"

#include <chrono>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <sstream>

namespace striptool {
namespace {

std::string timestamp(const std::chrono::system_clock::time_point& value) {
  const auto seconds = std::chrono::system_clock::to_time_t(value);
  const auto milliseconds = std::chrono::duration_cast<std::chrono::milliseconds>(
      value.time_since_epoch()).count() % 1000;
  std::tm utc{};
#ifdef _WIN32
  gmtime_s(&utc, &seconds);
#else
  gmtime_r(&seconds, &utc);
#endif
  std::ostringstream result;
  result << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.'
         << std::setfill('0') << std::setw(3) << milliseconds << 'Z';
  return result.str();
}

std::string csvQuote(const std::string& value) {
  std::string escaped;
  for (char character : value) {
    escaped += character;
    if (character == '"') escaped += '"';
  }
  return '"' + escaped + '"';
}

template <typename Writer>
bool writeFile(const std::filesystem::path& path, Writer writer,
               std::string* error) {
  std::ofstream output(path);
  if (!output) {
    if (error) *error = "unable to open " + path.string();
    return false;
  }
  if (!writer(output)) {
    if (error) *error = "unable to write " + path.string();
    return false;
  }
  return true;
}

}  // namespace

bool ExportService::writeText(std::ostream& output, const StripToolModel& model,
                              const CurveSamples& samples) {
  output << "# Qt StripTool data dump\n";
  output << "# timestamp curve value status severity\n";
  output << std::setprecision(17);
  for (std::size_t curve = 0; curve < samples.size(); ++curve) {
    if (!model.curves[curve].nameSet) continue;
    for (const auto& sample : samples[curve]) {
      if (!sample.plotable) continue;
      output << timestamp(sample.timestamp) << ' ' << model.curves[curve].name
             << ' ' << sample.value << ' ' << sample.status << ' '
             << sample.severity << '\n';
    }
  }
  return bool(output);
}

bool ExportService::writeCsv(std::ostream& output, const StripToolModel& model,
                             const CurveSamples& samples) {
  output << "timestamp,curve,value,status,severity\n";
  output << std::setprecision(17);
  for (std::size_t curve = 0; curve < samples.size(); ++curve) {
    if (!model.curves[curve].nameSet) continue;
    for (const auto& sample : samples[curve]) {
      if (!sample.plotable) continue;
      output << csvQuote(timestamp(sample.timestamp)) << ','
             << csvQuote(model.curves[curve].name) << ',' << sample.value << ','
             << sample.status << ',' << sample.severity << '\n';
    }
  }
  return bool(output);
}

bool ExportService::writeTextFile(const std::filesystem::path& path,
                                  const StripToolModel& model,
                                  const CurveSamples& samples,
                                  std::string* error) {
  return writeFile(path, [&](std::ostream& out) { return writeText(out, model, samples); },
                   error);
}

bool ExportService::writeCsvFile(const std::filesystem::path& path,
                                 const StripToolModel& model,
                                 const CurveSamples& samples,
                                 std::string* error) {
  return writeFile(path, [&](std::ostream& out) { return writeCsv(out, model, samples); },
                   error);
}

}  // namespace striptool
