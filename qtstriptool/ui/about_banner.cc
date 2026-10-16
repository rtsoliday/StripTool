#include "ui/about_banner.h"

#include <QMouseEvent>
#include <QPainter>
#include <cmath>

namespace striptool {
namespace {
// Messages scroll from right to left, like a value written by the chart.
constexpr qreal kMessagePixelsPerSecond = 150;

QRectF plotArea(const QRect& rect) {
  return QRectF(rect).adjusted(19, 17, -19, -17);
}
}

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
  messageClock_.invalidate();
  QWidget::hideEvent(event);
}

void AboutBanner::mousePressEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || messageClock_.isValid()) {
    QWidget::mousePressEvent(event);
    return;
  }
  const QString messages[] = {tr("Happy trending!"),
                              tr("Thanks, Janet, Chris, Albert & Ken!"),
                              tr("No PVs were harmed in this animation.")};
  message_ = messages[nextMessage_];
  nextMessage_ = (nextMessage_ + 1) % 3;
  QFont font = this->font();
  font.setBold(true);
  font.setPixelSize(qMax(12, qRound(plotArea(rect()).height() * 0.42)));
  messagePath_ = QPainterPath();
  messagePath_.addText(0, 0, font, message_);
  messageClock_.start();
  update();
  event->accept();
}

void AboutBanner::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  const QRectF frame = QRectF(rect()).adjusted(1, 1, -1, -1);
  painter.setPen(QPen(palette().color(QPalette::Mid), 1));
  painter.setBrush(palette().brush(QPalette::Base));
  painter.drawRoundedRect(frame, 8, 8);
  const QRectF plot = plotArea(rect());
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
  QPointF messageOffset;
  if (messageClock_.isValid()) {
    const QRectF bounds = messagePath_.boundingRect();
    const qreal left = plot.right() - messageClock_.elapsed() / 1000.0 * kMessagePixelsPerSecond;
    if (left + bounds.width() < plot.left()) messageClock_.invalidate();
    else messageOffset = QPointF(left - bounds.left(), plot.center().y() - bounds.center().y());
  }
  const bool showingMessage = messageClock_.isValid();
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
    QColor color = colors[curve];
    if (showingMessage) color.setAlpha(70);
    painter.setPen(QPen(color, 2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
  }
  if (!showingMessage) return;
  const QPainterPath text = messagePath_.translated(messageOffset);
  const QRectF bounds = text.boundingRect();
  QLinearGradient gradient(bounds.topLeft(), bounds.topRight());
  for (int curve = 0; curve < 3; ++curve) gradient.setColorAt(curve / 2.0, colors[curve]);
  painter.setPen(Qt::NoPen);
  painter.setBrush(gradient);
  painter.drawPath(text);
}
}
