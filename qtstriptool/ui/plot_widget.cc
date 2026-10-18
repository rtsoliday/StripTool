#include "ui/plot_widget.h"

#include <QDateTime>
#include <QContextMenuEvent>
#include <QMouseEvent>
#include <QInputDialog>
#include <QKeyEvent>
#include <QPaintEngine>
#include <QPainter>
#include <QPainterPath>
#include <QFontMetrics>
#include <QEvent>
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

double epochSeconds(std::chrono::system_clock::time_point time) {
  return std::chrono::duration<double>(time.time_since_epoch()).count();
}

// Time points are 64-bit nanosecond counts, about ±292 years around 1970. A
// view within ±146 years also has a duration that fits, so zoom and pan steps
// that would leave that span are ignored rather than overflowing.
bool representableView(double startSeconds, double endSeconds) {
  constexpr double limit = 146.0 * 365.25 * 24 * 3600;
  return std::abs(startSeconds) < limit && std::abs(endSeconds) < limit;
}

bool labelsAreDistinct(const QStringList& labels) {
  for (int i = 1; i < labels.size(); ++i)
    if (labels[i] == labels[i - 1]) return false;
  return true;
}

QString fixedLabel(double value, int decimals) {
  QString label = QString::number(value, 'f', decimals);
  if (label.contains(QLatin1Char('.'))) {
    while (label.endsWith(QLatin1Char('0'))) label.chop(1);
    if (label.endsWith(QLatin1Char('.'))) label.chop(1);
  }
  return label == QStringLiteral("-0") ? QStringLiteral("0") : label;
}

double rangeValue(const ValueRange& range, double fraction) {
  const double span = range.maximum - range.minimum;
  return std::isfinite(span) ? range.maximum - span * fraction
      : range.maximum * (1.0 - fraction) + range.minimum * fraction;
}

double rangeOffset(const ValueRange& range, double value, double pixels) {
  const double offset = value - range.minimum;
  const double span = range.maximum - range.minimum;
  if (std::isfinite(offset) && std::isfinite(span)) {
    // Offsets above DBL_MAX / pixels overflow when scaled first; dividing
    // first is only used then, so ordinary coordinates stay bit-identical.
    const double scaled = pixels * offset;
    return std::isfinite(scaled) ? scaled / span : pixels * (offset / span);
  }
  return pixels * ((value / 2.0 - range.minimum / 2.0) /
                   (range.maximum / 2.0 - range.minimum / 2.0));
}

QStringList axisLabels(const ValueRange& range, ScaleMode scale, int divisions) {
  QStringList labels;
  if (!range.isValid() || divisions < 1) return labels;

  // Tick precision depends on the visible range, rather than the PV's value
  // precision. Use four significant digits of the largest magnitude, adding
  // more only for narrow zooms. Ordinary linear values use fixed notation;
  // exponents are kept for log axes and very large or small magnitudes.
  const double span = range.maximum - range.minimum;
  const double magnitude = std::max(std::abs(range.minimum), std::abs(range.maximum));
  const int exponent = magnitude > 0.0 ? int(std::floor(std::log10(magnitude))) : 0;
  const bool fixed = scale != ScaleMode::Log10 && exponent >= -3 && exponent < 6;
  for (int candidate = 4; candidate <= 17; ++candidate) {
    labels.clear();
    for (int tick = 0; tick <= divisions; ++tick) {
      double plotted = std::isfinite(span) ? range.maximum - span * tick / divisions
          : rangeValue(range, double(tick) / divisions);
      // A tick at a zero crossing can carry arithmetic residue such as 1e-17.
      const double zeroTolerance = std::isfinite(span) ? span * 1e-9
          : range.maximum * 1e-9 - range.minimum * 1e-9;
      if (std::abs(plotted) < zeroTolerance) plotted = 0.0;
      const double value = scale == ScaleMode::Log10 ? std::pow(10.0, plotted)
                                                     : plotted;
      labels.push_back(fixed ? fixedLabel(value, std::clamp(candidate - 1 - exponent, 0, 20))
                             : QString::number(value == 0.0 ? 0.0 : value, 'g', candidate));
    }
    if (labelsAreDistinct(labels)) return labels;
  }

  return labels;
}

}  // namespace

PlotWidget::PlotWidget(QWidget* parent) : QWidget(parent) {
  setObjectName(QStringLiteral("plotWidget"));
  updateMinimumSize();
  setMouseTracking(true);
  setFocusPolicy(Qt::StrongFocus);
  resetView();
}

void PlotWidget::setModel(const StripToolModel& model) {
  // Precision, scale or limits may have changed; size legend values afresh.
  widestLegendValue_.fill(QString());
  const bool timespanChanged = model_.timing.timespanSeconds != model.timing.timespanSeconds;
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
  updateMinimumSize();
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
  if (timespanChanged && autoScroll_ && !paused_) resetView();
  else updateAutoRange();
  redraw();
}

