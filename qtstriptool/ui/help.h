#pragma once

#include <QString>
#include <QUrl>

class QDialog;
class QMenu;
class QWidget;

namespace striptool {
QDialog* showHelp(QWidget* parent, const QString& topic = {});
void showAbout(QWidget* parent);
QUrl helpManualUrl();
void populateHelpMenu(QMenu* menu, QWidget* owner,
                      const QString& prefix = {}, const QString& topic = {});
}
