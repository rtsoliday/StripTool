#include "ui/plot_widget.h"

#include <QDateTime>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QWheelEvent>
#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>

namespace striptool {
namespace {

qint64 milliseconds(std::chrono::system_clock::time_point time) {
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             time.time_since_epoch()).count();
}

QString formattedValue(double value, int precision, ScaleMode scale) {
  if (scale == ScaleMode::Log10)
    return QString::number(std::pow(10.0, value), 'g', std::max(1, precision));
  QString label = QString::number(value, 'f', std::clamp(precision, 0, 12));
  if (label.contains(QLatin1Char('.'))) {
    while (label.endsWith(QLatin1Char('0'))) label.chop(1);
    if (label.endsWith(QLatin1Char('.'))) label.chop(1);
  }
  return label == QStringLiteral("-0") ? QStringLiteral("0") : label;
}

bool labelsAreDistinct(const QStringList& labels) {
  for (int i = 1; i < labels.size(); ++i)
    if (labels[i] == labels[i - 1]) return false;
  return true;
}

QStringList axisLabels(const ValueRange& range, int precision,
                       ScaleMode scale, int divisions) {
  QStringList labels;
  if (!range.isValid() || divisions < 1) return labels;

  // A channel's precision describes its values, but an axis may be zoomed to
  // a range whose tick spacing is smaller than that precision. Add decimals
  // until adjacent ticks remain distinguishable.
  const int firstPrecision = scale == ScaleMode::Log10
                                 ? std::max(1, precision)
                                 : std::max(0, precision);
  const int maximumPrecision = scale == ScaleMode::Log10 ? 17 : 15;
  for (int candidate = std::min(firstPrecision, maximumPrecision);
       candidate <= maximumPrecision; ++candidate) {
    labels.clear();
    for (int tick = 0; tick <= divisions; ++tick) {
      const double value = range.maximum -
          (range.maximum - range.minimum) * tick / divisions;
      labels.push_back(formattedValue(value, candidate, scale));
    }
    if (labelsAreDistinct(labels)) return labels;
  }

  // Fixed notation can still run out of useful decimal places for a narrow
  // range around a very large value. Full significant-digit notation is the
  // final lossless fallback.
  labels.clear();
  for (int tick = 0; tick <= divisions; ++tick) {
    const double plotted = range.maximum -
        (range.maximum - range.minimum) * tick / divisions;
    const double value = scale == ScaleMode::Log10 ? std::pow(10.0, plotted)
                                                    : plotted;
    labels.push_back(QString::number(value, 'g', 17));
  }
  return labels;
}

}  // namespace

PlotWidget::PlotWidget(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("plotWidget"));
  setMinimumSize(800, 260);
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  resetView();
}

void PlotWidget::setModel(const StripToolModel& model) {
  std::array<bool, kMaximumCurves> replacedCurves{};
  for (std::size_t i = 0; i < kMaximumCurves; ++i) {
    const auto& old = model_.curves[i];
    const auto& next = model.curves[i];
    if (old.nameSet != next.nameSet || old.name != next.name ||
        old.scale != next.scale || old.minimumSet != next.minimumSet ||
        old.maximumSet != next.maximumSet ||
        (old.minimumSet && old.minimum != next.minimum) ||
        (old.maximumSet && old.maximum != next.maximum)) {
      verticalRanges_[i].reset();
    }
    replacedCurves[i] = old.nameSet &&
        (!next.nameSet || old.name != next.name);
  }
  model_ = model;
  const int previousSelection = selectedAnnotation_;
  int nextSelection = previousSelection;
  int removedBeforeSelection = 0;
  for (std::size_t i = 0; i < model_.annotations.size(); ++i) {
    const auto& annotation = model_.annotations[i];
    const bool removed = annotation.curveIndex &&
                         *annotation.curveIndex < kMaximumCurves &&
                         replacedCurves[*annotation.curveIndex];
    if (removed && static_cast<int>(i) == previousSelection)
      nextSelection = -1;
    else if (removed && static_cast<int>(i) < previousSelection)
      ++removedBeforeSelection;
  }
  if (nextSelection >= 0) nextSelection -= removedBeforeSelection;
  const auto count = model_.annotations.size();
  model_.annotations.erase(std::remove_if(model_.annotations.begin(),
                                          model_.annotations.end(),
      [&replacedCurves](const Annotation& annotation) {
        return annotation.curveIndex &&
               *annotation.curveIndex < kMaximumCurves &&
               replacedCurves[*annotation.curveIndex];
      }), model_.annotations.end());
  const bool annotationsRemoved = model_.annotations.size() != count;
  if (annotationsRemoved) emit annotationsChanged();
  if (nextSelection >= static_cast<int>(model_.annotations.size())) nextSelection = -1;
  if (nextSelection != previousSelection) {
    selectedAnnotation_ = nextSelection;
    emit annotationSelectionChanged(nextSelection);
  }
  if (dragMode_ == DragMode::Annotation && nextSelection < 0)
    dragMode_ = DragMode::None;
  if (selectedCurve_ >= 0 &&
      (!model_.curves[static_cast<std::size_t>(selectedCurve_)].nameSet ||
       !model_.curves[static_cast<std::size_t>(selectedCurve_)].plotted))
    selectedCurve_ = -1;
  updateAutoRange();
  update();
}

