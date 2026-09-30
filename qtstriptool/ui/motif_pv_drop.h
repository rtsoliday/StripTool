#pragma once
#include <QStringList>
#include <functional>
class QWidget;
namespace striptool {
void installMotifPvDropTarget(QWidget* window,
                              std::function<bool(const QStringList&)> addPvs);
}