void PlotWidget::setCurveSamples(
    std::size_t curve, std::vector<Sample> samples,
    std::optional<std::chrono::system_clock::time_point> firstEvicted) {
  if (curve >= samples_.size()) return;
  // During a drag the ranges are kept as they were before these samples.
  if (dragMode_ != DragMode::None) resolveAutoRanges();
  const auto earlier = [](const auto& left, const auto& right) {
    return left.timestamp < right.timestamp;
  };
  if (!std::is_sorted(samples.begin(), samples.end(), earlier))
    std::sort(samples.begin(), samples.end(), earlier);
  liveSamples_[curve] = std::move(samples);
  firstEvicted_[curve] = firstEvicted;
  if (historicalSamples_[curve].empty()) {
    if (liveSamples_[curve].empty() || samples_[curve].capacity() / 2 > liveSamples_[curve].size())
      std::vector<Sample>(liveSamples_[curve]).swap(samples_[curve]);
    else samples_[curve] = liveSamples_[curve];
  } else samples_[curve] = joinedSamples(curve);
  if (autoScroll_ && !paused_ && dragMode_ == DragMode::None) {
    if (!samples_[curve].empty()) {
      const auto duration = visibleTimeRange_.end - visibleTimeRange_.start;
      const auto now = std::chrono::system_clock::now();
      if (visibleTimeRange_.end > now)
        visibleTimeRange_.end = now;
      else if (samples_[curve].back().timestamp <= now)
        visibleTimeRange_.end = std::max(visibleTimeRange_.end,
                                         samples_[curve].back().timestamp);
      visibleTimeRange_.start = visibleTimeRange_.end - duration;
    }
  }
  if (dragMode_ == DragMode::None) updateAutoRange();
  redraw();
}

void PlotWidget::appendSample(std::size_t curve, Sample sample) {
  if (curve >= samples_.size()) return;
  // During a drag the ranges are kept as they were before these samples.
  if (dragMode_ != DragMode::None) resolveAutoRanges();
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
  if (data.size() > maximum) {
    if (!firstEvicted_[curve]) firstEvicted_[curve] = data.front().timestamp;
    data.erase(data.begin(), data.end() - static_cast<std::ptrdiff_t>(maximum));
  }
  samples_[curve] = joinedSamples(curve);
  if (autoScroll_ && !paused_ && dragMode_ == DragMode::None) {
    const auto duration = visibleTimeRange_.end - visibleTimeRange_.start;
    const auto now = std::chrono::system_clock::now();
    if (visibleTimeRange_.end > now)
      visibleTimeRange_.end = now;
    else if (sample.timestamp <= now)
      visibleTimeRange_.end = std::max(visibleTimeRange_.end, sample.timestamp);
    visibleTimeRange_.start = visibleTimeRange_.end - duration;
  }
  if (dragMode_ == DragMode::None) updateAutoRange();
  redraw();
}

void PlotWidget::clearCurveSamples(std::size_t curve) {
  if (curve >= samples_.size()) return;
  std::vector<Sample>().swap(liveSamples_[curve]);
  std::vector<Sample>().swap(historicalSamples_[curve]);
  firstEvicted_[curve].reset();
  widestLegendValue_[curve].clear();
  std::vector<Sample>().swap(samples_[curve]);
  if (historyReduced_[curve]) {
    historyReduced_[curve] = false;
    emit historyReductionChanged();
  }
  automaticRanges_[curve].reset();
  automaticRangeStale_[curve] = false;
  verticalRanges_[curve].reset();
  redraw();
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
  // During a drag the ranges are kept as they were before these samples.
  if (dragMode_ != DragMode::None) resolveAutoRanges();
  auto ordered = samples;
  std::sort(ordered.begin(), ordered.end(), [](const auto& left, const auto& right) {
    return left.timestamp < right.timestamp;
  });
  const auto maximum = std::min(kMaximumHistorySamples, kHistoryMemoryLimit / sizeof(Sample));
  const bool reduceIncoming = ordered.size() > maximum;
  if (reduceIncoming) ordered = decimateSamples(ordered, maximum);
  auto merged = joinHistoricalAndLive(historicalSamples_[curve], ordered);
  const bool reduced = reduceIncoming || merged.size() > maximum;
  if (merged.size() > maximum) merged = decimateSamples(merged, maximum);
  else if (merged.capacity() > maximum) std::vector<Sample>(merged).swap(merged);
  historicalSamples_[curve] = std::move(merged);
  samples_[curve] = joinedSamples(curve);
  if (reduced && !historyReduced_[curve]) {
    historyReduced_[curve] = true;
    emit historyReductionChanged();
  }
  if (dragMode_ == DragMode::None) updateAutoRange();
  redraw();
}

std::vector<Sample> PlotWidget::joinedSamples(std::size_t curve) const {
  const auto& historical = historicalSamples_[curve];
  const auto& live = liveSamples_[curve];
  const auto& evicted = firstEvicted_[curve];
  if (!evicted || historical.empty()) return joinHistoricalAndLive(historical, live);
  // Live samples from the first evicted one up to the oldest retained one were
  // discarded. Break the trace there so an archived value is not held across
  // a span in which newer data existed.
  const auto at = std::lower_bound(live.begin(), live.end(), *evicted,
      [](const Sample& sample, const auto& time) { return sample.timestamp < time; });
  std::vector<Sample> marked;
  marked.reserve(live.size() + 1);
  marked.insert(marked.end(), live.begin(), at);
  marked.push_back({*evicted, 0.0, 0, 0, false});
  marked.insert(marked.end(), at, live.end());
  return joinHistoricalAndLive(historical, marked);
}

bool PlotWidget::historyReduced(std::size_t curve) const {
  return curve < historyReduced_.size() && historyReduced_[curve];
}

std::size_t PlotWidget::retainedSampleBytes(std::size_t curve) const {
  if (curve >= samples_.size()) return 0;
  return (liveSamples_[curve].capacity() + historicalSamples_[curve].capacity() +
          samples_[curve].capacity()) * sizeof(Sample);
}

ValueRange PlotWidget::valueRange(std::size_t curve) const {
  if (curve >= kMaximumCurves) return {};
  if (verticalRanges_[curve]) return *verticalRanges_[curve];
  resolveAutoRange(curve);
  if (automaticRanges_[curve]) return *automaticRanges_[curve];
  const auto& config = model_.curves[curve];
  ValueRange range{plotValue(config.minimum, config.scale),
                   plotValue(config.maximum, config.scale)};
  return range.isValid() ? range : ValueRange{};
}