void PlotWidget::setCurveSamples(std::size_t curve, std::vector<Sample> samples) {
  if (curve >= samples_.size()) return;
  const bool hadSamples = std::any_of(samples_.begin(), samples_.end(),
                                      [](const auto& data) { return !data.empty(); });
  std::sort(samples.begin(), samples.end(), [](const auto& left, const auto& right) {
    return left.timestamp < right.timestamp;
  });
  liveSamples_[curve] = std::move(samples);
  samples_[curve] = joinHistoricalAndLive(historicalSamples_[curve], liveSamples_[curve]);
  if (autoScroll_ && !paused_ && dragMode_ == DragMode::None) {
    if (!hadSamples) resetView();
    else if (!samples_[curve].empty()) {
      const auto now = std::chrono::system_clock::now();
      if (visibleTimeRange_.end > now)
        visibleTimeRange_.end = now;
      else if (samples_[curve].back().timestamp <= now)
        visibleTimeRange_.end = std::max(visibleTimeRange_.end,
                                         samples_[curve].back().timestamp);
      visibleTimeRange_.start = visibleTimeRange_.end -
          std::chrono::seconds(model_.timing.timespanSeconds);
    }
  }
  if (dragMode_ == DragMode::None) updateAutoRange();
  update();
}

void PlotWidget::appendSample(std::size_t curve, Sample sample) {
  if (curve >= samples_.size()) return;
  auto& data = liveSamples_[curve];
  if (!data.empty() && sample.timestamp < data.back().timestamp) {
    const auto at = std::upper_bound(data.begin(), data.end(), sample.timestamp,
                                     [](const auto& time, const Sample& item) {
                                       return time < item.timestamp;
                                     });
    data.insert(at, sample);
  } else {
    data.push_back(sample);
  }
  const std::size_t maximum = std::max(1, model_.timing.numberOfSamples);
  if (data.size() > maximum)
    data.erase(data.begin(), data.end() - static_cast<std::ptrdiff_t>(maximum));
  samples_[curve] = joinHistoricalAndLive(historicalSamples_[curve], data);
  if (autoScroll_ && !paused_ && dragMode_ == DragMode::None) {
    const auto now = std::chrono::system_clock::now();
    if (visibleTimeRange_.end > now)
      visibleTimeRange_.end = now;
    else if (sample.timestamp <= now)
      visibleTimeRange_.end = std::max(visibleTimeRange_.end, sample.timestamp);
    visibleTimeRange_.start = visibleTimeRange_.end -
        std::chrono::seconds(model_.timing.timespanSeconds);
  }
  if (dragMode_ == DragMode::None) updateAutoRange();
  update();
}

void PlotWidget::clearCurveSamples(std::size_t curve) {
  if (curve >= samples_.size()) return;
  liveSamples_[curve].clear();
  historicalSamples_[curve].clear();
  samples_[curve].clear();
  automaticRanges_[curve].reset();
  verticalRanges_[curve].reset();
  update();
}

void PlotWidget::clearSamples() {
  for (std::size_t i = 0; i < samples_.size(); ++i) clearCurveSamples(i);
  resetView();
}

const std::vector<Sample>& PlotWidget::curveSamples(std::size_t curve) const {
  static const std::vector<Sample> empty;
  return curve < samples_.size() ? samples_[curve] : empty;
}

void PlotWidget::joinHistoricalSamples(std::size_t curve,
                                       const std::vector<Sample>& samples) {
  if (curve >= samples_.size()) return;
  auto ordered = samples;
  std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
    return left.timestamp < right.timestamp;
  });
  historicalSamples_[curve] = joinHistoricalAndLive(historicalSamples_[curve], ordered);
  samples_[curve] = joinHistoricalAndLive(historicalSamples_[curve], liveSamples_[curve]);
  if (dragMode_ == DragMode::None) updateAutoRange();
  update();
}

ValueRange PlotWidget::valueRange(std::size_t curve) const {
  if (curve >= kMaximumCurves) return {};
  if (verticalRanges_[curve]) return *verticalRanges_[curve];
  if (automaticRanges_[curve]) return *automaticRanges_[curve];
  const auto& config = model_.curves[curve];
  ValueRange range{plotValue(config.minimum, config.scale),
                   plotValue(config.maximum, config.scale)};
  return range.isValid() ? range : ValueRange{};
}

QStringList PlotWidget::yAxisLabels(std::size_t curve, int divisions) const {
  if (curve >= kMaximumCurves) return {};
  return axisLabels(valueRange(curve), model_.curves[curve].precision,
                    model_.curves[curve].scale, divisions);
}

void PlotWidget::setAutoScroll(bool enabled) {
  const bool changed = autoScroll_ != enabled;
  autoScroll_ = enabled;
  if (changed) emit autoScrollChanged(enabled);
  if (enabled && !paused_) resetView();
}

void PlotWidget::setPaused(bool paused) {
  paused_ = paused;
  if (!paused_ && autoScroll_) resetView();
}

void PlotWidget::setVisibleTimeRange(TimeRange range) {
  if (!range.isValid() || range.start == range.end) return;
  visibleTimeRange_ = range;
  setAutoScroll(false);
  updateAutoRange();
  update();
}

void PlotWidget::pan(double fractionOfWindow) {
  const auto duration = visibleTimeRange_.end - visibleTimeRange_.start;
  const auto shift = std::chrono::duration_cast<std::chrono::system_clock::duration>(
      std::chrono::duration<double>(
          std::chrono::duration<double>(duration).count() * fractionOfWindow));
  visibleTimeRange_.start += shift;
  visibleTimeRange_.end += shift;
  setAutoScroll(false);
  updateAutoRange();
  update();
}

void PlotWidget::zoom(double factor) {
  zoomAt(factor, plotRect().center());
}

