#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QWidget>

namespace striptool {
// Decorative traces only; the banner never connects to a process variable.
class AboutBanner final : public QWidget {
public:
  explicit AboutBanner(QWidget* parent = nullptr);
  QSize sizeHint() const override { return QSize(460, 132); }

protected:
  void paintEvent(QPaintEvent*) override;
  void showEvent(QShowEvent*) override;
  void hideEvent(QHideEvent*) override;

private:
  QTimer timer_;
  QElapsedTimer clock_;
};
}
