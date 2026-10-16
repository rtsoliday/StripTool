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
  return QStringLiteral(
      "<p>Live EPICS trends, archived data, and graph annotations.<br>"
      "Developed at Argonne National Laboratory</p>"
      "<p><b>Qt port development and maintenance</b><br>"
      "Robert Soliday (APS) — <a href=\"mailto:soliday@anl.gov\">soliday@anl.gov</a></p>"
      "<p><b>Original StripTool</b><br>"
      "Janet Anderson (APS) — early stripTool program<br>"
      "Christopher A. Larrieu (TJNAF) — later StripTool design and implementation<br>"
      "Albert Kagarmanov (DESY) — Y-axis zoom, autoscaling, legend values, "
      "logarithmic scaling, and archived-data history<br>"
      "Kenneth Evans, Jr. (APS) — unified version, maintenance, and user guide</p>"
      "<p><b>Other contributors</b><br>"
      "Deb Kirstens — suggestions and code<br>"
      "Vladimir Romano — pixmap and bug fixes</p>");
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
