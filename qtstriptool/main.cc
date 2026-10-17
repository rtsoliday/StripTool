#include "core/application.h"
#include "core/command_line.h"
#include "core/config.h"
#include "services/file_workflow.h"
#include "ui/main_window.h"
#include <QApplication>
#include <QDir>
#include <QTextStream>
#include <QTimer>
#include <utility>
int main(int argc, char** argv) {
  // Establish consistent metrics across desktops. QApplication processes any
  // explicit -style option afterward, so users can still select another style.
  striptool::setDefaultApplicationStyle();
  QApplication application(argc, argv);
  striptool::configureApplication(application);
  const QStringList arguments = application.arguments();
  QCommandLineParser parser;
  striptool::configureCommandLineParser(parser);
  if (!parser.parse(arguments)) {
    QTextStream(stderr) << "qtstriptool: " << parser.errorText() << '\n';
    return 1;
  }
  if (parser.isSet(QStringLiteral("help-all"))) parser.process(application);
  if (parser.isSet(QStringLiteral("help"))) {
    QTextStream(stdout) << parser.helpText();
    return 0;
  }
  if (parser.isSet(QStringLiteral("version"))) {
    QTextStream(stdout) << striptool::versionText() << '\n';
    return 0;
  }
  if (parser.positionalArguments().size() > 1) {
    QTextStream(stderr) << "qtstriptool: specify at most one configuration file\n";
    return 1;
  }
  const QString explicitName = parser.positionalArguments().value(0);
  const auto workingDirectory = std::filesystem::u8path(QDir::currentPath().toStdString());
  auto model = striptool::makeDefaultModel();
  const auto explicitConfiguration = explicitName.isEmpty()
                                         ? std::filesystem::path{}
                                         : striptool::findConfigurationFile(
                                               std::filesystem::u8path(explicitName.toStdString()),
                                               workingDirectory,
                                               qEnvironmentVariable(
                                                   "STRIP_FILE_SEARCH_PATH")
                                                   .toStdString());
  if (!explicitName.isEmpty() && explicitConfiguration.empty()) {
    QTextStream(stderr) << "qtstriptool: cannot find " << explicitName
                        << "; trying StripTool.stp\n";
  }
  const auto configuration = striptool::findStartupConfiguration(
      explicitName.toStdString(), workingDirectory,
      qEnvironmentVariable("STRIP_FILE_SEARCH_PATH").toStdString());
  bool loaded = false;
  if (!configuration.empty()) {
    const auto result = striptool::FileWorkflow::open(configuration, model);
    loaded = result.success;
    if (!loaded) {
      QTextStream(stderr) << "qtstriptool: "
                          << QString::fromStdString(result.diagnostics.front().message)
                          << (explicitName.isEmpty() ? "; using defaults\n"
                                                     : "; trying StripTool.stp\n");
    }
  }
  if (!loaded && !explicitName.isEmpty()) {
    const auto fallback = workingDirectory /
                          "StripTool.stp";
    std::error_code checkError;
    const bool sameFile = !configuration.empty() &&
        std::filesystem::absolute(configuration).lexically_normal() == fallback;
    if (!sameFile && std::filesystem::is_regular_file(fallback, checkError)) {
      const auto result = striptool::FileWorkflow::open(fallback, model);
      if (!result.success)
        QTextStream(stderr) << "qtstriptool: "
                            << QString::fromStdString(result.diagnostics.front().message)
                            << "; using defaults\n";
    }
  }
  QString timingError;
  if (!striptool::applyCommandLineTiming(parser, model, &timingError)) {
    QTextStream(stderr) << "qtstriptool: " << timingError << '\n';
    return 1;
  }
  striptool::MainWindow window(std::move(model));
  window.startAcquisition();
  window.show();
  if (explicitName.isEmpty()) window.showControls();
  bool validExitDelay = false;
  const int exitDelay = qEnvironmentVariableIntValue(
      "QTSTRIPTOOL_TEST_EXIT_AFTER_MS", &validExitDelay);
  if (validExitDelay && exitDelay > 0)
    QTimer::singleShot(exitDelay, &application, &QCoreApplication::quit);
  return application.exec();
}