void PlotWidget::zoomAt(double factor, const QPointF& position) {
  if (!(factor > 0.0) || !std::isfinite(factor)) return;
  const QRectF area = plotRect();
  if (area.width() <= 0.0) return;
  const double fraction = std::clamp(
      (position.x() - area.left()) / area.width(), 0.0, 1.0);
  const auto duration = visibleTimeRange_.end - visibleTimeRange_.start;
  const auto scaledDuration = std::chrono::duration_cast<
      std::chrono::system_clock::duration>(
          std::chrono::duration<double>(
              std::chrono::duration<double>(duration).count() * factor));
  if (scaledDuration <= std::chrono::milliseconds(2)) return;
  const auto anchorOffset = std::chrono::duration_cast<
      std::chrono::system_clock::duration>(
          std::chrono::duration<double>(
              std::chrono::duration<double>(duration).count() * fraction));
  const auto scaledAnchorOffset = std::chrono::duration_cast<
      std::chrono::system_clock::duration>(
          std::chrono::duration<double>(
              std::chrono::duration<double>(scaledDuration).count() * fraction));
  const auto anchor = visibleTimeRange_.start + anchorOffset;
  visibleTimeRange_ = {anchor - scaledAnchorOffset,
                       anchor - scaledAnchorOffset + scaledDuration};
  setAutoScroll(false);
  updateAutoRange();
  update();
}

void PlotWidget::panY(double fractionOfRange) {
  if (!std::isfinite(fractionOfRange)) return;
  if (autoScaleEnabled_) {
    autoScaleEnabled_ = false;
    emit autoScaleChanged(false);
  }
  for (const auto index : plottedCurves()) {
    const ValueRange range = valueRange(index);
    if (!range.isValid()) continue;
    const double shift = (range.maximum - range.minimum) * fractionOfRange;
    const ValueRange moved{range.minimum + shift, range.maximum + shift};
    if (std::isfinite(moved.minimum) && std::isfinite(moved.maximum) &&
        moved.isValid()) verticalRanges_[index] = moved;
  }
  update();
}

void PlotWidget::zoomY(double factor) {
  if (!std::isfinite(factor) || factor <= 0.0) return;
  if (autoScaleEnabled_) {
    autoScaleEnabled_ = false;
    emit autoScaleChanged(false);
  }
  for (const auto index : plottedCurves()) {
    const ValueRange range = valueRange(index);
    if (!range.isValid()) continue;
    const double center = range.minimum / 2.0 + range.maximum / 2.0;
    const double half = (range.maximum - range.minimum) * factor / 2.0;
    const ValueRange zoomed{center - half, center + half};
    if (std::isfinite(zoomed.minimum) && std::isfinite(zoomed.maximum) &&
        zoomed.isValid()) verticalRanges_[index] = zoomed;
  }
  update();
}

void PlotWidget::resetVerticalView() {
  const bool changed = autoScaleEnabled_;
  autoScaleEnabled_ = false;
  for (std::size_t i = 0; i < kMaximumCurves; ++i) {
    verticalRanges_[i].reset();
    automaticRanges_[i].reset();
  }
  if (changed) emit autoScaleChanged(false);
  update();
}

void PlotWidget::resetView() {
  visibleTimeRange_.end = std::chrono::system_clock::now();
  visibleTimeRange_.start = visibleTimeRange_.end -
      std::chrono::seconds(model_.timing.timespanSeconds);
  const bool changed = !autoScroll_;
  autoScroll_ = true;
  if (changed) emit autoScrollChanged(true);
  updateAutoRange();
  update();
}

void PlotWidget::advanceToNow() {
  if (!autoScroll_ || paused_ || dragMode_ != DragMode::None) return;
  visibleTimeRange_.end = std::chrono::system_clock::now();
  visibleTimeRange_.start = visibleTimeRange_.end -
      std::chrono::seconds(model_.timing.timespanSeconds);
  updateAutoRange();
  update();
}

void PlotWidget::autoScale() {
  const bool changed = !autoScaleEnabled_;
  autoScaleEnabled_ = true;
  verticalRanges_.fill(std::nullopt);
  updateAutoRange();
  if (changed) emit autoScaleChanged(true);
  update();
}

void PlotWidget::replot() { update(); }

int PlotWidget::addAnnotation(Annotation annotation) {
  model_.annotations.push_back(std::move(annotation));
  selectedAnnotation_ = static_cast<int>(model_.annotations.size()) - 1;
  emit annotationSelectionChanged(selectedAnnotation_);
  emit annotationsChanged();
  update();
  return selectedAnnotation_;
}

int PlotWidget::addAnnotationAt(const QPoint& position, const QString& text) {
  if (!plotRect().contains(position)) return -1;
  Annotation annotation{timeAt(position), valueAt(position), text.toStdString()};
  const auto curves = plottedCurves();
  if (!curves.empty())
    annotation.curveIndex = selectedCurve_ >= 0
                                ? static_cast<std::size_t>(selectedCurve_) : curves.front();
  return addAnnotation(std::move(annotation));
}

bool PlotWidget::updateAnnotation(int index, Annotation annotation) {
  if (index < 0 || index >= static_cast<int>(model_.annotations.size())) return false;
  model_.annotations[static_cast<std::size_t>(index)] = std::move(annotation);
  emit annotationsChanged();
  update();
  return true;
}

bool PlotWidget::removeAnnotation(int index) {
  if (index < 0 || index >= static_cast<int>(model_.annotations.size())) return false;
  model_.annotations.erase(model_.annotations.begin() + index);
  selectedAnnotation_ = -1;
  emit annotationSelectionChanged(-1);
  emit annotationsChanged();
  update();
  return true;
}

