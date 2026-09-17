#pragma once

#include "core/model.h"
#include "core/plot_data.h"

#include <QDateTime>
#include <QWidget>
#include <array>
#include <optional>

namespace striptool {

class PlotWidget final : public QWidget {
  Q_OBJECT
public:
  explicit PlotWidget(QWidget* parent = nullptr);

  void setModel(const StripToolModel& model);
  const StripToolModel& model() const { return model_; }
  void setCurveSamples(std::size_t curve, std::vector<Sample> samples);
  void appendSample(std::size_t curve, Sample sample);
  const std::vector<Sample>& curveSamples(std::size_t curve) const;
  void joinHistoricalSamples(std::size_t curve, const std::vector<Sample>& samples);

  bool autoScroll() const { return autoScroll_; }
  bool paused() const { return paused_; }
  TimeRange visibleTimeRange() const { return visibleTimeRange_; }
  ValueRange valueRange(std::size_t curve) const;
  int selectedAnnotation() const { return selectedAnnotation_; }

  void setAutoScroll(bool enabled);
  void setPaused(bool paused);
  void setVisibleTimeRange(TimeRange range);
  void pan(double fractionOfWindow);
  void zoom(double factor);
  void resetView();
  void autoScale(std::optional<std::size_t> curve = std::nullopt);
  void replot();

  int addAnnotation(Annotation annotation);
  bool updateAnnotation(int index, Annotation annotation);
  bool removeAnnotation(int index);
  void selectAnnotation(int index);

signals:
  void cursorLocationChanged(QDateTime timestamp, double value,
                             int curveIndex);
  void annotationSelectionChanged(int index);

protected:
  void paintEvent(QPaintEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;

private:
  QRectF plotRect() const;
  void updateAutoRange();
  QColor color(const Rgba16& value) const;
  std::optional<QPointF> mapSample(const Sample& sample,
                                  const ValueRange& range,
                                  ScaleMode scale) const;

  StripToolModel model_ = makeDefaultModel();
  std::array<std::vector<Sample>, kMaximumCurves> samples_;
  std::array<std::optional<ValueRange>, kMaximumCurves> automaticRanges_;
  std::array<bool, kMaximumCurves> autoScaleOverrides_{};
  TimeRange visibleTimeRange_;
  bool autoScroll_ = true;
  bool paused_ = false;
  bool dragging_ = false;
  bool draggingAnnotation_ = false;
  QPoint dragStart_;
  TimeRange dragRange_;
  QPoint cursorPosition_{-1, -1};
  int selectedAnnotation_ = -1;
};

}  // namespace striptool
