#include "core/config.h"

#include <algorithm>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>

namespace striptool {
namespace {

std::string trim(const std::string& value) {
  const auto first = value.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return {};
  const auto last = value.find_last_not_of(" \t\r\n");
  return value.substr(first, last - first + 1);
}

std::pair<std::string, std::string> splitLine(const std::string& line) {
  const auto separator = line.find_first_of(" \t");
  if (separator == std::string::npos) return {trim(line), {}};
  return {line.substr(0, separator), trim(line.substr(separator + 1))};
}

template <typename Integer>
bool parseInteger(const std::string& text, Integer& value) {
  const std::string clean = trim(text);
  if (clean.empty()) return false;
  const char* begin = clean.data();
  const char* end = begin + clean.size();
  const auto result = std::from_chars(begin, end, value);
  return result.ec == std::errc{} && result.ptr == end;
}

bool parseDouble(const std::string& text, double& value) {
  const std::string clean = trim(text);
  if (clean.empty()) return false;
  char* end = nullptr;
  errno = 0;
  value = std::strtod(clean.c_str(), &end);
  return errno != ERANGE && end == clean.c_str() + clean.size() &&
         std::isfinite(value);
}

bool parseBooleanInteger(const std::string& text, bool& value) {
  int parsed = 0;
  if (!parseInteger(text, parsed)) return false;
  value = parsed != 0;
  return true;
}

ConfigResult fail(std::size_t line, const std::string& message,
                  bool legacy = false) {
  return {false, legacy, {{line, message}}};
}

bool checkLength(const std::string& value, std::size_t maximum) {
  return value.size() <= maximum;
}

std::vector<std::string> components(const std::string& key) {
  std::vector<std::string> result;
  std::size_t begin = 0;
  while (begin <= key.size()) {
    const auto end = key.find('.', begin);
    result.push_back(key.substr(begin, end - begin));
    if (end == std::string::npos) break;
    begin = end + 1;
  }
  return result;
}

ConfigResult parseLegacy(const std::vector<std::string>& lines,
                         StripToolModel& candidate) {
  int curveIndex = -1;
  bool recognized = false;
  for (std::size_t i = 0; i < lines.size(); ++i) {
    const auto [key, value] = splitLine(lines[i]);
    if (key.empty()) continue;
    if (key == "SAMPLEFREQUENCY") {
      double parsed = 0;
      if (!parseDouble(value, parsed))
        return fail(i + 1, "invalid SAMPLEFREQUENCY", true);
      candidate.timing.sampleIntervalSeconds = std::max(parsed, 0.01);
      candidate.timing.refreshIntervalSeconds = std::max(parsed, 0.1);
      recognized = true;
    } else if (key == "TIMESPAN") {
      unsigned parsed = 0;
      if (!parseInteger(value, parsed))
        return fail(i + 1, "invalid TIMESPAN", true);
      candidate.timing.timespanSeconds = std::max(parsed, 1U);
      recognized = true;
    } else if (key == "CHANNEL") {
      ++curveIndex;
      if (curveIndex >= static_cast<int>(kMaximumCurves))
        return fail(i + 1, "configuration exceeds the ten-curve limit", true);
      const std::string name = trim(value);
      if (name.empty() || name.find_first_of(" \t") != std::string::npos ||
          !checkLength(name, kMaximumCurveNameLength))
        return fail(i + 1, "invalid CHANNEL name", true);
      auto& curve = candidate.curves[static_cast<std::size_t>(curveIndex)];
      curve.name = name;
      curve.nameSet = true;
      recognized = true;
    } else if (key == "MINIMUM" || key == "MAXIMUM") {
      double parsed = 0;
      if (curveIndex < 0 || !parseDouble(value, parsed))
        return fail(i + 1, "curve limit appears before a CHANNEL", true);
      auto& curve = candidate.curves[static_cast<std::size_t>(curveIndex)];
      if (key == "MINIMUM") {
        curve.minimum = parsed;
        curve.minimumSet = true;
      } else {
        curve.maximum = parsed;
        curve.maximumSet = true;
      }
      recognized = true;
    }
  }
  if (!recognized) return fail(1, "not a recognized StripTool configuration", true);
  candidate.timing.numberOfSamples = static_cast<int>(std::clamp(
      std::ceil(candidate.timing.timespanSeconds /
                candidate.timing.sampleIntervalSeconds), 1.0, 65536.0));
  return {true, true, {}};
}

ConfigResult parseCurrent(const std::vector<std::string>& lines,
                          StripToolModel& candidate) {
  bool numberOfSamplesSet = false;
  for (std::size_t i = 1; i < lines.size(); ++i) {
    const auto [key, value] = splitLine(lines[i]);
    if (key.empty()) continue;
    const auto parts = components(key);
    if (parts.empty() || parts[0] != "Strip") {
      candidate.unknownFields.push_back({key, value});
      continue;
    }

    bool known = true;
    bool valid = true;
    if (parts.size() == 3 && parts[1] == "Time") {
      if (parts[2] == "Timespan") {
        unsigned parsed = 0;
        valid = parseInteger(value, parsed);
        if (valid) candidate.timing.timespanSeconds = std::max(parsed, 1U);
      } else if (parts[2] == "NumSamples") {
        int parsed = 0;
        valid = parseInteger(value, parsed);
        if (valid) {
          candidate.timing.numberOfSamples = std::clamp(parsed, 1, 65536);
          numberOfSamplesSet = true;
        }
      } else if (parts[2] == "SampleInterval") {
        double parsed = 0;
        valid = parseDouble(value, parsed);
        if (valid) candidate.timing.sampleIntervalSeconds = std::max(parsed, 0.01);
      } else if (parts[2] == "RefreshInterval") {
        double parsed = 0;
        valid = parseDouble(value, parsed);
        if (valid) candidate.timing.refreshIntervalSeconds = std::max(parsed, 0.1);
      } else {
        known = false;
      }
    } else if (parts.size() == 3 && parts[1] == "Color") {
      Rgba16* color = nullptr;
      if (parts[2] == "Background") color = &candidate.colors.background;
      else if (parts[2] == "Foreground") color = &candidate.colors.foreground;
      else if (parts[2] == "Grid") color = &candidate.colors.grid;
      else if (parts[2].rfind("Color", 0) == 0) {
        int index = 0;
        if (parseInteger(parts[2].substr(5), index) && index >= 1 && index <= 10)
          color = &candidate.colors.curves[static_cast<std::size_t>(index - 1)];
      }
      if (color) {
        std::istringstream stream(value);
        unsigned red = 0, green = 0, blue = 0;
        std::string extra;
        valid = static_cast<bool>(stream >> red >> green >> blue) &&
                !(stream >> extra) && red <= 65535 && green <= 65535 &&
                blue <= 65535;
        if (valid) *color = {static_cast<std::uint16_t>(red),
                             static_cast<std::uint16_t>(green),
                             static_cast<std::uint16_t>(blue), 65535};
      } else {
        known = false;
      }
    } else if (parts.size() == 3 && parts[1] == "Option") {
      int parsed = 0;
      if (parts[2] == "GridXon" || parts[2] == "GridYon") {
        valid = parseInteger(value, parsed);
        if (valid) {
          auto mode = parsed >= 0 && parsed <= 2 ? static_cast<GridMode>(parsed)
                                                 : GridMode::Some;
          if (parts[2] == "GridXon") candidate.graph.xGrid = mode;
          else candidate.graph.yGrid = mode;
        }
      } else if (parts[2] == "AxisYcolorStat") {
        valid = parseBooleanInteger(value, candidate.graph.coloredYAxis);
      } else if (parts[2] == "GraphLineWidth") {
        valid = parseInteger(value, parsed);
        if (valid) candidate.graph.lineWidth = std::clamp(parsed, 0, 10);
      } else {
        known = false;
      }
    } else if (parts.size() == 4 && parts[1] == "Curve") {
      int index = -1;
      if (!parseInteger(parts[2], index) || index < 0 || index >= 10)
        return fail(i + 1, "curve index must be between 0 and 9");
      auto& curve = candidate.curves[static_cast<std::size_t>(index)];
      if (parts[3] == "Name" || parts[3] == "Units") {
        const std::string word = trim(value);
        valid = !word.empty() && word.find_first_of(" \t") == std::string::npos;
        if (parts[3] == "Name") {
          valid = valid && checkLength(word, kMaximumCurveNameLength);
          if (valid) { curve.name = word; curve.nameSet = true; }
        } else {
          valid = valid && checkLength(word, kMaximumUnitsLength);
          if (valid) { curve.units = word; curve.unitsSet = true; }
        }
      } else if (parts[3] == "Comment") {
        valid = checkLength(value, kMaximumCommentLength);
        if (valid) { curve.comment = value; curve.commentSet = true; }
      } else if (parts[3] == "Precision") {
        int parsed = 0;
        valid = parseInteger(value, parsed) && parsed >= 0 && parsed <= 20;
        if (valid) { curve.precision = parsed; curve.precisionSet = true; }
      } else if (parts[3] == "Min" || parts[3] == "Max") {
        double parsed = 0;
        valid = parseDouble(value, parsed);
        if (valid && parts[3] == "Min") { curve.minimum = parsed; curve.minimumSet = true; }
        if (valid && parts[3] == "Max") { curve.maximum = parsed; curve.maximumSet = true; }
      } else if (parts[3] == "Scale") {
        int parsed = 0;
        valid = parseInteger(value, parsed) && (parsed == 0 || parsed == 1);
        if (valid) curve.scale = static_cast<ScaleMode>(parsed);
      } else if (parts[3] == "PlotStatus") {
        valid = parseBooleanInteger(value, curve.plotted);
      } else {
        known = false;
      }
    } else {
      known = false;
    }

    if (!known) candidate.unknownFields.push_back({key, value});
    else if (!valid) return fail(i + 1, "invalid value for " + key);
  }

  if (!numberOfSamplesSet) {
    const double samples = std::ceil(candidate.timing.timespanSeconds /
                                     candidate.timing.sampleIntervalSeconds);
    candidate.timing.numberOfSamples = static_cast<int>(std::clamp(samples, 1.0, 65536.0));
  }
  return {true, false, {}};
}

void writeField(std::ostream& output, const std::string& key,
                const std::string& value) {
  output << std::left << std::setw(30) << key << value << '\n';
}

std::string number(double value, int precision = 15) {
  std::ostringstream stream;
  stream << std::setprecision(precision) << value;
  return stream.str();
}

}  // namespace

ConfigResult readConfiguration(std::istream& input, StripToolModel& model) {
  std::vector<std::string> lines;
  std::string line;
  while (std::getline(input, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    lines.push_back(line);
  }
  if (input.bad()) return fail(0, "unable to read configuration");
  if (lines.empty()) return fail(0, "configuration is empty");

  StripToolModel candidate = model;
  const auto [header, versionText] = splitLine(lines.front());
  ConfigResult result;
  if (header != "StripConfig") {
    result = parseLegacy(lines, candidate);
  } else {
    int major = 0, minor = 0;
    char dot = 0;
    std::istringstream version(versionText);
    std::string extra;
    if (!(version >> major >> dot >> minor) || dot != '.' || (version >> extra))
      return fail(1, "invalid StripConfig version header");
    if (major < 0 || minor < 0 || major > 1 || (major == 1 && minor > 2))
      return fail(1, "configuration version is newer than supported 1.2");
    result = parseCurrent(lines, candidate);
  }
  if (result.success) model = std::move(candidate);
  return result;
}

ConfigResult readConfigurationFile(const std::filesystem::path& path,
                                   StripToolModel& model) {
  std::ifstream input(path);
  if (!input) return fail(0, "unable to open " + path.string());
  ConfigResult result = readConfiguration(input, model);
  if (result.success) {
    model.filename = path.string();
    model.title = path.filename().string();
  }
  return result;
}

bool writeConfiguration(std::ostream& output, const StripToolModel& model,
                        std::string* error) {
  if (model.timing.timespanSeconds < 1 || model.timing.numberOfSamples < 1 ||
      model.timing.numberOfSamples > 65536 ||
      !std::isfinite(model.timing.sampleIntervalSeconds) ||
      model.timing.sampleIntervalSeconds < 0.01 ||
      !std::isfinite(model.timing.refreshIntervalSeconds) ||
      model.timing.refreshIntervalSeconds < 0.1 || model.graph.lineWidth < 0 ||
      model.graph.lineWidth > 10) {
    if (error) *error = "model contains invalid timing or graph values";
    return false;
  }
  for (const auto& curve : model.curves) {
    if ((curve.nameSet && (curve.name.empty() ||
                           curve.name.find_first_of(" \t\r\n") != std::string::npos ||
                           !checkLength(curve.name, kMaximumCurveNameLength))) ||
        (curve.unitsSet &&
         (curve.units.empty() ||
          curve.units.find_first_of(" \t\r\n") != std::string::npos ||
          !checkLength(curve.units, kMaximumUnitsLength))) ||
        (curve.commentSet && !checkLength(curve.comment, kMaximumCommentLength)) ||
        curve.precision < 0 || curve.precision > 20 ||
        !std::isfinite(curve.minimum) || !std::isfinite(curve.maximum)) {
      if (error) *error = "model contains invalid curve values";
      return false;
    }
  }
  writeField(output, "StripConfig", "1.2");
  for (const auto& field : model.unknownFields) writeField(output, field.key, field.value);
  writeField(output, "Strip.Time.Timespan", std::to_string(model.timing.timespanSeconds));
  writeField(output, "Strip.Time.NumSamples", std::to_string(model.timing.numberOfSamples));
  writeField(output, "Strip.Time.SampleInterval", number(model.timing.sampleIntervalSeconds));
  writeField(output, "Strip.Time.RefreshInterval", number(model.timing.refreshIntervalSeconds));

  const auto writeColor = [&output](const std::string& key, const Rgba16& color) {
    writeField(output, key, std::to_string(color.red) + " " +
                            std::to_string(color.green) + " " +
                            std::to_string(color.blue));
  };
  writeColor("Strip.Color.Background", model.colors.background);
  writeColor("Strip.Color.Foreground", model.colors.foreground);
  writeColor("Strip.Color.Grid", model.colors.grid);
  for (std::size_t i = 0; i < model.colors.curves.size(); ++i)
    writeColor("Strip.Color.Color" + std::to_string(i + 1), model.colors.curves[i]);
  writeField(output, "Strip.Option.GridXon", std::to_string(static_cast<int>(model.graph.xGrid)));
  writeField(output, "Strip.Option.GridYon", std::to_string(static_cast<int>(model.graph.yGrid)));
  writeField(output, "Strip.Option.AxisYcolorStat", model.graph.coloredYAxis ? "1" : "0");
  writeField(output, "Strip.Option.GraphLineWidth", std::to_string(model.graph.lineWidth));

  for (std::size_t i = 0; i < model.curves.size(); ++i) {
    const auto& curve = model.curves[i];
    if (!curve.nameSet) continue;
    const std::string prefix = "Strip.Curve." + std::to_string(i) + ".";
    writeField(output, prefix + "Name", curve.name);
    if (curve.unitsSet) writeField(output, prefix + "Units", curve.units);
    if (curve.commentSet) writeField(output, prefix + "Comment", curve.comment);
    if (curve.precisionSet) writeField(output, prefix + "Precision", std::to_string(curve.precision));
    if (curve.minimumSet) writeField(output, prefix + "Min", number(curve.minimum));
    if (curve.maximumSet) writeField(output, prefix + "Max", number(curve.maximum));
    writeField(output, prefix + "Scale", std::to_string(static_cast<int>(curve.scale)));
    writeField(output, prefix + "PlotStatus", curve.plotted ? "1" : "0");
  }
  if (!output) {
    if (error) *error = "unable to write configuration";
    return false;
  }
  return true;
}

bool writeConfigurationFile(const std::filesystem::path& path,
                            const StripToolModel& model, std::string* error) {
  std::ostringstream serialized;
  if (!writeConfiguration(serialized, model, error)) return false;
  std::ofstream output(path);
  if (!output) {
    if (error) *error = "unable to open " + path.string();
    return false;
  }
  output << serialized.str();
  if (!output) {
    if (error) *error = "unable to write " + path.string();
    return false;
  }
  return true;
}

ConfigResult loadConfigurationLayers(
    const std::vector<std::filesystem::path>& paths, StripToolModel& model) {
  StripToolModel candidate = model;
  ConfigResult combined{true, false, {}};
  for (const auto& path : paths) {
    std::error_code error;
    if (!std::filesystem::is_regular_file(path, error)) continue;
    ConfigResult result = readConfigurationFile(path, candidate);
    if (!result.success) return result;
    combined.legacyFormat = combined.legacyFormat || result.legacyFormat;
    combined.diagnostics.insert(combined.diagnostics.end(), result.diagnostics.begin(),
                                result.diagnostics.end());
  }
  model = std::move(candidate);
  return combined;
}

std::filesystem::path findConfigurationFile(
    const std::filesystem::path& name,
    const std::filesystem::path& workingDirectory,
    const std::string& searchPath) {
  const auto exists = [](const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
  };
  if (name.is_absolute() || name.has_parent_path()) return exists(name) ? name : std::filesystem::path{};
  const auto local = workingDirectory / name;
  if (exists(local)) return local;
#ifdef _WIN32
  constexpr char separator = ';';
#else
  constexpr char separator = ':';
#endif
  std::istringstream paths(searchPath);
  std::string directory;
  while (std::getline(paths, directory, separator)) {
    if (directory.empty()) continue;
    const auto candidate = std::filesystem::path(directory) / name;
    if (exists(candidate)) return candidate;
  }
  return {};
}

std::filesystem::path findStartupConfiguration(
    const std::string& explicitName,
    const std::filesystem::path& workingDirectory,
    const std::string& searchPath) {
  if (!explicitName.empty())
    if (const auto explicitFile =
            findConfigurationFile(explicitName, workingDirectory, searchPath);
        !explicitFile.empty())
      return explicitFile;
  const auto defaultFile = workingDirectory / "StripTool.stp";
  std::error_code error;
  return std::filesystem::is_regular_file(defaultFile, error)
             ? defaultFile
             : std::filesystem::path{};
}

}  // namespace striptool
