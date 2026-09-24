#include "core/application.h"
#include "core/config.h"
#include "services/file_workflow.h"
#include "ui/main_window.h"
#include <QApplication>
#include <QDir>
#include <QTextStream>
#include <utility>
int main(int argc, char** argv) {
  // Establish consistent metrics across desktops. QApplication processes any
  // explicit -style option afterward, so users can still select another style.
  striptool::setDefaultApplicationStyle();
  QApplication application(argc, argv);
  striptool::configureApplication(application);
  const QStringList arguments = application.arguments();
  if (arguments.contains(QStringLiteral("--help"))) {
    QTextStream(stdout) << "Usage: qtstriptool [--help] [--version] [configuration.stp]\n";
    return 0;
  }
  if (arguments.contains(QStringLiteral("--version"))) {
    QTextStream(stdout) << striptool::versionText() << '\n';
    return 0;
  }
  QString explicitName;
  for (int i = 1; i < arguments.size(); ++i) {
    if (!arguments[i].startsWith(QLatin1Char('-'))) {
      explicitName = arguments[i];
      break;
    }
  }
  auto model = striptool::makeDefaultModel();
  const auto explicitConfiguration = explicitName.isEmpty()
                                         ? std::filesystem::path{}
                                         : striptool::findConfigurationFile(
                                               explicitName.toStdString(),
                                               QDir::currentPath().toStdString(),
                                               qEnvironmentVariable(
                                                   "STRIP_FILE_SEARCH_PATH")
                                                   .toStdString());
  if (!explicitName.isEmpty() && explicitConfiguration.empty()) {
    QTextStream(stderr) << "qtstriptool: cannot find " << explicitName
                        << "; trying StripTool.stp\n";
  }
  const auto configuration = striptool::findStartupConfiguration(
      explicitName.toStdString(), QDir::currentPath().toStdString(),
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
    const auto fallback = std::filesystem::path(QDir::currentPath().toStdString()) /
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
  striptool::MainWindow window(std::move(model));
  window.startAcquisition();
  window.show();
  if (explicitName.isEmpty()) window.showControls();
  return application.exec();
}
