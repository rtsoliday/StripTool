#include "core/application.h"
#include "services/runtime_capabilities.h"
#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
namespace striptool {
QString applicationName() { return QStringLiteral("Qt StripTool"); }
QString applicationVersion() { return QStringLiteral("0.1.0-dev"); }
QString versionText() {
  return QStringLiteral("%1 %2 (Qt %3, EPICS %4)")
      .arg(applicationName(), applicationVersion(), qtVersion(), epicsVersion());
}
void configureApplication(QApplication& application) {
  QCoreApplication::setApplicationName(QStringLiteral("qtstriptool"));
  QCoreApplication::setApplicationVersion(applicationVersion());
  QCoreApplication::setOrganizationName(QStringLiteral("EPICS"));
  application.setWindowIcon(QIcon(QStringLiteral(":/icons/qtstriptool.svg")));
}
}
