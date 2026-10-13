#include "core/command_line.h"

#include <limits>

namespace striptool {

void configureCommandLineParser(QCommandLineParser& parser) {
  parser.setApplicationDescription(QStringLiteral("Plot live and archived EPICS values."));
  parser.setSingleDashWordOptionMode(QCommandLineParser::ParseAsLongOptions);
  parser.addHelpOption();
  parser.addVersionOption();
  parser.addOption({QStringLiteral("history"),
                    QStringLiteral("Startup history and time window in seconds (1–2147483647)."),
                    QStringLiteral("seconds")});
  parser.addOption({QStringLiteral("sampleCount"),
                    QStringLiteral("Maximum live samples per curve (1–65536)."),
                    QStringLiteral("count")});
  parser.addPositionalArgument(QStringLiteral("configuration.stp"),
                              QStringLiteral("Configuration file to open."),
                              QStringLiteral("[configuration.stp]"));
}

bool applyCommandLineTiming(const QCommandLineParser& parser,
                           StripToolModel& model, QString* error) {
  auto timing = model.timing;
  for (const QString& option : {QStringLiteral("history"), QStringLiteral("sampleCount")}) {
    if (!parser.isSet(option)) continue;
    bool valid = false;
    const qlonglong value = parser.value(option).toLongLong(&valid);
    const int maximum = option == QStringLiteral("history")
                            ? std::numeric_limits<int>::max() : 65536;
    if (!valid || value < 1 || value > maximum) {
      if (error) *error = QStringLiteral("-%1 must be an integer from 1 to %2.")
                             .arg(option).arg(maximum);
      return false;
    }
    if (option == QStringLiteral("history"))
      timing.timespanSeconds = static_cast<unsigned>(value);
    else
      timing.numberOfSamples = static_cast<int>(value);
  }
  model.timing = timing;
  return true;
}

}  // namespace striptool