void PlotWidget::selectAnnotation(int index) {
  if (index < -1 || index >= static_cast<int>(model_.annotations.size())) return;
  selectedAnnotation_ = index;
  emit annotationSelectionChanged(index);
  update();
}

void PlotWidget::editSelectedAnnotation() {
  if (selectedAnnotation_ < 0 ||
      selectedAnnotation_ >= static_cast<int>(model_.annotations.size())) return;
  bool accepted = false;
  const QString text = QInputDialog::getText(
      this, tr("Edit Annotation"), tr("Text:"), QLineEdit::Normal,
      QString::fromStdString(model_.annotations[static_cast<std::size_t>(selectedAnnotation_)].text),
      &accepted);
  if (!accepted || text.isEmpty()) return;
  auto annotation = model_.annotations[static_cast<std::size_t>(selectedAnnotation_)];
  annotation.text = text.toStdString();
  updateAnnotation(selectedAnnotation_, std::move(annotation));
}

QRectF PlotWidget::plotRect() const {
  // The Motif graph has one selectable Y axis and a legend beside the plot.
  constexpr int unitsLaneWidth = 22;
  constexpr int labelGap = 8;
  int leftMargin = 72;
  const auto curves = plottedCurves();
  const int axisCurve = selectedCurve_ >= 0 ? selectedCurve_ :
                        (curves.empty() ? -1 : static_cast<int>(curves.front()));
  if (axisCurve >= 0) {
    const QFontMetrics metrics(font());
    int labelWidth = 0;
    for (const QString& label : yAxisLabels(static_cast<std::size_t>(axisCurve)))
      labelWidth = std::max(labelWidth, metrics.horizontalAdvance(label));
    leftMargin = std::max(leftMargin, unitsLaneWidth + labelGap + labelWidth + labelGap);
  }
  return rect().adjusted(leftMargin, model_.title.empty() ? 20 : 38, -178, -64);
}

std::vector<std::size_t> PlotWidget::plottedCurves() const {
  std::vector<std::size_t> result;
  for (std::size_t i = 0; i < kMaximumCurves; ++i)
    if (model_.curves[i].nameSet && model_.curves[i].plotted) result.push_back(i);
  return result;
}

QRectF PlotWidget::legendRect(std::size_t position) const {
  const qreal rowHeight = std::min<qreal>(56, qreal(height() - 12) /
                                                std::max<std::size_t>(1, plottedCurves().size()));
  const QRectF area = plotRect();
  return QRectF(area.right() + 10, 8 + position * rowHeight,
                width() - area.right() - 16, rowHeight - 2);
}

std::chrono::system_clock::time_point PlotWidget::timeAt(const QPoint& position) const {
  const QRectF area = plotRect();
  const qint64 begin = milliseconds(visibleTimeRange_.start);
  const qint64 end = milliseconds(visibleTimeRange_.end);
  const qreal fraction = std::clamp<qreal>((position.x() - area.left()) /
                                           std::max<qreal>(1, area.width()), 0, 1);
  return std::chrono::system_clock::time_point(
      std::chrono::milliseconds(begin + qint64((end - begin) * fraction)));
}

std::optional<double> PlotWidget::valueAt(const QPoint& position) const {
  const auto curves = plottedCurves();
  if (curves.empty()) return std::nullopt;
  const std::size_t index = selectedCurve_ >= 0
                                ? static_cast<std::size_t>(selectedCurve_) : curves.front();
  const ValueRange range = valueRange(index);
  const QRectF area = plotRect();
  if (!range.isValid() || area.height() <= 0) return std::nullopt;
  const double plotted = range.maximum -
      (position.y() - area.top()) / area.height() *
          (range.maximum - range.minimum);
  const double value = model_.curves[index].scale == ScaleMode::Log10
                           ? std::pow(10.0, plotted) : plotted;
  return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
}

QRectF PlotWidget::annotationRect(std::size_t index) const {
  if (index >= model_.annotations.size()) return {};
  const auto& annotation = model_.annotations[index];
  const QRectF area = plotRect();
  const qint64 begin = milliseconds(visibleTimeRange_.start);
  const qint64 end = milliseconds(visibleTimeRange_.end);
  const qint64 time = milliseconds(annotation.time);
  if (end <= begin || time < begin || time > end) return {};
  const qreal x = area.left() + area.width() * double(time - begin) / double(end - begin);
  qreal y = area.top() + 4;
  const auto curves = plottedCurves();
  if (annotation.value && !curves.empty()) {
    const std::size_t curve = annotation.curveIndex &&
                                      *annotation.curveIndex < kMaximumCurves &&
                                      model_.curves[*annotation.curveIndex].nameSet &&
                                      model_.curves[*annotation.curveIndex].plotted
                                  ? *annotation.curveIndex : curves.front();
    const ValueRange range = valueRange(curve);
    const double plotted = plotValue(*annotation.value, model_.curves[curve].scale);
    if (range.isValid() && std::isfinite(plotted))
      y = area.bottom() - area.height() *
          (plotted - range.minimum) / (range.maximum - range.minimum);
  }
  const QFontMetrics metrics(font());
  const QString text = QString::fromStdString(annotation.text);
  const int wrapWidth = std::max(16, std::min(240, int(area.width()) - 12));
  const QRect bounds = metrics.boundingRect(QRect(0, 0, wrapWidth, 1000),
                                             Qt::TextWordWrap, text);
  const qreal boxWidth = std::min<qreal>(area.width(),
                                        std::max(28, bounds.width() + 12));
  const qreal boxHeight = std::min<qreal>(area.height(),
                                         std::max(metrics.height() + 8,
                                                  bounds.height() + 8));
  return QRectF(std::clamp(x, area.left(), area.right() - boxWidth),
                std::clamp(y, area.top(), area.bottom() - boxHeight),
                boxWidth, boxHeight);
}