QStringList PlotWidget::yAxisLabels(std::size_t curve, int divisions) const {
  if (curve >= kMaximumCurves) return {};
  return axisLabels(valueRange(curve), model_.curves[curve].scale, divisions);
}

TimeAxisLabels PlotWidget::xAxisLabels(int divisions) const {
  TimeAxisLabels labels;
  if (divisions < 1) return labels;
  const qint64 begin = milliseconds(visibleTimeRange_.start);
  const qint64 end = milliseconds(visibleTimeRange_.end);
  const qint64 span = end - begin;
  const double unit = span >= 86400000 ? 86400000.0 : span >= 3600000 ? 3600000.0
                      : span >= 60000 ? 60000.0 : 1000.0;
  labels.caption = span >= 86400000 ? tr("Days relative to right edge")
                   : span >= 3600000 ? tr("Hours relative to right edge")
                   : span >= 60000 ? tr("Minutes relative to right edge")
                   : tr("Seconds relative to right edge");
  const QString format = span >= 86400000 ? QStringLiteral("MMM d HH:mm")
                         : span >= 3600000 ? QStringLiteral("HH:mm")
                         : span < 10000 ? QStringLiteral("HH:mm:ss.zzz")
                         : QStringLiteral("HH:mm:ss");
  for (int i = 0; i <= divisions; ++i) {
    const qint64 time = begin + span * i / divisions;
    labels.relative.push_back(i == divisions ? QStringLiteral("0")
        : QString::number(-double(span) / unit * (divisions - i) / divisions, 'g', 4));
    labels.clock.push_back(QDateTime::fromMSecsSinceEpoch(time).toString(format));
  }
  labels.date = QDateTime::fromMSecsSinceEpoch(end).toString(QStringLiteral("MMM d, yyyy"));
  return labels;
}

void PlotWidget::setAutoScroll(bool enabled) {
  const bool changed = autoScroll_ != enabled;
  autoScroll_ = enabled;
  if (changed) emit autoScrollChanged(enabled);
  if (enabled && !paused_) advanceToNow();
}

void PlotWidget::setPaused(bool paused) {
  paused_ = paused;
  if (!paused_ && autoScroll_) advanceToNow();
}

void PlotWidget::setVisibleTimeRange(TimeRange range) {
  if (!range.isValid() || range.start == range.end) return;
  visibleTimeRange_ = range;
  setAutoScroll(false);
  updateAutoRange();
  redraw();
  emit visibleTimeRangeChanged();
}

void PlotWidget::pan(double fractionOfWindow) {
  const auto duration = visibleTimeRange_.end - visibleTimeRange_.start;
  const double span = std::chrono::duration<double>(duration).count();
  const double start = epochSeconds(visibleTimeRange_.start) + span * fractionOfWindow;
  if (!representableView(start, start + span)) return;
  const auto shift = std::chrono::duration_cast<std::chrono::system_clock::duration>(
      std::chrono::duration<double>(
          std::chrono::duration<double>(duration).count() * fractionOfWindow));
  visibleTimeRange_.start += shift;
  visibleTimeRange_.end += shift;
  setAutoScroll(false);
  updateAutoRange();
  redraw();
  emit visibleTimeRangeChanged();
}

void PlotWidget::zoom(double factor) {
  const QRectF area = plotRect();
  // Toolbar zoom in a live view changes its duration and keeps now at the
  // right edge. Wheel zoom remains anchored at the pointer in a fixed view.
  zoomAt(factor, autoScroll_ ? QPointF(area.right(), area.center().y()) : area.center(),
         autoScroll_);
}

void PlotWidget::zoomAt(double factor, const QPointF& position, bool keepAutoScroll) {
  if (!(factor > 0.0) || !std::isfinite(factor)) return;
  const QRectF area = plotRect();
  if (area.width() <= 0.0) return;
  const double fraction = std::clamp(
      (position.x() - area.left()) / area.width(), 0.0, 1.0);
  const auto duration = visibleTimeRange_.end - visibleTimeRange_.start;
  const double span = std::chrono::duration<double>(duration).count();
  const double start = epochSeconds(visibleTimeRange_.start) + span * fraction * (1.0 - factor);
  if (!representableView(start, start + span * factor)) return;
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
  if (!keepAutoScroll) setAutoScroll(false);
  updateAutoRange();
  redraw();
  emit visibleTimeRangeChanged();
}

void PlotWidget::panY(double fractionOfRange) {
  if (!std::isfinite(fractionOfRange)) return;
  if (autoScaleEnabled_) {
    resolveAutoRanges();
    autoScaleEnabled_ = false;
    emit autoScaleChanged(false);
  }
  for (const auto index : plottedCurves()) {
    const ValueRange range = valueRange(index);
    if (!range.isValid()) continue;
    // Halve first only when the span overflows; doubling the fraction rather
    // than the halved span keeps that product finite.
    const double span = range.maximum - range.minimum;
    const double shift = std::isfinite(span) ? span * fractionOfRange
        : (range.maximum / 2.0 - range.minimum / 2.0) * (2.0 * fractionOfRange);
    const ValueRange moved{range.minimum + shift, range.maximum + shift};
    if (std::isfinite(moved.minimum) && std::isfinite(moved.maximum) &&
        moved.isValid()) verticalRanges_[index] = moved;
  }
  redraw();
}

