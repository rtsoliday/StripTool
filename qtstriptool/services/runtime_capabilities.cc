#include "services/runtime_capabilities.h"
#include <QtGlobal>
#include <epicsVersion.h>
namespace striptool {
QString qtVersion() { return QString::fromLatin1(qVersion()); }
QString epicsVersion() {
  QString version = QString::fromLatin1(EPICS_VERSION_STRING);
  if (version.startsWith(QStringLiteral("EPICS "))) {
    version.remove(0, 6);
  }
  return version;
}
}