int PlotWidget::annotationAt(const QPoint& position) const {
  for (int i = static_cast<int>(model_.annotations.size()) - 1; i >= 0; --i)
    if (annotationRect(static_cast<std::size_t>(i)).contains(position)) return i;
  return -1;
}

void PlotWidget::updateAutoRange() {
  for (std::size_t i = 0; i < kMaximumCurves; ++i) {
    if (autoScaleEnabled_) {
      automaticRanges_[i] = visibleSampleValueRange(
          samples_[i], visibleTimeRange_.start, visibleTimeRange_.end,
          model_.curves[i].scale);
    } else {
      automaticRanges_[i].reset();
    }
  }
}

QColor PlotWidget::color(const Rgba16& value) const {
  return QColor::fromRgbF(value.red / 65535.0, value.green / 65535.0,
                          value.blue / 65535.0, value.alpha / 65535.0);
}

std::optional<QPointF> PlotWidget::mapSample(const Sample& sample,
                                            const ValueRange& range,
                                            ScaleMode scale) const {
  const QRectF area = plotRect();
  const qint64 begin = milliseconds(visibleTimeRange_.start);
  const qint64 end = milliseconds(visibleTimeRange_.end);
  const qint64 time = milliseconds(sample.timestamp);
  const double value = plotValue(sample.value, scale);
  if (end <= begin || !sample.plotable || !range.isValid() || !std::isfinite(value))
    return std::nullopt;
  return QPointF(area.left() + area.width() * double(time - begin) / double(end - begin),
                 area.bottom() - area.height() * (value - range.minimum) /
                                     (range.maximum - range.minimum));
}