void PlotWidget::zoomY(double factor) {
  if (!std::isfinite(factor) || factor <= 0.0) return;
  if (autoScaleEnabled_) {
    resolveAutoRanges();
    autoScaleEnabled_ = false;
    emit autoScaleChanged(false);
  }
  for (const auto index : plottedCurves()) {
    const ValueRange range = valueRange(index);
    if (!range.isValid()) continue;
    const double center = range.minimum / 2.0 + range.maximum / 2.0;
    // Halve first only when the span overflows, keeping tiny spans exact.
    const double span = range.maximum - range.minimum;
    const double half = std::isfinite(span) ? span * factor / 2.0
        : (range.maximum / 2.0 - range.minimum / 2.0) * factor;
    const ValueRange zoomed{center - half, center + half};
    if (std::isfinite(zoomed.minimum) && std::isfinite(zoomed.maximum) &&
        zoomed.isValid()) verticalRanges_[index] = zoomed;
  }
  redraw();
}

void PlotWidget::resetVerticalView() {
  const bool changed = autoScaleEnabled_;
  autoScaleEnabled_ = false;
  for (std::size_t i = 0; i < kMaximumCurves; ++i) {
    verticalRanges_[i].reset();
    automaticRanges_[i].reset();
    automaticRangeStale_[i] = false;
  }
  if (changed) emit autoScaleChanged(false);
  redraw();
}

void PlotWidget::resetView() {
  visibleTimeRange_.end = std::chrono::system_clock::now();
  visibleTimeRange_.start = visibleTimeRange_.end -
      std::chrono::seconds(model_.timing.timespanSeconds);
  const bool changed = !autoScroll_;
  autoScroll_ = true;
  if (changed) emit autoScrollChanged(true);
  updateAutoRange();
  redraw();
  emit visibleTimeRangeChanged();
}

void PlotWidget::advanceToNow() {
  if (!autoScroll_ || paused_ || dragMode_ != DragMode::None) return;
  const auto duration = visibleTimeRange_.end - visibleTimeRange_.start;
  visibleTimeRange_.end = std::chrono::system_clock::now();
  visibleTimeRange_.start = visibleTimeRange_.end - duration;
  updateAutoRange();
  redraw();
}

void PlotWidget::autoScale() {
  const bool changed = !autoScaleEnabled_;
  autoScaleEnabled_ = true;
  verticalRanges_.fill(std::nullopt);
  updateAutoRange();
  if (changed) emit autoScaleChanged(true);
  redraw();
}

void PlotWidget::replot() { redraw(); }

int PlotWidget::addAnnotation(Annotation annotation) {
  model_.annotations.push_back(std::move(annotation));
  selectedAnnotation_ = static_cast<int>(model_.annotations.size()) - 1;
  emit annotationSelectionChanged(selectedAnnotation_);
  emit annotationsChanged();
  redraw();
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
  redraw();
  return true;
}

bool PlotWidget::removeAnnotation(int index) {
  if (index < 0 || index >= static_cast<int>(model_.annotations.size())) return false;
  model_.annotations.erase(model_.annotations.begin() + index);
  selectedAnnotation_ = -1;
  emit annotationSelectionChanged(-1);
  emit annotationsChanged();
  redraw();
  return true;
}

void PlotWidget::selectAnnotation(int index) {
  if (index < -1 || index >= static_cast<int>(model_.annotations.size())) return;
  selectedAnnotation_ = index;
  emit annotationSelectionChanged(index);
  redraw();
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
  const QFontMetrics metrics(font(), this);
  const int lineHeight = std::max(metrics.height(), metrics.lineSpacing());
  const int unitsLaneWidth = std::max(22, metrics.height() + 8);
  constexpr int labelGap = 8;
  int leftMargin = 72;
  const int topMargin = model_.title.empty() ? std::max(20, lineHeight / 2 + 8)
                                            : lineHeight + 18;
  const int bottomMargin = 3 * lineHeight + lineHeight / 2 + 16;
  const auto curves = plottedCurves();
  const int axisCurve = selectedCurve_ >= 0 ? selectedCurve_ :
                        (curves.empty() ? -1 : static_cast<int>(curves.front()));
  if (axisCurve >= 0) {
    int labelWidth = 0;
    const int divisions = valueDivisionsForHeight(height() - topMargin - bottomMargin);
    for (const QString& label : yAxisLabels(static_cast<std::size_t>(axisCurve), divisions))
      labelWidth = std::max(labelWidth, metrics.horizontalAdvance(label));
    leftMargin = std::max(leftMargin, unitsLaneWidth + labelGap + labelWidth + labelGap);
  }
  int timeLabelWidth = 0;
  const auto timeLabels = xAxisLabels();
  for (const auto& labels : {timeLabels.relative, timeLabels.clock})
    for (const QString& label : labels)
      timeLabelWidth = std::max(timeLabelWidth, metrics.horizontalAdvance(label));
  leftMargin = std::max(leftMargin, timeLabelWidth / 2 + labelGap);

  int nameWidth = 178;
  int valueWidth = 0;
  for (const auto curve : curves) {
    const QStringList lines = legendLines(curve);
    nameWidth = std::max(nameWidth, metrics.horizontalAdvance(lines[0]) + 40);
    // The value line never shrinks while a curve is configured: it keeps the
    // widest text shown so far, starting from the widest value the precision
    // allows. Otherwise each update that changes its width would rescale the
    // time axis and shift the whole trace.
    auto& widest = widestLegendValue_[curve];
    int widestWidth = metrics.horizontalAdvance(widest);
    for (const QString& line : {legendLines(curve, true)[1], lines[1]}) {
      const int lineWidth = metrics.horizontalAdvance(line);
      if (lineWidth > widestWidth) {
        widest = line;
        widestWidth = lineWidth;
      }
    }
    valueWidth = std::max(valueWidth, widestWidth + 26);
  }
  // Long names can be elided with a full tooltip, so their space is capped to
  // leave room for the traces. The value and limits are always shown in full.
  const int traceRoom = width() - leftMargin - 200;
  const int rightMargin = std::max({timeLabelWidth / 2 + labelGap,
      std::min(nameWidth, std::max(178, std::min(width() * 2 / 5, traceRoom))),
      std::min(valueWidth, traceRoom)});
  return rect().adjusted(leftMargin, topMargin, -rightMargin, -bottomMargin);
}

