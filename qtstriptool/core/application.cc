#include "core/application.h"
#include "services/runtime_capabilities.h"
#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
namespace striptool {
QString applicationName() { return QStringLiteral("Qt StripTool"); }
QString applicationVersion() { return QStringLiteral("1.0.0"); }
QString versionText() {
  return QStringLiteral("%1 %2 (Qt %3, EPICS %4)")
      .arg(applicationName(), applicationVersion(), qtVersion(), epicsVersion());
}
QString aboutText() {
  return versionText() + QStringLiteral(
      "\n\nQt StripTool\n"
      "Robert Soliday (APS) - developed and maintains the Qt port\n"
      "\nOriginal StripTool\n"
      "Janet Anderson (APS) - early stripTool program\n"
      "Christopher A. Larrieu (TJNAF) - designed and wrote later StripTool\n"
      "Albert Kagarmanov (DESY) - Y-axis zoom, autoscaling, legend values, logarithmic scaling, and archived-data history\n"
      "Kenneth Evans, Jr. (APS) - merged and maintained the unified version and wrote the user guide\n"
      "\nOther contributors\n"
      "Deb Kirstens - suggestions and code\n"
      "Vladimir Romano - pixmap and bug fixes");
}

void setDefaultApplicationStyle() {
  QApplication::setStyle(QStringLiteral("fusion"));
}
void configureApplication(QApplication& application) {
  QCoreApplication::setApplicationName(QStringLiteral("qtstriptool"));
  QCoreApplication::setApplicationVersion(applicationVersion());
  QCoreApplication::setOrganizationName(QStringLiteral("EPICS"));
  application.setWindowIcon(QIcon(QStringLiteral(":/icons/qtstriptool.svg")));
}
}