void PlotWidget::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.fillRect(rect(), color(model_.colors.background));
  const QRectF area = plotRect();
  const QColor foreground = color(model_.colors.foreground);
  const auto visibleCurves = plottedCurves();

  const QString title = QString::fromStdString(model_.title);
  if (!title.isEmpty()) {
    QFont titleFont = painter.font();
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(foreground);
    painter.drawText(QRectF(area.left(), 6, area.width(), 24),
                     Qt::AlignCenter,
                     painter.fontMetrics().elidedText(
                         title, Qt::ElideRight, static_cast<int>(area.width())));
    painter.setFont(font());
  }

  for (std::size_t position = 0; position < visibleCurves.size(); ++position) {
    const std::size_t index = visibleCurves[position];
    const QRectF legend = legendRect(position);
    if (selectedCurve_ == static_cast<int>(index)) {
      painter.setPen(QPen(foreground, 1));
      painter.drawRect(legend);
    }
    painter.setPen(QPen(color(model_.colors.curves[index]), 3));
    painter.drawLine(QPointF(legend.left() + 4, legend.top() + 10),
                     QPointF(legend.left() + 18, legend.top() + 10));
    const auto& config = model_.curves[index];
    const QString label = QString::fromStdString(config.name);
    QString latestValue;
    if (!samples_[index].empty() && samples_[index].back().plotable &&
        std::isfinite(samples_[index].back().value))
      latestValue = QString::number(samples_[index].back().value, 'g',
                                    std::clamp(config.precision, 1, 15));
    painter.setPen(foreground);
    const QRectF textArea(legend.left() + 23, legend.top(),
                          legend.width() - 26, 21);
    painter.drawText(textArea, Qt::AlignVCenter | Qt::AlignLeft,
                     painter.fontMetrics().elidedText(label, Qt::ElideRight,
                                                       static_cast<int>(textArea.width())));
    if (legend.height() >= 38) {
      const ValueRange plottedRange = valueRange(index);
      const double lower = config.scale == ScaleMode::Log10
                               ? std::pow(10.0, plottedRange.minimum) : plottedRange.minimum;
      const double upper = config.scale == ScaleMode::Log10
                               ? std::pow(10.0, plottedRange.maximum) : plottedRange.maximum;
      const QString limits = QStringLiteral("%1(%2, %3)")
          .arg(config.scale == ScaleMode::Log10 ? QStringLiteral("log10 ") : QString())
          .arg(lower, 0, 'g', 4).arg(upper, 0, 'g', 4);
      const QString secondLine = latestValue.isEmpty()
          ? limits : latestValue + QStringLiteral("  ") + limits;
      painter.drawText(QRectF(legend.left() + 5, legend.top() + 21,
                              legend.width() - 9, 18), Qt::AlignLeft | Qt::AlignTop,
                       painter.fontMetrics().elidedText(secondLine, Qt::ElideRight,
                                                         static_cast<int>(legend.width() - 9)));
    }
    if (legend.height() >= 52) {
      QString detail = config.units == "Undefined" ? QString()
                        : QString::fromStdString(config.units);
      if (!config.comment.empty()) {
        const QString comment = QString::fromStdString(config.comment);
        const QString combined = detail.isEmpty() ? comment
            : detail + QStringLiteral(" · ") + comment;
        detail = painter.fontMetrics().horizontalAdvance(combined) <= legend.width() - 9
                     ? combined : comment;
      }
      painter.drawText(QRectF(legend.left() + 5, legend.top() + 38,
                              legend.width() - 9, 16), Qt::AlignLeft | Qt::AlignTop,
                       painter.fontMetrics().elidedText(detail, Qt::ElideRight,
                                                         static_cast<int>(legend.width() - 9)));
    }
  }
  painter.setPen(foreground);
  painter.drawRect(area);

  const QColor grid = color(model_.colors.grid);
  painter.setPen(QPen(grid, 1, Qt::DashLine));
  const int xDivisions = model_.graph.xGrid == GridMode::All ? 10 : 5;
  const int yDivisions = model_.graph.yGrid == GridMode::All ? 10 : 5;
  if (model_.graph.xGrid != GridMode::None)
    for (int i = 1; i < xDivisions; ++i)
      painter.drawLine(QPointF(area.left() + area.width() * i / xDivisions, area.top()),
                       QPointF(area.left() + area.width() * i / xDivisions, area.bottom()));
  if (model_.graph.yGrid != GridMode::None)
    for (int i = 1; i < yDivisions; ++i)
      painter.drawLine(QPointF(area.left(), area.top() + area.height() * i / yDivisions),
                       QPointF(area.right(), area.top() + area.height() * i / yDivisions));

  painter.setPen(foreground);
  const qint64 begin = milliseconds(visibleTimeRange_.start);
  const qint64 end = milliseconds(visibleTimeRange_.end);
  const bool showMilliseconds = end - begin < 10000;
  const int labelWidth = showMilliseconds ? 106 : 76;
  const int timeDivisions = std::clamp(int(area.width() / (labelWidth + 12)), 1, 5);
  for (int i = 0; i <= timeDivisions; ++i) {
    const qint64 time = begin + (end - begin) * i / timeDivisions;
    const QString clockLabel = QDateTime::fromMSecsSinceEpoch(time).toString(
        showMilliseconds ? QStringLiteral("HH:mm:ss.zzz") : QStringLiteral("HH:mm:ss"));
    const double minutesFromEnd = -double(end - begin) / 60000.0 *
        (timeDivisions - i) / timeDivisions;
    const QString minuteLabel = i == timeDivisions
        ? QStringLiteral("0") : QString::number(minutesFromEnd, 'g', 6);
    const qreal x = area.left() + area.width() * i / timeDivisions;
    painter.drawText(QRectF(x - labelWidth / 2.0, area.bottom() + 5,
                            labelWidth, 17), Qt::AlignHCenter, minuteLabel);
    painter.drawText(QRectF(x - labelWidth / 2.0, area.bottom() + 22,
                            labelWidth, 19), Qt::AlignHCenter, clockLabel);
  }
  painter.drawText(QRectF(area.left(), area.bottom() + 42, 230, 20),
                   Qt::AlignLeft, tr("Minutes relative to right edge"));
  painter.drawText(QRectF(area.right() - 160, area.bottom() + 42, 160, 20),
                   Qt::AlignRight,
                   QDateTime::fromMSecsSinceEpoch(milliseconds(visibleTimeRange_.end))
                       .toString(QStringLiteral("MMM d, yyyy")));

  const int axisCurve = selectedCurve_ >= 0 ? selectedCurve_ :
                        (visibleCurves.empty() ? -1 : static_cast<int>(visibleCurves.front()));
  auto drawOrder = visibleCurves;
  if (selectedCurve_ >= 0) {
    const auto selected = std::find(drawOrder.begin(), drawOrder.end(),
                                    static_cast<std::size_t>(selectedCurve_));
    if (selected != drawOrder.end()) {
      drawOrder.erase(selected);
      drawOrder.push_back(static_cast<std::size_t>(selectedCurve_));
    }
  }
  for (const std::size_t curveIndex : drawOrder) {
    const auto& config = model_.curves[curveIndex];
    if (!config.nameSet || !config.plotted) continue;
    const auto range = valueRange(curveIndex);
    const QColor curveColor = color(model_.colors.curves[curveIndex]);
    if (static_cast<int>(curveIndex) == axisCurve) {
      painter.setPen(model_.graph.coloredYAxis ? curveColor : foreground);
      const QStringList labels = yAxisLabels(curveIndex);
      constexpr qreal unitsLaneWidth = 22;
      constexpr qreal labelGap = 8;
      const qreal labelX = unitsLaneWidth + labelGap;
      const qreal labelWidth = area.left() - labelX - labelGap;
      for (int tick = 0; tick <= 5; ++tick) {
        painter.drawText(QRectF(labelX, area.top() + area.height() * tick / 5.0 - 9,
                                labelWidth, 18), Qt::AlignRight, labels[tick]);
      }
      painter.save();
      painter.translate(unitsLaneWidth / 2.0, area.center().y());
      painter.rotate(-90);
      painter.drawText(QRectF(-area.height() / 2.0, -unitsLaneWidth / 2.0,
                              area.height(), unitsLaneWidth),
                       Qt::AlignCenter, QString::fromStdString(config.units));
      painter.restore();
    }

    const auto traceEnd = std::min(visibleTimeRange_.end,
                                   std::chrono::system_clock::now());
    auto selected = selectSamples(samples_[curveIndex], visibleTimeRange_.start,
                                  traceEnd,
                                  std::max(2, int(area.width()) * 2), config.scale);
    const auto& source = samples_[curveIndex];
    const auto first = std::lower_bound(source.begin(), source.end(),
                                        visibleTimeRange_.start,
        [](const Sample& sample, const auto& time) { return sample.timestamp < time; });
    if (traceEnd >= visibleTimeRange_.start && first != source.begin())
      selected.insert(selected.begin(), *std::prev(first));
    QPainterPath path;
    bool started = false;
    for (const auto& sample : selected) {
      const auto point = mapSample(sample, range, config.scale);
      if (!point) {
        if (started) {
          const qint64 sampleTime = milliseconds(sample.timestamp);
          const qint64 rangeStart = milliseconds(visibleTimeRange_.start);
          const qint64 rangeEnd = milliseconds(visibleTimeRange_.end);
          if (rangeEnd > rangeStart) {
            const qreal x = area.left() + area.width() *
                double(sampleTime - rangeStart) / double(rangeEnd - rangeStart);
            path.lineTo(x, path.currentPosition().y());
          }
        }
        started = false;
        continue;
      }
      if (!started) { path.moveTo(*point); started = true; }
      else {
        // Channel values are piecewise constant between monitor updates.
        path.lineTo(point->x(), path.currentPosition().y());
        path.lineTo(*point);
      }
    }
    if (started && !selected.empty() && selected.back().plotable) {
      const qint64 rangeStart = milliseconds(visibleTimeRange_.start);
      const qint64 rangeEnd = milliseconds(visibleTimeRange_.end);
      const qint64 heldEnd = milliseconds(traceEnd);
      if (rangeEnd > rangeStart) {
        const qreal x = area.left() + area.width() *
            double(heldEnd - rangeStart) / double(rangeEnd - rangeStart);
        path.lineTo(x, path.currentPosition().y());
      }
    }
    painter.setPen(QPen(curveColor, std::max(1, model_.graph.lineWidth)));
    painter.save();
    painter.setClipRect(area);
    painter.drawPath(path);
    painter.restore();
  }

  if (area.contains(cursorPosition_)) {
    painter.setPen(QPen(foreground, 1, Qt::DotLine));
    painter.drawLine(QPointF(cursorPosition_.x(), area.top()),
                     QPointF(cursorPosition_.x(), area.bottom()));
    painter.drawLine(QPointF(area.left(), cursorPosition_.y()),
                     QPointF(area.right(), cursorPosition_.y()));
  }

  for (std::size_t i = 0; i < model_.annotations.size(); ++i) {
    const auto& annotation = model_.annotations[i];
    const QRectF box = annotationRect(i);
    if (box.isEmpty()) continue;
    painter.fillRect(box, color(model_.colors.background));
    const QColor border = annotation.curveIndex &&
                                  *annotation.curveIndex < kMaximumCurves
                              ? color(model_.colors.curves[*annotation.curveIndex])
                              : foreground;
    painter.setPen(QPen(i == static_cast<std::size_t>(selectedAnnotation_)
                            ? foreground : border, 1,
                        i == static_cast<std::size_t>(selectedAnnotation_)
                            ? Qt::DashLine : Qt::SolidLine));
    painter.drawRect(box);
    painter.setPen(foreground);
    painter.drawText(box.adjusted(6, 4, -6, -4), Qt::TextWordWrap,
                     QString::fromStdString(annotation.text));
  }

}

