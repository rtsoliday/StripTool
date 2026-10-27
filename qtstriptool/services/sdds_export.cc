#include "services/export_service.h"

#ifdef STRIPTOOL_HAVE_SDDS
#include <SDDS.h>
#include <QFile>
#include <QSaveFile>
#include <QTemporaryFile>
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <set>
#endif

namespace striptool {

bool ExportService::sddsAvailable() {
#ifdef STRIPTOOL_HAVE_SDDS
  return true;
#else
  return false;
#endif
}

#ifdef STRIPTOOL_HAVE_SDDS
namespace {

// Older EPICS installations expose writable string arguments in the C API.
// Pass owned copies so both old and current headers work without const casts.
bool validColumnName(std::string name) {
  char kind[] = "column";
  return SDDS_IsValidName(name.data(), kind);
}

int32_t defineColumn(SDDS_DATASET* dataset, std::string name,
                     std::string units, std::string description) {
  return SDDS_DefineColumn(dataset, name.data(), nullptr, units.data(),
                           description.data(), nullptr, SDDS_DOUBLE, 0);
}

std::string sddsError() {
  int32_t count = 0;
  char** messages = SDDS_GetErrorMessages(&count, SDDS_ALL_GetErrorMessages);
  std::string detail;
  if (messages) {
    for (int32_t i = 0; i < count; ++i) {
      if (!detail.empty()) detail += "; ";
      detail += messages[i];
      std::free(messages[i]);
    }
    std::free(messages);
  }
  SDDS_ClearErrors();
  return detail;
}

// SDDS_Terminate does not report fclose errors. Detach and close the ordinary
// binary stream ourselves, then let SDDS release its layout and page buffers.
struct SddsOutput {
  SDDS_DATASET dataset{};
  bool initialized = false;

  bool close() {
    bool ok = true;
    if (dataset.layout.fp) {
      ok = std::fclose(dataset.layout.fp) == 0;
      dataset.layout.fp = nullptr;
    }
    if (initialized) {
      initialized = false;
      if (!SDDS_Terminate(&dataset)) ok = false;
    }
    return ok;
  }
  ~SddsOutput() { close(); }
};

}  // namespace
#endif

bool ExportService::writeSddsFile(const std::filesystem::path& path,
                                  const StripToolModel& model,
                                  const CurveSamples& samples,
                                  std::string* error) {
#ifndef STRIPTOOL_HAVE_SDDS
  (void)path;
  (void)model;
  (void)samples;
  if (error) *error = "SDDS export is unavailable in this build";
  return false;
#else
  if (error) error->clear();
  SDDS_ClearErrors();
  auto fail = [&](const std::string& message) {
    const auto detail = sddsError();
    if (error) *error = "unable to export " + path.u8string() + ": " + message +
                        (detail.empty() ? "" : ": " + detail);
    return false;
  };
  std::set<std::string> names{"Time"};
  using Clock = std::chrono::system_clock;
  std::vector<Clock::time_point> times;
  for (std::size_t curve = 0; curve < samples.size(); ++curve) {
    const auto& config = model.curves[curve];
    if (!config.nameSet) continue;
    if (!validColumnName(config.name))
      return fail("invalid SDDS column name '" + config.name + "'");
    if (!names.insert(config.name).second)
      return fail("duplicate SDDS column name '" + config.name + "' (Time is reserved)");
    for (const auto& sample : samples[curve]) times.push_back(sample.timestamp);
  }
  std::sort(times.begin(), times.end());
  times.erase(std::unique(times.begin(), times.end()), times.end());

  // Stage an uncompressed binary file, regardless of the destination suffix.
  // QSaveFile commits it only after SDDS has successfully written and closed it,
  // and handles native destination paths, including Unicode on Windows.
  QTemporaryFile temporary;
  if (!temporary.open()) return fail(temporary.errorString().toStdString());
  temporary.close();
#ifdef _WIN32
  QByteArray temporaryName = temporary.fileName().toLocal8Bit();
#else
  QByteArray temporaryName = QFile::encodeName(temporary.fileName());
#endif
  SddsOutput output;
  output.initialized = true; // Also clean up a partially initialized dataset.
  char description[] = "Qt StripTool data dump";
  char contents[] = "StripTool data";
  if (!SDDS_InitializeOutput(&output.dataset, SDDS_BINARY, 1, description,
                              contents, temporaryName.data()))
    return fail("initializing SDDS output");
  auto& dataset = output.dataset;
  if (defineColumn(&dataset, "Time", "seconds", "Unix epoch time") < 0)
    return fail("defining Time column");
  for (const auto& config : model.curves) {
    if (config.nameSet && defineColumn(&dataset, config.name, config.units, config.comment) < 0)
      return fail("defining column '" + config.name + "'");
  }
  if (!SDDS_WriteLayout(&dataset) || !SDDS_StartPage(&dataset, times.size()))
    return fail("writing SDDS layout");
  // Older SDDS versions do not accept a zero-length SetColumn call.
  if (!times.empty()) {
    std::vector<double> values(times.size());
    for (std::size_t row = 0; row < times.size(); ++row)
      values[row] = std::chrono::duration<double>(times[row].time_since_epoch()).count();
    if (!SDDS_SetColumn(&dataset, SDDS_SET_BY_INDEX, values.data(), times.size(), int32_t{0}))
      return fail("writing Time column");
    int32_t column = 1;
    for (std::size_t curve = 0; curve < samples.size(); ++curve) {
      if (!model.curves[curve].nameSet) continue;
      std::fill(values.begin(), values.end(), std::numeric_limits<double>::quiet_NaN());
      // Iterating in supplied order makes the last duplicate timestamp win.
      for (const auto& sample : samples[curve]) {
        const auto row = std::lower_bound(times.begin(), times.end(), sample.timestamp) - times.begin();
        values[row] = sample.plotable ? sample.value : std::numeric_limits<double>::quiet_NaN();
      }
      if (!SDDS_SetColumn(&dataset, SDDS_SET_BY_INDEX, values.data(), times.size(), column++))
        return fail("writing column '" + model.curves[curve].name + "'");
    }
  }
  if (!SDDS_WritePage(&dataset)) return fail("writing SDDS page");
  if (!output.close()) return fail("closing SDDS output");

#ifdef _WIN32
  const QString destination = QString::fromStdWString(path.wstring());
#else
  const QString destination = QString::fromStdString(path.native());
#endif
  QSaveFile file(destination);
  if (!temporary.open() || !file.open(QIODevice::WriteOnly))
    return fail(!temporary.isOpen() ? temporary.errorString().toStdString()
                                    : file.errorString().toStdString());
  while (!temporary.atEnd()) {
    const QByteArray block = temporary.read(64 * 1024);
    if (block.isEmpty() || file.write(block) != block.size())
      return fail("copying SDDS output: " + (temporary.error() != QFile::NoError
          ? temporary.errorString() : file.errorString()).toStdString());
  }
  if (!file.commit()) return fail(file.errorString().toStdString());
  return true;
#endif
}

}  // namespace striptool
