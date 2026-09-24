#pragma once
#include <QString>
class QApplication;
namespace striptool {
QString applicationName();
QString applicationVersion();
QString versionText();
void setDefaultApplicationStyle();
void configureApplication(QApplication& application);
}
