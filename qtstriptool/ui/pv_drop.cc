#include "ui/pv_drop.h"
#include "ui/motif_pv_drop.h"
#include "core/model.h"

#include <QApplication>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QRegularExpression>
#include <QWidget>

namespace striptool {

QStringList droppedPvNames(const QString& text) {
  if (text.size() > 4096) return {};
  for (const QChar character : text) {
    if ((character.unicode() < 32 && !character.isSpace()) ||
        character.unicode() == 127 || character == QChar::ReplacementCharacter)
      return {};
  }
  const auto names = text.split(QRegularExpression(QStringLiteral("\\s+")),
                                Qt::SkipEmptyParts);
  QStringList result;
  for (const auto& name : names) {
    if (name.toUtf8().size() > int(kMaximumCurveNameLength)) return {};
    if (!result.contains(name)) result.append(name);
  }
  return result;
}

namespace {
class PvDropTarget final : public QObject {
public:
  PvDropTarget(QWidget* window, std::function<bool(const QStringList&)> addPvs)
      : QObject(window), window_(window), addPvs_(std::move(addPvs)) {
    window->setAcceptDrops(true);
    qApp->installEventFilter(this);
  }

protected:
  bool eventFilter(QObject* object, QEvent* event) override {
    auto* widget = qobject_cast<QWidget*>(object);
    if (!widget || widget->window() != window_)
      return false;
    if (event->type() == QEvent::DragLeave) {
      pendingNames_.clear();
      return false;
    }
    if (event->type() != QEvent::DragEnter &&
        event->type() != QEvent::DragMove && event->type() != QEvent::Drop)
      return false;
    auto* drop = static_cast<QDropEvent*>(event);
    const auto* mime = drop->mimeData();
    // External Qt sources can relinquish their selection before the final
    // drop is delivered. Keep the PVs accepted for this gesture rather than
    // requesting that selection again on every motion and on release.
    if (event->type() == QEvent::DragEnter) {
      pendingNames_ = mime->hasText() && !mime->hasUrls()
                          ? droppedPvNames(mime->text()) : QStringList{};
    }
    const auto names = pendingNames_;
    if (names.isEmpty() || !(drop->possibleActions() & Qt::CopyAction)) {
      drop->ignore();
    } else if (event->type() != QEvent::Drop || addPvs_(names)) {
      drop->setDropAction(Qt::CopyAction);
      drop->accept();
    } else {
      drop->ignore();
    }
    if (event->type() == QEvent::Drop) pendingNames_.clear();
    return true;
  }

private:
  QStringList pendingNames_;
  QWidget* window_;
  std::function<bool(const QStringList&)> addPvs_;
};
}  // namespace

void installPvDropTarget(QWidget* window,
                         std::function<bool(const QStringList&)> addPvs) {
  installMotifPvDropTarget(window, addPvs);
  new PvDropTarget(window, std::move(addPvs));
}

}  // namespace striptool
