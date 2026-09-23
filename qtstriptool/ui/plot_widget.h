#pragma once

#include "core/model.h"
#include "core/plot_data.h"

#include <QDateTime>
#include <QWidget>
#include <array>
#include <optional>

class QContextMenuEvent;

namespace striptool {

class PlotWidget final : public QWidget {
  Q_OBJECT
public:
  explicit PlotWidget(QWidget* parent = nullptr);

  void setModel(const StripToolModel& model);
  const StripToolModel& model() const { return model_; }
  void setCurveSamples(std::size_t curve, std::vector<Sample> samples);
  void appendSample(std::size_t curve, Sample sample);
  void clearCurveSamples(std::size_t curve);
  void clearSamples();
  const std::vector<Sample>& curveSamples(std::size_t curve) const;
  void joinHistoricalSamples(std::size_t curve, const std::vector<Sample>& samples);

  bool autoScroll() const { return autoScroll_; }
  bool paused() const { return paused_; }
  TimeRange visibleTimeRange() const { return visibleTimeRange_; }
  ValueRange valueRange(std::size_t curve) const;
  int selectedAnnotation() const { return selectedAnnotation_; }
  bool isInPlot(const QPoint& position) const { return plotRect().contains(position); }

  void setAutoScroll(bool enabled);
  void setPaused(bool paused);
  void setVisibleTimeRange(TimeRange range);
  void pan(double fractionOfWindow);
  void zoom(double factor);
  void resetView();
  void advanceToNow();
  void autoScale(std::optional<std::size_t> curve = std::nullopt);
  void replot();

  int addAnnotation(Annotation annotation);
  int addAnnotationAt(const QPoint& position, const QString& text);
  bool updateAnnotation(int index, Annotation annotation);
  bool removeAnnotation(int index);
  void selectAnnotation(int index);
  void editSelectedAnnotation();

signals:
  void cursorLocationChanged(QDateTime timestamp, double value,
                             int curveIndex);
  void annotationSelectionChanged(int index);
  void annotationsChanged();
  void autoScrollChanged(bool enabled);
  void plotContextMenuRequested(QPoint globalPosition, QPoint plotPosition);

protected:
  void paintEvent(QPaintEvent* event) override;
  void mouseMoveEvent(QMouseEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;
  void wheelEvent(QWheelEvent* event) override;
  void keyPressEvent(QKeyEvent* event) override;

private:
  QRectF plotRect() const;
  std::vector<std::size_t> plottedCurves() const;
  int legendColumns() const;
  QRectF legendRect(std::size_t position) const;
  QRectF annotationRect(std::size_t index) const;
  int annotationAt(const QPoint& position) const;
  std::chrono::system_clock::time_point timeAt(const QPoint& position) const;
  std::optional<double> valueAt(const QPoint& position) const;
  void updateAutoRange();
  QColor color(const Rgba16& value) const;
  std::optional<QPointF> mapSample(const Sample& sample,
                                  const ValueRange& range,
                                  ScaleMode scale) const;

  StripToolModel model_ = makeDefaultModel();
  std::array<std::vector<Sample>, kMaximumCurves> liveSamples_;
  std::array<std::vector<Sample>, kMaximumCurves> historicalSamples_;
  std::array<std::vector<Sample>, kMaximumCurves> samples_;
  std::array<std::optional<ValueRange>, kMaximumCurves> automaticRanges_;
  std::array<bool, kMaximumCurves> autoScaleOverrides_{};
  TimeRange visibleTimeRange_;
  bool autoScroll_ = true;
  bool paused_ = false;
  enum class DragMode { None, Pan, Annotation };
  DragMode dragMode_ = DragMode::None;
  QPoint dragStart_;
  TimeRange dragRange_;
  Annotation dragAnnotation_;
  bool annotationMoved_ = false;
  QPoint cursorPosition_{-1, -1};
  int selectedAnnotation_ = -1;
  int selectedCurve_ = -1;
};

}  // namespace striptool
