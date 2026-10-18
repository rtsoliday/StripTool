#include "services/export_service.h"

#include <chrono>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <ostream>
#include <sstream>
#ifdef _WIN32
#include <QDateTime>
#endif

namespace striptool {
namespace {

std::string timestamp(const std::chrono::system_clock::time_point& value) {
#ifdef _WIN32
  // The Windows CRT calendar conversion cannot cover system_clock's full
  // range. Qt can, including dates before the Unix epoch and beyond year 9999.
  const auto milliseconds = std::chrono::floor<std::chrono::milliseconds>(
      value.time_since_epoch()).count();
  return QDateTime::fromMSecsSinceEpoch(milliseconds).toUTC()
      .toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss.zzz'Z'")).toStdString();
#else
  const auto wholeSeconds = std::chrono::floor<std::chrono::seconds>(
      value.time_since_epoch());
  // Converting the floored second back to a time_point can underflow at the
  // clock minimum. Supported system clocks and time_t use the Unix epoch.
  const auto seconds = static_cast<std::time_t>(wholeSeconds.count());
  auto milliseconds = std::chrono::floor<std::chrono::milliseconds>(
      value.time_since_epoch()).count() % 1000;
  if (milliseconds < 0) milliseconds += 1000;
  std::tm utc{};
  gmtime_r(&seconds, &utc);
  std::ostringstream result;
  result << std::put_time(&utc, "%Y-%m-%dT%H:%M:%S") << '.'
         << std::setfill('0') << std::setw(3) << milliseconds << 'Z';
  return result.str();
#endif
}

std::string csvQuote(const std::string& value) {
  std::string escaped;
  for (char character : value) {
    escaped += character;
    if (character == '"') escaped += '"';
  }
  return '"' + escaped + '"';
}

// EPICS INVALID_ALARM: no valid data.
constexpr std::uint16_t kInvalidSeverity = 3;

// Writes the value, status and severity fields. A non-plotable sample marks a
// disconnect, CA error or lost updates; its stored fields are not a reading,
// so the row says nan with INVALID severity.
void writeReading(std::ostream& output, const Sample& sample, char separator) {
  if (!sample.plotable) {
    output << "nan" << separator << 0 << separator << kInvalidSeverity;
    return;
  }
  output << sample.value << separator << sample.status << separator
         << sample.severity;
}

template <typename Writer>
bool writeFile(const std::filesystem::path& path, Writer writer,
               std::string* error) {
  std::ofstream output(path);
  if (!output) {
    if (error) *error = "unable to open " + path.u8string();
    return false;
  }
  // The last buffered block is written by close(); a full disk can fail there.
  const bool written = writer(output);
  output.close();
  if (!written || !output) {
    if (error) *error = "unable to write " + path.u8string();
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
      output << timestamp(sample.timestamp) << ' ' << model.curves[curve].name
             << ' ';
      writeReading(output, sample, ' ');
      output << '\n';
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
      output << csvQuote(timestamp(sample.timestamp)) << ','
             << csvQuote(model.curves[curve].name) << ',';
      writeReading(output, sample, ',');
      output << '\n';
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
