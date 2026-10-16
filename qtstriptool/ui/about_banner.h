#pragma once

#include <QElapsedTimer>
#include <QPainterPath>
#include <QTimer>
#include <QWidget>

namespace striptool {
// Decorative traces only; the banner never connects to a process variable.
// Clicking it scrolls a short message across the chart.
class AboutBanner final : public QWidget {
public:
  explicit AboutBanner(QWidget* parent = nullptr);
  QSize sizeHint() const override { return QSize(460, 132); }
  QString message() const { return messageClock_.isValid() ? message_ : QString(); }

protected:
  void paintEvent(QPaintEvent*) override;
  void mousePressEvent(QMouseEvent* event) override;
  void showEvent(QShowEvent*) override;
  void hideEvent(QHideEvent*) override;

private:
  QTimer timer_;
  QElapsedTimer clock_;
  QElapsedTimer messageClock_;
  QString message_;
  QPainterPath messagePath_;
  int nextMessage_ = 0;
};
}
