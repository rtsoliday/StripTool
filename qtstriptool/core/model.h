#pragma once

#include <array>
#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace striptool {

constexpr std::size_t kMaximumCurves = 10;
constexpr std::size_t kMaximumCurveNameLength = 63;
constexpr std::size_t kMaximumUnitsLength = 31;
constexpr std::size_t kMaximumCommentLength = 255;

enum class ScaleMode { Linear = 0, Log10 = 1 };
enum class GridMode { None = 0, Some = 1, All = 2 };
enum class ConnectionState { Disconnected, Connecting, Connected, Stale, Error };

struct Rgba16 {
  std::uint16_t red = 0;
  std::uint16_t green = 0;
  std::uint16_t blue = 0;
  std::uint16_t alpha = 65535;

  bool operator==(const Rgba16& other) const;
};

struct TimeRange {
  std::chrono::system_clock::time_point start;
  std::chrono::system_clock::time_point end;

  bool isValid() const { return start <= end; }
};

struct Annotation {
  std::chrono::system_clock::time_point time;
  std::optional<double> value;
  std::string text;
  std::optional<std::size_t> curveIndex;

  Annotation() = default;
  Annotation(std::chrono::system_clock::time_point when,
             std::optional<double> atValue, std::string label,
             std::optional<std::size_t> curve = std::nullopt)
      : time(when), value(atValue), text(std::move(label)), curveIndex(curve) {}
};

struct ChannelMetadata {
  ConnectionState connection = ConnectionState::Disconnected;
  std::optional<std::chrono::system_clock::time_point> lastUpdate;
  std::string units;
  std::string description;
  int precision = 0;
  std::optional<double> displayMinimum;
  std::optional<double> displayMaximum;
  std::string statusMessage;
};

struct CurveConfiguration {
  std::string name;
  std::string units = "Undefined";
  std::string comment;
  int precision = 4;
  double minimum = 1e-7;
  double maximum = 1e7;
  ScaleMode scale = ScaleMode::Linear;
  bool plotted = true;

  // The legacy writer omits these fields unless they occurred in input or
  // were explicitly edited. Name presence determines whether a slot is active.
  bool nameSet = false;
  bool unitsSet = false;
  bool commentSet = false;
  bool precisionSet = false;
  bool minimumSet = false;
  bool maximumSet = false;

  // Provider metadata should be written to an .stp file like legacy
  // StripTool metadata, but must remain distinguishable from user-configured
  // values while the application is running (notably for auto scaling).
  bool unitsDiscovered = false;
  bool commentDiscovered = false;
  bool precisionDiscovered = false;
  bool minimumDiscovered = false;
  bool maximumDiscovered = false;
};

struct TimingConfiguration {
  unsigned timespanSeconds = 300;
  int numberOfSamples = 7200;
  double sampleIntervalSeconds = 1.0;
  double refreshIntervalSeconds = 1.0;
};

struct GraphConfiguration {
  GridMode xGrid = GridMode::Some;
  GridMode yGrid = GridMode::Some;
  bool coloredYAxis = true;
  int lineWidth = 2;
};

struct ColorConfiguration {
  Rgba16 background{65535, 65535, 65535, 65535};
  Rgba16 foreground{0, 0, 0, 65535};
  Rgba16 grid{49151, 49151, 49151, 65535};
  std::array<Rgba16, kMaximumCurves> curves;
};

struct UnknownConfigField {
  std::string key;
  std::string value;
};

struct StripToolModel {
  std::string title = "Untitled";
  std::string filename;
  TimingConfiguration timing;
  GraphConfiguration graph;
  ColorConfiguration colors;
  std::array<CurveConfiguration, kMaximumCurves> curves;
  std::vector<Annotation> annotations;
  std::vector<UnknownConfigField> unknownFields;
};

StripToolModel makeDefaultModel();

}  // namespace striptool