void PlotWidget::mouseMoveEvent(QMouseEvent* event) {
  if ((dragMode_ == DragMode::Pan && !(event->buttons() & Qt::LeftButton)) ||
      (dragMode_ == DragMode::Annotation && !(event->buttons() & Qt::MiddleButton)))
    finishDrag();
  cursorPosition_ = event->pos();
  updateDrag(event->pos());
  QString legendTip;
  const auto curves = plottedCurves();
  for (std::size_t row = 0; row < curves.size(); ++row) {
    if (!legendRect(row).contains(event->pos())) continue;
    const auto& curve = model_.curves[curves[row]];
    legendTip = QString::fromStdString(curve.name);
    if (curve.units != "Undefined" && !curve.units.empty()) {
      legendTip += QLatin1Char('\n');
      legendTip += QString::fromStdString(curve.units);
    }
    if (!curve.comment.empty()) {
      legendTip += QLatin1Char('\n');
      legendTip += QString::fromStdString(curve.comment);
    }
    break;
  }
  if (toolTip() != legendTip) setToolTip(legendTip);
  const QRectF area = plotRect();
  if (area.contains(event->pos())) {
    const qint64 begin = milliseconds(visibleTimeRange_.start);
    const qint64 end = milliseconds(visibleTimeRange_.end);
    const qint64 time = begin + qint64((end - begin) *
        (event->pos().x() - area.left()) / area.width());
    int curveIndex = selectedCurve_;
    if (curveIndex < 0) {
      const auto curves = plottedCurves();
      if (!curves.empty()) curveIndex = static_cast<int>(curves.front());
    }
    double value = std::numeric_limits<double>::quiet_NaN();
    if (curveIndex >= 0) {
      const auto range = valueRange(static_cast<std::size_t>(curveIndex));
      const double plotted = range.maximum -
          (event->pos().y() - area.top()) / area.height() *
              (range.maximum - range.minimum);
      value = model_.curves[static_cast<std::size_t>(curveIndex)].scale == ScaleMode::Log10
                  ? std::pow(10.0, plotted) : plotted;
    }
    emit cursorLocationChanged(QDateTime::fromMSecsSinceEpoch(time), value, curveIndex);
  }
  update();
}

