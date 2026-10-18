#pragma once

#include "core/model.h"
#include "core/plot_data.h"

#include <QDateTime>
#include <QImage>
#include <QStringList>
#include <QWidget>
#include <array>
#include <optional>

class QContextMenuEvent;
class QPainter;

namespace striptool {

struct TimeAxisLabels {
  QStringList relative;
  QStringList clock;
  QString caption;
  QString date;
};

class PlotWidget final : public QWidget {
  Q_OBJECT
public:
  explicit PlotWidget(QWidget* parent = nullptr);

  void setModel(const StripToolModel& model);
  const StripToolModel& model() const { return model_; }
  // firstEvicted is the time of the first live sample the acquisition buffer
  // discarded, if any. Archived samples are not held across that point.
  void setCurveSamples(std::size_t curve, std::vector<Sample> samples,
                       std::optional<std::chrono::system_clock::time_point>
                           firstEvicted = std::nullopt);
  void appendSample(std::size_t curve, Sample sample);
  void clearCurveSamples(std::size_t curve);
  void clearSamples();
  const std::vector<Sample>& curveSamples(std::size_t curve) const;
  void joinHistoricalSamples(std::size_t curve, const std::vector<Sample>& samples);
  static constexpr std::size_t kMaximumHistorySamples = 65536;
  static constexpr std::size_t kHistoryMemoryLimit = 2 * 1024 * 1024;
  bool historyReduced(std::size_t curve) const;
  std::size_t retainedSampleBytes(std::size_t curve) const;

  bool autoScroll() const { return autoScroll_; }
  bool autoScaleEnabled() const { return autoScaleEnabled_; }
  bool paused() const { return paused_; }
  TimeRange visibleTimeRange() const { return visibleTimeRange_; }
  ValueRange valueRange(std::size_t curve) const;
  QStringList yAxisLabels(std::size_t curve, int divisions = 5) const;
  TimeAxisLabels xAxisLabels(int divisions = 5) const;
  // Labelled tick and grid divisions at the current size and font.
  int timeAxisDivisions() const;
  int valueAxisDivisions() const;
  int selectedAnnotation() const { return selectedAnnotation_; }
  bool isInPlot(const QPoint& position) const { return plotRect().contains(position); }

  void setAutoScroll(bool enabled);
  void setPaused(bool paused);
  void setVisibleTimeRange(TimeRange range);
  void pan(double fractionOfWindow);
  void zoom(double factor);
  void panY(double fractionOfRange);
  void zoomY(double factor);
  void resetVerticalView();
  void resetView();
  void advanceToNow();
  void autoScale();
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
  void historyReductionChanged();
  void autoScrollChanged(bool enabled);
  // Emitted when zoom, pan, drag or reset changes the visible time range;
  // live scrolling, which only moves it forward, does not emit it.
  void visibleTimeRangeChanged();
  void autoScaleChanged(bool enabled);
  void plotContextMenuRequested(QPoint globalPosition, QPoint plotPosition);

protected:
  void changeEvent(QEvent* event) override;
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
  // plotRect() as last painted while that is still current.
  QRectF currentPlotRect() const;
  std::vector<std::size_t> plottedCurves() const;
  QRectF legendRect(std::size_t position) const;
  // These take an already computed plotRect(), which is costly to lay out.
  QRectF legendRect(std::size_t position, const QRectF& area) const;
  int timeAxisDivisions(const QRectF& area) const;
  QRectF annotationRect(std::size_t index, const QRectF& area) const;
  QStringList legendLines(std::size_t curve, bool widestValue = false) const;
  int valueDivisionsForHeight(qreal plotHeight) const;
  void updateMinimumSize();
  QRectF annotationRect(std::size_t index) const;
  int annotationAt(const QPoint& position) const;
  std::chrono::system_clock::time_point timeAt(const QPoint& position) const;
  std::optional<double> valueAt(const QPoint& position) const;
  // Historical and live samples of a curve, broken where live data was evicted.
  std::vector<Sample> joinedSamples(std::size_t curve) const;
  void updateAutoRange();
  void resolveAutoRange(std::size_t curve) const;
  void resolveAutoRanges() const;
  // Schedules a repaint of everything; update() alone only redraws the cursor.
  void redraw();
  void paintPlot(QPainter& painter, const QRectF& area);
  void zoomAt(double factor, const QPointF& position, bool keepAutoScroll = false);
  void updateDrag(const QPoint& position);
  void finishDrag();
  QColor color(const Rgba16& value) const;
  std::optional<QPointF> mapSample(const Sample& sample,
                                  const ValueRange& range,
                                  ScaleMode scale, const QRectF& area,
                                  qint64 begin, qint64 end) const;

  StripToolModel model_ = makeDefaultModel();
  std::array<std::vector<Sample>, kMaximumCurves> liveSamples_;
  std::array<std::vector<Sample>, kMaximumCurves> historicalSamples_;
  std::array<std::optional<std::chrono::system_clock::time_point>, kMaximumCurves>
      firstEvicted_;
  std::array<bool, kMaximumCurves> historyReduced_{};
  // Widest legend value line shown per curve; plotRect() only ever grows it.
  mutable std::array<QString, kMaximumCurves> widestLegendValue_;
  std::array<std::vector<Sample>, kMaximumCurves> samples_;
  // Automatic ranges are computed when next read. A stale range is resolved
  // before its inputs change without a new updateAutoRange().
  mutable std::array<std::optional<ValueRange>, kMaximumCurves> automaticRanges_;
  mutable std::array<bool, kMaximumCurves> automaticRangeStale_{};
  std::array<std::optional<ValueRange>, kMaximumCurves> verticalRanges_;
  bool autoScaleEnabled_ = true;
  TimeRange visibleTimeRange_;
  bool autoScroll_ = true;
  bool paused_ = false;
  enum class DragMode { None, Pan, Annotation };
  DragMode dragMode_ = DragMode::None;
  QPoint dragStart_;
  TimeRange dragRange_;
  bool panMoved_ = false;
  Annotation dragAnnotation_;
  QRectF dragAnnotationRect_;
  QPoint cursorPosition_{-1, -1};
  int selectedAnnotation_ = -1;
  int selectedCurve_ = -1;
  // Everything except the cursor and annotations, reused while only the
  // cursor moves.
  QImage plotCache_;
  QRectF plotCacheArea_;
  QSize plotCacheSize_;
  bool plotCacheValid_ = false;
};

}  // namespace striptool