int PlotWidget::timeAxisDivisions() const {
  return timeAxisDivisions(plotRect());
}

int PlotWidget::timeAxisDivisions(const QRectF& area) const {
  const QFontMetrics metrics(font(), this);
  // Labels are centred on their ticks; plotRect() reserves half of the widest
  // label beyond each end of the axis.
  const auto fits = [&metrics, &area](const QStringList& labels) {
    qreal previousRight = -std::numeric_limits<qreal>::infinity();
    const int divisions = labels.size() - 1;
    for (int i = 0; i <= divisions; ++i) {
      const int width = metrics.horizontalAdvance(labels[i]);
      const qreal left = area.left() + area.width() * i / divisions - width / 2.0;
      if (left < previousRight + 12) return false;
      previousRight = left + width;
    }
    return true;
  };
  for (int divisions = 5; divisions > 1; --divisions) {
    const auto labels = xAxisLabels(divisions);
    if (fits(labels.relative) && fits(labels.clock)) return divisions;
  }
  return 1;
}

int PlotWidget::valueAxisDivisions() const {
  return valueDivisionsForHeight(plotRect().height());
}

int PlotWidget::valueDivisionsForHeight(qreal plotHeight) const {
  const QFontMetrics metrics(font(), this);
  const int lineHeight = std::max(metrics.height(), metrics.lineSpacing());
  return std::clamp(int(plotHeight / (lineHeight + 6)), 1, 5);
}

std::vector<std::size_t> PlotWidget::plottedCurves() const {
  std::vector<std::size_t> result;
  for (std::size_t i = 0; i < kMaximumCurves; ++i)
    if (model_.curves[i].nameSet && model_.curves[i].plotted) result.push_back(i);
  return result;
}

QRectF PlotWidget::legendRect(std::size_t position) const {
  return legendRect(position, plotRect());
}

QRectF PlotWidget::legendRect(std::size_t position, const QRectF& area) const {
  const QFontMetrics metrics(font(), this);
  const int lineHeight = std::max(metrics.height(), metrics.lineSpacing());
  const qreal rowHeight = std::min<qreal>(3 * lineHeight + 8, (area.bottom() - 8) /
                                                std::max<std::size_t>(1, plottedCurves().size()));
  return QRectF(area.right() + 10, 8 + position * rowHeight,
                width() - area.right() - 16, rowHeight - 2);
}

QStringList PlotWidget::legendLines(std::size_t curve, bool widestValue) const {
  const auto& config = model_.curves[curve];
  // EPICS PREC counts decimal places (0 by default), so never show fewer than
  // the legacy legend's %g six significant digits; larger values add digits.
  const int digits = std::clamp(config.precision, 6, 15);
  QString latestValue;
  if (widestValue)
    latestValue = QStringLiteral("-.") + QString(digits, QLatin1Char('8'));
  else if (!samples_[curve].empty() && samples_[curve].back().plotable &&
           std::isfinite(samples_[curve].back().value))
    latestValue = QString::number(samples_[curve].back().value, 'g', digits);
  const ValueRange range = valueRange(curve);
  const double lower = config.scale == ScaleMode::Log10 ? std::pow(10.0, range.minimum)
                                                       : range.minimum;
  const double upper = config.scale == ScaleMode::Log10 ? std::pow(10.0, range.maximum)
                                                       : range.maximum;
  const QString limits = QStringLiteral("%1(%2, %3)")
      .arg(config.scale == ScaleMode::Log10 ? QStringLiteral("log10 ") : QString())
      .arg(lower, 0, 'g', 4).arg(upper, 0, 'g', 4);
  QString detail = config.units == "Undefined" ? QString() : QString::fromStdString(config.units);
  if (!config.comment.empty()) {
    if (!detail.isEmpty()) detail += QStringLiteral(" · ");
    detail += QString::fromStdString(config.comment);
  }
  return {QString::fromStdString(config.name),
          latestValue.isEmpty() ? limits : latestValue + QStringLiteral("  ") + limits,
          detail};
}

void PlotWidget::updateMinimumSize() {
  const QFontMetrics metrics(font(), this);
  const int lineHeight = std::max(metrics.height(), metrics.lineSpacing());
  // Every active curve gets a name and value row, even with a large desktop font.
  const int footerHeight = 3 * lineHeight + lineHeight / 2 + 16;
  const int topHeight = model_.title.empty() ? std::max(20, lineHeight / 2 + 8)
                                            : lineHeight + 18;
  const int legendHeight = int(plottedCurves().size()) * (2 * lineHeight + 8) + 16;
  setMinimumSize(800, std::max(260, topHeight + footerHeight + legendHeight));
}

void PlotWidget::changeEvent(QEvent* event) {
  QWidget::changeEvent(event);
  if (event->type() == QEvent::FontChange || event->type() == QEvent::ApplicationFontChange) {
    updateMinimumSize();
    redraw();
  }
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
  const double plotted = rangeValue(range, (position.y() - area.top()) / area.height());
  const double value = model_.curves[index].scale == ScaleMode::Log10
                           ? std::pow(10.0, plotted) : plotted;
  return std::isfinite(value) ? std::optional<double>(value) : std::nullopt;
}

