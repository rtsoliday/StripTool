#pragma once
#include <QString>
class QApplication;
namespace striptool {
QString applicationName();
QString applicationVersion();
QString versionText();
QString aboutText();
void setDefaultApplicationStyle();
void configureApplication(QApplication& application);
}
