#pragma once

#include <QObject>
#include <QStringList>
#include <functional>

class QWidget;

namespace striptool {

// Both display managers export whitespace-separated PV names.
QStringList droppedPvNames(const QString& text);

// Handles drops over a whole screen, including children such as line editors.
// The callback must preflight capacity and leave the model unchanged on failure.
void installPvDropTarget(QWidget* window,
                         std::function<bool(const QStringList&)> addPvs);

}  // namespace striptool