QRectF PlotWidget::annotationRect(std::size_t index) const {
  return annotationRect(index, plotRect());
}

QRectF PlotWidget::annotationRect(std::size_t index, const QRectF& area) const {
  if (index >= model_.annotations.size()) return {};
  const auto& annotation = model_.annotations[index];
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
      y = area.bottom() - rangeOffset(range, plotted, area.height());
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
  if (model_.annotations.empty()) return -1;
  const QRectF area = plotRect();
  for (int i = static_cast<int>(model_.annotations.size()) - 1; i >= 0; --i)
    if (annotationRect(static_cast<std::size_t>(i), area).contains(position)) return i;
  return -1;
}

void PlotWidget::updateAutoRange() {
  // A live refresh updates every curve before painting; scan each only once.
  automaticRangeStale_.fill(true);
}

void PlotWidget::resolveAutoRange(std::size_t curve) const {
  if (!automaticRangeStale_[curve]) return;
  automaticRangeStale_[curve] = false;
  if (autoScaleEnabled_) {
    automaticRanges_[curve] = visibleSampleValueRange(
        samples_[curve], visibleTimeRange_.start, visibleTimeRange_.end,
        model_.curves[curve].scale);
  } else {
    automaticRanges_[curve].reset();
  }
}

void PlotWidget::resolveAutoRanges() const {
  for (std::size_t i = 0; i < kMaximumCurves; ++i) resolveAutoRange(i);
}

void PlotWidget::redraw() {
  plotCacheValid_ = false;
  update();
}

QColor PlotWidget::color(const Rgba16& value) const {
  return QColor::fromRgbF(value.red / 65535.0, value.green / 65535.0,
                          value.blue / 65535.0, value.alpha / 65535.0);
}

std::optional<QPointF> PlotWidget::mapSample(const Sample& sample,
                                            const ValueRange& range,
                                            ScaleMode scale,
                                            const QRectF& area,
                                            qint64 begin, qint64 end) const {
  const qint64 time = milliseconds(sample.timestamp);
  const double value = plotValue(sample.value, scale);
  if (end <= begin || !sample.plotable || !range.isValid() || !std::isfinite(value))
    return std::nullopt;
  // Off-scale sentinels such as 9.9e37 map far beyond what Qt's raster
  // engine draws correctly, or to infinity. The trace's steps are clipped to
  // the area, so holding them within one area height of it draws the same.
  return QPointF(area.left() + area.width() * double(time - begin) / double(end - begin),
                 std::clamp<qreal>(area.bottom() - rangeOffset(range, value, area.height()),
                                   area.top() - area.height(),
                                   area.bottom() + area.height()));
}

QRectF PlotWidget::currentPlotRect() const {
  return plotCacheValid_ && plotCacheSize_ == size() ? plotCacheArea_ : plotRect();
}

