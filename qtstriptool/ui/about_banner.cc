#include "ui/about_banner.h"

#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace striptool {
AboutBanner::AboutBanner(QWidget* parent) : QWidget(parent), timer_(this) {
  setObjectName(QStringLiteral("aboutBanner"));
  setAccessibleName(tr("Illustrative StripTool traces"));
  setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
  setFixedHeight(sizeHint().height());
  timer_.setObjectName(QStringLiteral("aboutAnimationTimer"));
  timer_.setInterval(40);
  connect(&timer_, &QTimer::timeout, this, qOverload<>(&QWidget::update));
}

void AboutBanner::showEvent(QShowEvent* event) {
  QWidget::showEvent(event);
  clock_.start();
  timer_.start();
}

void AboutBanner::hideEvent(QHideEvent* event) {
  timer_.stop();
  QWidget::hideEvent(event);
}

void AboutBanner::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  const QRectF frame = QRectF(rect()).adjusted(1, 1, -1, -1);
  painter.setPen(QPen(palette().color(QPalette::Mid), 1));
  painter.setBrush(palette().brush(QPalette::Base));
  painter.drawRoundedRect(frame, 8, 8);
  const QRectF plot = frame.adjusted(18, 16, -18, -16);
  painter.setPen(QPen(palette().color(QPalette::Mid), 1, Qt::DotLine));
  for (int i = 1; i < 6; ++i) {
    const qreal x = plot.left() + plot.width() * i / 6;
    painter.drawLine(QPointF(x, plot.top()), QPointF(x, plot.bottom()));
  }
  for (int i = 1; i < 4; ++i) {
    const qreal y = plot.top() + plot.height() * i / 4;
    painter.drawLine(QPointF(plot.left(), y), QPointF(plot.right(), y));
  }
  const QColor colors[] = {palette().color(QPalette::Link),
                           QColor(34, 160, 107), QColor(214, 141, 36)};
  const qreal time = clock_.isValid() ? clock_.elapsed() / 1000.0 : 0;
  painter.setClipRect(plot);
  for (int curve = 0; curve < 3; ++curve) {
    QPainterPath path;
    qreal previousY = 0;
    for (int point = 0; point <= 80; ++point) {
      const qreal fraction = point / 80.0;
      const qreal x = plot.left() + fraction * plot.width();
      const qreal value = 0.5 + 0.25 * std::sin(fraction * 9 + time * 0.6 + curve * 2)
          + 0.12 * std::cos(fraction * 21 - time * 0.4 + curve);
      const qreal y = plot.bottom() - value * plot.height();
      if (!point) path.moveTo(x, y);
      else {
        path.lineTo(x, previousY);
        path.lineTo(x, y);
      }
      previousY = y;
    }
    painter.setPen(QPen(colors[curve], 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
  }
}
}