void PlotWidget::updateDrag(const QPoint& position) {
  const QRectF area = plotRect();
  if (area.width() > 0) {
    if (dragMode_ == DragMode::Annotation && selectedAnnotation_ >= 0) {
      const qreal targetX = std::clamp(dragAnnotationRect_.left() +
                                          position.x() - dragStart_.x(),
                                      area.left(), area.right() - dragAnnotationRect_.width());
      const qreal targetY = std::clamp(dragAnnotationRect_.top() +
                                          position.y() - dragStart_.y(),
                                      area.top(), area.bottom() - dragAnnotationRect_.height());
      auto& annotation = model_.annotations[static_cast<std::size_t>(selectedAnnotation_)];
      const auto duration = visibleTimeRange_.end - visibleTimeRange_.start;
      const auto shift = std::chrono::duration_cast<std::chrono::system_clock::duration>(
          std::chrono::duration<double>(std::chrono::duration<double>(duration).count() *
                                        (targetX - dragAnnotationRect_.left()) / area.width()));
      annotation.time = dragAnnotation_.time + shift;
      if (dragAnnotation_.value) {
        const auto curves = plottedCurves();
        if (!curves.empty()) {
          const std::size_t index = dragAnnotation_.curveIndex &&
                                          *dragAnnotation_.curveIndex < kMaximumCurves &&
                                          model_.curves[*dragAnnotation_.curveIndex].nameSet &&
                                          model_.curves[*dragAnnotation_.curveIndex].plotted
                                        ? *dragAnnotation_.curveIndex : curves.front();
          const ValueRange range = valueRange(index);
          const double initial = plotValue(*dragAnnotation_.value,
                                           model_.curves[index].scale);
          const double moved = initial -
              (targetY - dragAnnotationRect_.top()) / area.height() *
                  (range.maximum - range.minimum);
          const double value = model_.curves[index].scale == ScaleMode::Log10
                                   ? std::pow(10.0, moved) : moved;
          if (std::isfinite(value)) annotation.value = value;
        }
      }
      emit annotationsChanged();
    } else if (dragMode_ == DragMode::Pan) {
      if (position.x() != dragStart_.x()) panMoved_ = true;
      if (panMoved_) {
        const double fraction = -double(position.x() - dragStart_.x()) / area.width();
        const auto duration = dragRange_.end - dragRange_.start;
        const auto shift = std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::duration<double>(std::chrono::duration<double>(duration).count() * fraction));
        visibleTimeRange_ = {dragRange_.start + shift, dragRange_.end + shift};
        setAutoScroll(false);
        updateAutoRange();
      }
    }
  }
}

void PlotWidget::mousePressEvent(QMouseEvent* event) {
  if (event->button() == Qt::RightButton) {
    // The native context-menu event follows this press (or release, depending
    // on the platform). Opening here as well can reopen the menu after dismissal.
    event->accept();
    return;
  }
  if (event->button() != Qt::LeftButton && event->button() != Qt::MiddleButton) return;
  setFocus();
  const auto curves = plottedCurves();
  if (event->button() == Qt::LeftButton) {
    for (std::size_t position = 0; position < curves.size(); ++position) {
      if (legendRect(position).contains(event->pos())) {
        selectedCurve_ = static_cast<int>(curves[position]);
        update();
        return;
      }
    }
  }
  if (!plotRect().contains(event->pos())) return;
  const int touched = annotationAt(event->pos());
  if (touched >= 0) {
    selectAnnotation(touched);
    if (event->button() == Qt::MiddleButton) {
      dragMode_ = DragMode::Annotation;
      dragStart_ = event->pos();
      dragAnnotation_ = model_.annotations[static_cast<std::size_t>(touched)];
      dragAnnotationRect_ = annotationRect(static_cast<std::size_t>(touched));
    }
  } else if (event->button() == Qt::LeftButton) {
    selectAnnotation(-1);
    dragMode_ = DragMode::Pan;
    dragStart_ = event->pos();
    dragRange_ = visibleTimeRange_;
    panMoved_ = false;
  }
}

void PlotWidget::mouseReleaseEvent(QMouseEvent* event) {
  if ((dragMode_ == DragMode::Annotation && event->button() == Qt::MiddleButton) ||
      (dragMode_ == DragMode::Pan && event->button() == Qt::LeftButton)) {
    updateDrag(event->pos());
    finishDrag();
  }
}

void PlotWidget::finishDrag() {
  if (dragMode_ == DragMode::None) return;
  const bool resumeAutoScroll = autoScroll_ && !paused_;
  dragMode_ = DragMode::None;
  panMoved_ = false;
  if (resumeAutoScroll) resetView();
  else {
    updateAutoRange();
    update();
  }
}

void PlotWidget::contextMenuEvent(QContextMenuEvent* event) {
  // Native right clicks and keyboard context requests are handled here.
  emit plotContextMenuRequested(event->globalPos(), event->pos());
  event->accept();
}

void PlotWidget::mouseDoubleClickEvent(QMouseEvent* event) {
  if (event->button() != Qt::LeftButton || !plotRect().contains(event->pos())) return;
  dragMode_ = DragMode::None;
  const int touched = annotationAt(event->pos());
  if (touched >= 0) {
    selectAnnotation(touched);
    editSelectedAnnotation();
    return;
  }
  bool accepted = false;
  const QString text = QInputDialog::getText(
      this, tr("Plot Annotation"), tr("Text:"), QLineEdit::Normal, {}, &accepted);
  if (accepted && !text.isEmpty()) addAnnotationAt(event->pos(), text);
}

void PlotWidget::wheelEvent(QWheelEvent* event) {
  const int verticalDelta = event->angleDelta().y();
  if (verticalDelta == 0) {
    event->ignore();
    return;
  }
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
  const QPointF position = event->position();
#else
  const QPointF position = event->posF();
#endif
  if (plotRect().contains(position))
    zoomAt(verticalDelta > 0 ? 0.8 : 1.25, position);
  else
    zoom(verticalDelta > 0 ? 0.8 : 1.25);
  event->accept();
}

void PlotWidget::keyPressEvent(QKeyEvent* event) {
  if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) &&
      selectedAnnotation_ >= 0) {
    removeAnnotation(selectedAnnotation_);
    event->accept();
    return;
  }
  QWidget::keyPressEvent(event);
}

}  // namespace striptool