void PlotWidget::paintEvent(QPaintEvent*) {
  QPainter painter(this);
  QRectF area = currentPlotRect();
  // Moving the cursor repaints only the cursor and annotations over a cached
  // plot. The cache is used only where it is pixel-identical to drawing
  // directly; printing and scaled output draw everything afresh.
  const QPaintEngine* engine = painter.paintEngine();
  const qreal ratio = devicePixelRatioF();
  if (engine && engine->type() == QPaintEngine::Raster && engine->paintDevice() &&
      engine->paintDevice()->devicePixelRatioF() == ratio &&
      painter.transform().type() <= QTransform::TxTranslate) {
    const QSize pixels = (QSizeF(size()) * ratio).toSize();
    // Text is laid out with the widget's metrics, so draw it at the same DPI.
    const int dotsPerMeterX = qRound(logicalDpiX() / 0.0254);
    const int dotsPerMeterY = qRound(logicalDpiY() / 0.0254);
    if (plotCache_.size() != pixels || plotCache_.devicePixelRatio() != ratio ||
        plotCache_.dotsPerMeterX() != dotsPerMeterX ||
        plotCache_.dotsPerMeterY() != dotsPerMeterY) {
      if (plotCache_.size() != pixels)
        plotCache_ = QImage(pixels, QImage::Format_ARGB32_Premultiplied);
      plotCache_.setDevicePixelRatio(ratio);
      if (plotCacheValid_) area = plotRect();
      plotCacheValid_ = false;
    }
    if (!plotCacheValid_) {
      plotCache_.setDotsPerMeterX(dotsPerMeterX);
      plotCache_.setDotsPerMeterY(dotsPerMeterY);
      plotCache_.fill(Qt::transparent);
      QPainter cachePainter(&plotCache_);
      cachePainter.setFont(font());
      paintPlot(cachePainter, area);
      plotCacheArea_ = area;
      plotCacheSize_ = size();
      plotCacheValid_ = true;
    }
    painter.drawImage(QPointF(0, 0), plotCache_);
  } else {
    paintPlot(painter, area);
  }

  const QColor foreground = color(model_.colors.foreground);
  if (area.contains(cursorPosition_)) {
    painter.setPen(QPen(foreground, 1, Qt::DotLine));
    painter.drawLine(QPointF(cursorPosition_.x(), area.top()),
                     QPointF(cursorPosition_.x(), area.bottom()));
    painter.drawLine(QPointF(area.left(), cursorPosition_.y()),
                     QPointF(area.right(), cursorPosition_.y()));
  }

  for (std::size_t i = 0; i < model_.annotations.size(); ++i) {
    const auto& annotation = model_.annotations[i];
    const QRectF box = annotationRect(i, area);
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

void PlotWidget::paintPlot(QPainter& painter, const QRectF& area) {
  painter.fillRect(rect(), color(model_.colors.background));
  const QColor foreground = color(model_.colors.foreground);
  const auto visibleCurves = plottedCurves();
  const QFontMetrics metrics(font(), this);
  const int lineHeight = std::max(metrics.height(), metrics.lineSpacing());

  const QString title = QString::fromStdString(model_.title);
  if (!title.isEmpty()) {
    QFont titleFont = painter.font();
    titleFont.setBold(true);
    painter.setFont(titleFont);
    painter.setPen(foreground);
    painter.drawText(QRectF(area.left(), 6, area.width(), lineHeight),
                     Qt::AlignCenter,
                     painter.fontMetrics().elidedText(
                         title, Qt::ElideRight, static_cast<int>(area.width())));
    painter.setFont(font());
  }

  for (std::size_t position = 0; position < visibleCurves.size(); ++position) {
    const std::size_t index = visibleCurves[position];
    const QRectF legend = legendRect(position, area);
    if (selectedCurve_ == static_cast<int>(index)) {
      painter.setPen(QPen(foreground, 1));
      painter.drawRect(legend);
    }
    painter.setPen(QPen(color(model_.colors.curves[index]), 3));
    painter.drawLine(QPointF(legend.left() + 4, legend.top() + lineHeight / 2.0),
                     QPointF(legend.left() + 18, legend.top() + lineHeight / 2.0));
    const QStringList lines = legendLines(index);
    painter.setPen(foreground);
    const QRectF textArea(legend.left() + 23, legend.top(),
                          legend.width() - 26, lineHeight);
    painter.drawText(textArea, Qt::AlignVCenter | Qt::AlignLeft,
                     metrics.elidedText(lines[0], Qt::ElideRight,
                                                       static_cast<int>(textArea.width())));
    for (int row = 1; row < lines.size(); ++row) {
      if (legend.height() < (row + 1) * lineHeight) break;
      painter.drawText(QRectF(legend.left() + 5, legend.top() + row * lineHeight,
                              legend.width() - 9, lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
                       metrics.elidedText(lines[row], Qt::ElideRight,
                                          static_cast<int>(legend.width() - 9)));
    }
  }
  painter.setPen(foreground);
  painter.drawRect(area);

  const QColor grid = color(model_.colors.grid);
  painter.setPen(QPen(grid, 1, Qt::DashLine));
  // Grid lines fall on the labelled ticks; All adds a line midway between them.
  const int timeDivisions = timeAxisDivisions(area);
  const int valueDivisions = valueDivisionsForHeight(area.height());
  const int xDivisions = timeDivisions * (model_.graph.xGrid == GridMode::All ? 2 : 1);
  const int yDivisions = valueDivisions * (model_.graph.yGrid == GridMode::All ? 2 : 1);
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
  const TimeAxisLabels timeLabels = xAxisLabels(timeDivisions);
  int labelWidth = 0;
  for (const auto& labels : {timeLabels.relative, timeLabels.clock})
    for (const QString& label : labels)
      labelWidth = std::max(labelWidth, metrics.horizontalAdvance(label) + 8);
  const qreal timeLabelTop = area.bottom() + 5 + lineHeight / 2;
  for (int i = 0; i <= timeDivisions; ++i) {
    const qreal labelLeft = area.left() + area.width() * i / timeDivisions - labelWidth / 2.0;
    painter.drawText(QRectF(labelLeft, timeLabelTop, labelWidth, lineHeight),
                     Qt::AlignHCenter | Qt::AlignVCenter, timeLabels.relative[i]);
    painter.drawText(QRectF(labelLeft, timeLabelTop + lineHeight, labelWidth, lineHeight),
                     Qt::AlignHCenter | Qt::AlignVCenter, timeLabels.clock[i]);
  }
  const int dateWidth = metrics.horizontalAdvance(timeLabels.date) + 8;
  const qreal captionWidth = std::max<qreal>(0, area.width() - dateWidth - 8);
  painter.drawText(QRectF(area.left(), timeLabelTop + 2 * lineHeight,
                          captionWidth, lineHeight), Qt::AlignLeft | Qt::AlignVCenter,
                   metrics.elidedText(timeLabels.caption, Qt::ElideRight, int(captionWidth)));
  painter.drawText(QRectF(area.right() - dateWidth, timeLabelTop + 2 * lineHeight,
                          dateWidth, lineHeight), Qt::AlignRight | Qt::AlignVCenter,
                   timeLabels.date);

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
      const QStringList labels = yAxisLabels(curveIndex, valueDivisions);
      const qreal unitsLaneWidth = std::max(22, metrics.height() + 8);
      constexpr qreal labelGap = 8;
      const qreal labelX = unitsLaneWidth + labelGap;
      const qreal labelWidth = area.left() - labelX - labelGap;
      // An invalid range has no labels; leave the ticks unlabelled.
      for (int tick = 0; labels.size() == valueDivisions + 1 && tick <= valueDivisions; ++tick) {
        painter.drawText(QRectF(labelX, area.top() + area.height() * tick / valueDivisions - lineHeight / 2.0,
                                labelWidth, lineHeight), Qt::AlignRight | Qt::AlignVCenter,
                         labels[tick]);
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
    // One bucket per pixel column, sized from the whole visible duration so
    // it stays fixed while the trace scrolls.
    const auto bucket = (visibleTimeRange_.end - visibleTimeRange_.start) /
                        std::max(1, int(area.width()));
    auto selected = selectTraceSamples(samples_[curveIndex], visibleTimeRange_.start,
                                       traceEnd, bucket, config.scale);
    const auto& source = samples_[curveIndex];
    const auto first = std::lower_bound(source.begin(), source.end(),
                                        visibleTimeRange_.start,
        [](const Sample& sample, const auto& time) { return sample.timestamp < time; });
    if (traceEnd >= visibleTimeRange_.start && first != source.begin())
      selected.insert(selected.begin(), *std::prev(first));
    // A wide opaque trace is drawn as separate line segments, which the
    // raster engine fills directly; stroking it as one wide path costs
    // several times more. Their square caps fill each step's corner. One
    // pixel traces are already fast, and a translucent trace stays one path
    // so overlaps are not blended twice.
    const int lineWidth = std::max(1, model_.graph.lineWidth);
    const bool opaque = curveColor.alpha() == 255 && lineWidth > 1;
    QPainterPath path;
    QVector<QLineF> lines;
    if (opaque) lines.reserve(2 * int(selected.size()) + 1);
    QPointF current;
    const auto moveTo = [&](const QPointF& point) {
      if (!opaque) path.moveTo(point);
      current = point;
    };
    const auto lineTo = [&](const QPointF& point) {
      if (opaque) lines.append(QLineF(current, point));
      else path.lineTo(point);
      current = point;
    };
    bool started = false;
    for (const auto& sample : selected) {
      const auto point = mapSample(sample, range, config.scale, area, begin, end);
      if (!point) {
        if (started) {
          const qint64 sampleTime = milliseconds(sample.timestamp);
          const qint64 rangeStart = milliseconds(visibleTimeRange_.start);
          const qint64 rangeEnd = milliseconds(visibleTimeRange_.end);
          if (rangeEnd > rangeStart) {
            const qreal x = area.left() + area.width() *
                double(sampleTime - rangeStart) / double(rangeEnd - rangeStart);
            lineTo(QPointF(x, current.y()));
          }
        }
        started = false;
        continue;
      }
      if (!started) { moveTo(*point); started = true; }
      else {
        // Channel values are piecewise constant between monitor updates.
        lineTo(QPointF(point->x(), current.y()));
        lineTo(*point);
      }
    }
    if (started && !selected.empty() && selected.back().plotable) {
      const qint64 rangeStart = milliseconds(visibleTimeRange_.start);
      const qint64 rangeEnd = milliseconds(visibleTimeRange_.end);
      const qint64 heldEnd = milliseconds(traceEnd);
      if (rangeEnd > rangeStart) {
        const qreal x = area.left() + area.width() *
            double(heldEnd - rangeStart) / double(rangeEnd - rangeStart);
        lineTo(QPointF(x, current.y()));
      }
    }
    painter.setPen(QPen(curveColor, lineWidth));
    painter.save();
    painter.setClipRect(area);
    if (opaque) painter.drawLines(lines);
    else painter.drawPath(path);
    painter.restore();
  }
}

void PlotWidget::mouseMoveEvent(QMouseEvent* event) {
  if ((dragMode_ == DragMode::Pan && !(event->buttons() & Qt::LeftButton)) ||
      (dragMode_ == DragMode::Annotation && !(event->buttons() & Qt::MiddleButton)))
    finishDrag();
  const QPoint previousCursor = cursorPosition_;
  cursorPosition_ = event->pos();
  const bool dragging = dragMode_ != DragMode::None;
  updateDrag(event->pos());
  // A drag can move the view; otherwise only the cursor has changed.
  if (dragging) redraw();
  const QRectF area = currentPlotRect();
  QString legendTip;
  const auto curves = plottedCurves();
  for (std::size_t row = 0; row < curves.size(); ++row) {
    if (!legendRect(row, area).contains(event->pos())) continue;
    legendTip = legendLines(curves[row]).join(QLatin1Char('\n'));
    break;
  }
  if (toolTip() != legendTip) setToolTip(legendTip);
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
      const double plotted = rangeValue(range, (event->pos().y() - area.top()) / area.height());
      value = model_.curves[static_cast<std::size_t>(curveIndex)].scale == ScaleMode::Log10
                  ? std::pow(10.0, plotted) : plotted;
    }
    emit cursorLocationChanged(QDateTime::fromMSecsSinceEpoch(time), value, curveIndex);
  }
  if (!dragging) {
    // Repaint the cursor lines where they were and where they are now.
    const QRect bounds = area.toAlignedRect().adjusted(-1, -1, 1, 1);
    QRegion changed;
    for (const QPoint& point : {previousCursor, cursorPosition_}) {
      if (!area.contains(point)) continue;
      changed += QRect(point.x() - 1, bounds.top(), 3, bounds.height());
      changed += QRect(bounds.left(), point.y() - 1, bounds.width(), 3);
    }
    if (!changed.isEmpty()) update(changed);
  }
}

void PlotWidget::updateDrag(const QPoint& position) {
  if (dragMode_ == DragMode::None) return;
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
        const double span = std::chrono::duration<double>(duration).count();
        const double start = epochSeconds(dragRange_.start) + span * fraction;
        if (!representableView(start, start + span)) return;
        const auto shift = std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::duration<double>(std::chrono::duration<double>(duration).count() * fraction));
        visibleTimeRange_ = {dragRange_.start + shift, dragRange_.end + shift};
        setAutoScroll(false);
        updateAutoRange();
        emit visibleTimeRangeChanged();
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
  const QRectF area = plotRect();
  if (event->button() == Qt::LeftButton) {
    for (std::size_t position = 0; position < curves.size(); ++position) {
      if (legendRect(position, area).contains(event->pos())) {
        selectedCurve_ = static_cast<int>(curves[position]);
        redraw();
        return;
      }
    }
  }
  if (!area.contains(event->pos())) return;
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
  // Catch a live view up to now without discarding its zoomed duration.
  if (resumeAutoScroll) {
    advanceToNow();
    emit visibleTimeRangeChanged();
  } else {
    updateAutoRange();
    redraw();
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
